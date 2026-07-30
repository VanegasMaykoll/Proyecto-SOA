// Copyright (c) 2026 Proyecto SOA. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Ribbon Filter implementation for LevelDB 1.23.
//
// Algorithm based on:
//   "Ribbon filter: practically smaller than Bloom and Xor"
//   Peter C. Dillinger and Stefan Walzer, 2021 (arXiv:2103.02515)
//
// Reference implementation: FastFilter/fastfilter_cpp
//
// This is a standalone implementation adapted to LevelDB's FilterPolicy
// interface. It does NOT depend on RocksDB code.

#include "util/ribbon_filter.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

#include "leveldb/slice.h"
#include "util/hash.h"

namespace leveldb {

namespace {

// ============================================================
// Constants
// ============================================================

// Width of the coefficient row (the "ribbon"). Each input key produces
// a coefficient row of this many bits, placed at a start position in
// the matrix. 64 bits matches uint64_t and provides good performance.
static constexpr int kRibbonWidth = 64;

// Overhead factor: num_slots = ceil(n * kOverheadNumerator / kOverheadDenominator)
// A 5% overhead provides a high probability of successful construction.
static constexpr int kOverheadNumerator = 105;
static constexpr int kOverheadDenominator = 100;

// Maximum number of seed attempts before falling back.
static constexpr int kMaxSeedTries = 64;

// ============================================================
// Portable count-trailing-zeros for uint64_t
// ============================================================

static inline int CountTrailingZeros64(uint64_t x) {
  assert(x != 0);
#if defined(__GNUC__) || defined(__clang__)
  return __builtin_ctzll(x);
#elif defined(_MSC_VER)
  unsigned long index;
#if defined(_WIN64)
  _BitScanForward64(&index, x);
#else
  // 32-bit MSVC fallback
  if (static_cast<uint32_t>(x) != 0) {
    _BitScanForward(&index, static_cast<uint32_t>(x));
  } else {
    _BitScanForward(&index, static_cast<uint32_t>(x >> 32));
    index += 32;
  }
#endif
  return static_cast<int>(index);
#else
  // Generic fallback
  int count = 0;
  while ((x & 1) == 0) {
    x >>= 1;
    count++;
  }
  return count;
#endif
}

// ============================================================
// Hashing
// ============================================================

// Produce a 64-bit hash from a key and a seed by combining two
// independent 32-bit hashes from LevelDB's built-in Hash function.
static uint64_t RibbonHash64(const char* data, size_t n, uint32_t seed) {
  uint32_t h1 = Hash(data, n, seed);
  uint32_t h2 = Hash(data, n, seed + 0x9E3779B97F4A7C15ULL);
  return (static_cast<uint64_t>(h1) << 32) | h2;
}

// Map a 32-bit hash value uniformly to [0, range) using multiplication.
static uint32_t FastRange32(uint32_t hash, uint32_t range) {
  return static_cast<uint32_t>(
      (static_cast<uint64_t>(hash) * static_cast<uint64_t>(range)) >> 32);
}

// ============================================================
// Key derivation from hash
// ============================================================

// Derive the start position (row index) from a hash value.
static uint32_t DeriveStart(uint64_t h1, uint32_t num_starts) {
  return FastRange32(static_cast<uint32_t>(h1 >> 32), num_starts);
}

// Derive the coefficient row from a second hash value.
// The lowest bit is always set to ensure the coefficient row has a
// leading coefficient at the start position.
static uint64_t DeriveCoeffRow(uint64_t h2) {
  return h2 | 1;
}

// Derive the result value from a hash value.
// Uses the lower bits of h1 (independent from the upper bits used for start).
static uint32_t DeriveResult(uint64_t h1, int result_bits) {
  return static_cast<uint32_t>(h1) & ((1u << result_bits) - 1);
}

// ============================================================
// Ribbon Filter Policy implementation
// ============================================================

class RibbonFilterPolicy : public FilterPolicy {
 public:
  explicit RibbonFilterPolicy(int bits_per_key)
      : bits_per_key_(bits_per_key) {
    // Compute the number of result bits needed to match a Bloom filter's
    // false positive rate with the given bits_per_key.
    //
    // For a Bloom filter with optimal k:
    //   FPR ≈ (1 - e^(-k*n/m))^k ≈ 0.6185^(bits_per_key)
    //
    // For a Ribbon filter:
    //   FPR = 2^(-result_bits)
    //
    // Matching: result_bits = ceil(bits_per_key * ln(2))
    //                       = ceil(bits_per_key * 0.6931)
    //
    // For bits_per_key=10: result_bits = ceil(6.931) = 7
    // This gives FPR = 2^(-7) ≈ 0.78%, comparable to Bloom's ~0.82%.
    result_bits_ = static_cast<int>(std::ceil(bits_per_key * 0.6931));
    if (result_bits_ < 1) result_bits_ = 1;
    if (result_bits_ > 16) result_bits_ = 16;
  }

  const char* Name() const override {
    return "leveldb.RibbonFilter";
  }

  void CreateFilter(const Slice* keys, int n, std::string* dst) const override {
    if (n <= 0) {
      // Empty filter: store just the metadata marker
      PushMetadata(dst, 0, 0);
      return;
    }

    // Handle very small filters: use at least kRibbonWidth slots
    uint32_t num_slots;
    if (n < kRibbonWidth) {
      num_slots = static_cast<uint32_t>(kRibbonWidth + n);
    } else {
      num_slots = static_cast<uint32_t>(
          (static_cast<uint64_t>(n) * kOverheadNumerator) /
          kOverheadDenominator) + 1;
    }

    uint32_t num_starts = num_slots - kRibbonWidth + 1;

    // Try banding with different seeds
    for (uint32_t seed = 0; seed < kMaxSeedTries; seed++) {
      if (TryBuildFilter(keys, n, num_slots, num_starts, seed, dst)) {
        return;  // Success
      }
    }

    // All seeds failed (extremely unlikely). Fall back to an "always match"
    // filter. This is safe: it just means more false positives.
    PushMetadata(dst, 0, 0);
  }

  bool KeyMayMatch(const Slice& key, const Slice& filter) const override {
    const size_t len = filter.size();

    // Minimum filter size: 5 bytes of metadata
    if (len < 5) return true;  // No filter data, assume match

    const char* data = filter.data();

    // Read metadata from the end of the filter
    uint8_t stored_result_bits = static_cast<uint8_t>(data[len - 1]);
    uint32_t stored_seed = DecodeFixed32(data + len - 5);

    if (stored_result_bits == 0) {
      return true;  // Empty/fallback filter, always match
    }

    // Compute number of slots from the solution data size
    size_t solution_size = len - 5;  // subtract metadata bytes
    uint32_t bytes_per_slot = (stored_result_bits <= 8) ? 1 : 2;
    uint32_t num_slots = static_cast<uint32_t>(solution_size / bytes_per_slot);

    if (num_slots < static_cast<uint32_t>(kRibbonWidth)) {
      return true;  // Filter too small, assume match
    }

    uint32_t num_starts = num_slots - kRibbonWidth + 1;

    // Hash the key
    uint64_t h1 = RibbonHash64(key.data(), key.size(), stored_seed);
    uint64_t h2 = RibbonHash64(key.data(), key.size(),
                                 stored_seed + 0x12345678);

    uint32_t start = DeriveStart(h1, num_starts);
    uint64_t coeff = DeriveCoeffRow(h2);
    uint32_t expected_result = DeriveResult(h1, stored_result_bits);

    // Compute actual result: XOR of solution values where coefficient
    // bits are set.
    uint32_t actual_result = 0;
    uint64_t c = coeff;
    uint32_t pos = start;

    while (c != 0) {
      int bit = CountTrailingZeros64(c);
      pos += bit;
      c >>= bit;

      if (pos >= num_slots) break;

      if (bytes_per_slot == 1) {
        actual_result ^= static_cast<uint8_t>(data[pos]);
      } else {
        actual_result ^= static_cast<uint8_t>(data[pos * 2]) |
                         (static_cast<uint32_t>(
                              static_cast<uint8_t>(data[pos * 2 + 1])) << 8);
      }

      c >>= 1;
      pos += 1;
    }

    return (actual_result & ((1u << stored_result_bits) - 1)) ==
           expected_result;
  }

 private:
  int bits_per_key_;
  int result_bits_;

  // --------------------------------------------------------
  // Serialization helpers
  // --------------------------------------------------------

  // Encode a 32-bit value in little-endian.
  static void EncodeFixed32(char* buf, uint32_t value) {
    buf[0] = static_cast<char>(value & 0xff);
    buf[1] = static_cast<char>((value >> 8) & 0xff);
    buf[2] = static_cast<char>((value >> 16) & 0xff);
    buf[3] = static_cast<char>((value >> 24) & 0xff);
  }

  // Decode a 32-bit value from little-endian.
  static uint32_t DecodeFixed32(const char* buf) {
    return (static_cast<uint32_t>(static_cast<uint8_t>(buf[0]))) |
           (static_cast<uint32_t>(static_cast<uint8_t>(buf[1])) << 8) |
           (static_cast<uint32_t>(static_cast<uint8_t>(buf[2])) << 16) |
           (static_cast<uint32_t>(static_cast<uint8_t>(buf[3])) << 24);
  }

  // Append metadata to the filter.
  // Format: [4 bytes: seed, little-endian] [1 byte: result_bits]
  static void PushMetadata(std::string* dst, uint32_t seed,
                           uint8_t result_bits) {
    char buf[4];
    EncodeFixed32(buf, seed);
    dst->append(buf, 4);
    dst->push_back(static_cast<char>(result_bits));
  }

  // --------------------------------------------------------
  // Core Ribbon construction
  // --------------------------------------------------------

  // Try to build a filter with the given seed. Returns true on success
  // (filter data appended to dst), false on banding failure.
  bool TryBuildFilter(const Slice* keys, int n, uint32_t num_slots,
                      uint32_t num_starts, uint32_t seed,
                      std::string* dst) const {
    // Phase 1: Hash all keys and derive (start, coeff, result) triples.
    struct Row {
      uint32_t start;
      uint64_t coeff;
      uint32_t result;
    };

    std::vector<Row> rows(n);
    for (int i = 0; i < n; i++) {
      uint64_t h1 = RibbonHash64(keys[i].data(), keys[i].size(), seed);
      uint64_t h2 = RibbonHash64(keys[i].data(), keys[i].size(),
                                   seed + 0x12345678);
      rows[i].start = DeriveStart(h1, num_starts);
      rows[i].coeff = DeriveCoeffRow(h2);
      rows[i].result = DeriveResult(h1, result_bits_);
    }

    // Sort by start position for better cache locality during banding.
    std::sort(rows.begin(), rows.end(),
              [](const Row& a, const Row& b) { return a.start < b.start; });

    // Phase 2: Banding (incremental Gaussian elimination).
    //
    // We maintain a partially reduced upper-triangular matrix.
    // pivots[col] stores the coefficient row for the pivot at column col.
    // pivot_results[col] stores the corresponding result value.
    // If pivots[col] == 0, column col is free (no pivot).

    std::vector<uint64_t> pivots(num_slots, 0);
    std::vector<uint32_t> pivot_results(num_slots, 0);

    for (int i = 0; i < n; i++) {
      uint64_t c = rows[i].coeff;
      uint32_t r = rows[i].result;
      uint32_t s = rows[i].start;

      while (c != 0) {
        int lsb = CountTrailingZeros64(c);
        s += lsb;
        c >>= lsb;

        if (s >= num_slots) {
          // Row extends beyond the matrix. If result is non-zero,
          // construction fails.
          if (r != 0) return false;
          c = 0;
          break;
        }

        if (pivots[s] == 0) {
          // Free column: place this row as a pivot.
          pivots[s] = c;
          pivot_results[s] = r;
          break;
        }

        // Column occupied: eliminate by XOR-ing with existing pivot.
        c ^= pivots[s];
        r ^= pivot_results[s];
      }

      if (c == 0 && r != 0) {
        // Row was fully eliminated but result is non-zero: contradiction.
        return false;
      }
    }

    // Phase 3: Back-substitution.
    //
    // Solve for the solution vector from the upper-triangular system.
    // For each column with a pivot, compute:
    //   solution[col] = pivot_result[col] XOR (sum of solution[col+j]
    //                   for each set bit j > 0 in pivots[col])

    std::vector<uint32_t> solution(num_slots, 0);

    for (int col = static_cast<int>(num_slots) - 1; col >= 0; col--) {
      if (pivots[col] == 0) {
        solution[col] = 0;  // Free variable, set to 0
        continue;
      }

      uint32_t val = pivot_results[col];
      uint64_t c = pivots[col] >> 1;  // Skip the pivot bit itself (bit 0)
      int j = 1;
      while (c != 0 && (col + j) < static_cast<int>(num_slots)) {
        if (c & 1) {
          val ^= solution[col + j];
        }
        c >>= 1;
        j++;
      }
      solution[col] = val;
    }

    // Phase 4: Serialize the solution.
    //
    // Format: [solution_data...][seed (4 bytes LE)][result_bits (1 byte)]
    //
    // Solution data: one byte per slot if result_bits <= 8,
    //                two bytes per slot if 9 <= result_bits <= 16.

    if (result_bits_ <= 8) {
      for (uint32_t i = 0; i < num_slots; i++) {
        dst->push_back(static_cast<char>(solution[i] & 0xFF));
      }
    } else {
      for (uint32_t i = 0; i < num_slots; i++) {
        dst->push_back(static_cast<char>(solution[i] & 0xFF));
        dst->push_back(static_cast<char>((solution[i] >> 8) & 0xFF));
      }
    }

    PushMetadata(dst, seed, static_cast<uint8_t>(result_bits_));
    return true;
  }
};

}  // namespace

const FilterPolicy* NewRibbonFilterPolicy(int bits_per_key) {
  return new RibbonFilterPolicy(bits_per_key);
}

}  // namespace leveldb
