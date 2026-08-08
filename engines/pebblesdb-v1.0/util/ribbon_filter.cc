// Copyright (c) 2026 Proyecto SOA. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Packed Ribbon Filter implementation for PebblesDB.
//
// Algorithmic basis:
//   "Ribbon filter: practically smaller than Bloom and Xor"
//   Peter C. Dillinger and Stefan Walzer, 2021.
//
// This implementation is independent from RocksDB. It adapts a 64-bit Ribbon
// construction to PebblesDB's FilterPolicy interface and uses a compact,
// versioned serialized format.
//
// Important behavior:
//   * The public precision parameter is Bloom-equivalent bits per key.
//   * Ribbon solution values are bit-packed instead of byte-aligned.
//   * Very small/non-beneficial filters use an internal Bloom fallback.
//   * Failed Ribbon construction also falls back to Bloom.
//   * Corrupt or unknown encodings conservatively return "may match".

#include "util/ribbon_filter.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "pebblesdb/slice.h"
#include "util/hash.h"

namespace leveldb {
namespace {

// -----------------------------------------------------------------------------
// Ribbon parameters
// -----------------------------------------------------------------------------

static const uint32_t kRibbonWidth = 64;

// Approximately 5% more slots than equations.
static const uint32_t kOverheadNumerator = 105;
static const uint32_t kOverheadDenominator = 100;

static const uint32_t kMaxSeedTries = 64;

// Explicit 32-bit constants. These avoid silent truncation of 64-bit literals
// when passed to PebblesDB's 32-bit Hash seed.
static const uint32_t kSecondHashDelta = 0x7F4A7C15u;
static const uint32_t kCoefficientSeedDelta = 0x12345678u;

// -----------------------------------------------------------------------------
// Serialized format
// -----------------------------------------------------------------------------
//
// [payload]
// [seed:        4 bytes little-endian]
// [slot_count:  4 bytes little-endian]
// [aux:         1 byte]
// [type:        1 byte]
// [version:     1 byte]
// [magic:       1 byte]
//
// For Ribbon:
//   slot_count = number of solution slots
//   aux        = result bits per slot
//
// For Bloom fallback:
//   slot_count = total Bloom bits
//   aux        = number of probes

static const size_t kTrailerSize = 12;
static const uint8_t kFormatMagic = 0xB7;
static const uint8_t kFormatVersion = 1;

enum FilterEncodingType {
  kEncodingEmpty = 0,
  kEncodingRibbon = 1,
  kEncodingBloom = 2,
  kEncodingAlwaysMatch = 3
};

// -----------------------------------------------------------------------------
// General helpers
// -----------------------------------------------------------------------------

static inline int CountTrailingZeros64(uint64_t value) {
  assert(value != 0);
#if defined(__GNUC__) || defined(__clang__)
  return __builtin_ctzll(value);
#else
  int count = 0;
  while ((value & 1u) == 0) {
    value >>= 1;
    ++count;
  }
  return count;
#endif
}

static void AppendFixed32(std::string* dst, uint32_t value) {
  dst->push_back(static_cast<char>(value & 0xffu));
  dst->push_back(static_cast<char>((value >> 8) & 0xffu));
  dst->push_back(static_cast<char>((value >> 16) & 0xffu));
  dst->push_back(static_cast<char>((value >> 24) & 0xffu));
}

static uint32_t DecodeFixed32(const char* data) {
  return static_cast<uint32_t>(static_cast<uint8_t>(data[0])) |
         (static_cast<uint32_t>(static_cast<uint8_t>(data[1])) << 8) |
         (static_cast<uint32_t>(static_cast<uint8_t>(data[2])) << 16) |
         (static_cast<uint32_t>(static_cast<uint8_t>(data[3])) << 24);
}

static void AppendTrailer(std::string* dst,
                          uint32_t seed,
                          uint32_t slot_count,
                          uint8_t aux,
                          uint8_t type) {
  AppendFixed32(dst, seed);
  AppendFixed32(dst, slot_count);
  dst->push_back(static_cast<char>(aux));
  dst->push_back(static_cast<char>(type));
  dst->push_back(static_cast<char>(kFormatVersion));
  dst->push_back(static_cast<char>(kFormatMagic));
}

static uint32_t BitMask(int bits) {
  assert(bits >= 1 && bits <= 16);
  return (1u << bits) - 1u;
}

static size_t PackedByteSize(uint32_t value_count, int bits_per_value) {
  const uint64_t total_bits =
      static_cast<uint64_t>(value_count) *
      static_cast<uint64_t>(bits_per_value);
  return static_cast<size_t>((total_bits + 7u) / 8u);
}

static void AppendPackedValues(const std::vector<uint32_t>& values,
                               int bits_per_value,
                               std::string* dst) {
  const uint32_t mask = BitMask(bits_per_value);
  uint64_t pending = 0;
  int pending_bits = 0;

  for (size_t i = 0; i < values.size(); ++i) {
    pending |= static_cast<uint64_t>(values[i] & mask) << pending_bits;
    pending_bits += bits_per_value;

    while (pending_bits >= 8) {
      dst->push_back(static_cast<char>(pending & 0xffu));
      pending >>= 8;
      pending_bits -= 8;
    }
  }

  if (pending_bits > 0) {
    dst->push_back(static_cast<char>(pending & 0xffu));
  }
}

static bool ReadPackedValue(const char* payload,
                            size_t payload_size,
                            uint32_t index,
                            int bits_per_value,
                            uint32_t* value) {
  const uint64_t bit_offset =
      static_cast<uint64_t>(index) *
      static_cast<uint64_t>(bits_per_value);
  const size_t byte_offset = static_cast<size_t>(bit_offset / 8u);
  const int shift = static_cast<int>(bit_offset % 8u);
  const int bytes_needed = (shift + bits_per_value + 7) / 8;

  if (byte_offset + static_cast<size_t>(bytes_needed) > payload_size) {
    return false;
  }

  uint64_t word = 0;
  for (int i = 0; i < bytes_needed; ++i) {
    word |= static_cast<uint64_t>(
                static_cast<uint8_t>(payload[byte_offset + i]))
            << (8 * i);
  }

  *value = static_cast<uint32_t>((word >> shift) & BitMask(bits_per_value));
  return true;
}

// -----------------------------------------------------------------------------
// Hash derivation
// -----------------------------------------------------------------------------

static uint64_t RibbonHash64(const char* data,
                             size_t size,
                             uint32_t seed) {
  const uint32_t high = Hash(data, size, seed);
  const uint32_t low = Hash(data, size, seed + kSecondHashDelta);
  return (static_cast<uint64_t>(high) << 32) | low;
}

static uint32_t FastRange32(uint32_t hash, uint32_t range) {
  if (range == 0) {
    return 0;
  }

  return static_cast<uint32_t>(
      (static_cast<uint64_t>(hash) * static_cast<uint64_t>(range)) >> 32);
}

static uint32_t DeriveStart(uint64_t hash, uint32_t start_count) {
  return FastRange32(static_cast<uint32_t>(hash >> 32), start_count);
}

static uint64_t DeriveCoefficient(uint64_t hash) {
  // Bit zero is always a coefficient, so the row has a pivot candidate at
  // its initial position.
  return hash | 1u;
}

static uint32_t DeriveResult(uint64_t hash, int result_bits) {
  return static_cast<uint32_t>(hash) & BitMask(result_bits);
}

// -----------------------------------------------------------------------------
// Internal Bloom fallback
// -----------------------------------------------------------------------------

static uint32_t BloomProbeCount(int bits_per_key) {
  uint32_t probes = static_cast<uint32_t>(bits_per_key * 0.69);
  if (probes < 1) {
    probes = 1;
  }
  if (probes > 30) {
    probes = 30;
  }
  return probes;
}

static uint32_t BloomBitCount(int key_count, int bits_per_key) {
  uint64_t bits = static_cast<uint64_t>(key_count) *
                  static_cast<uint64_t>(bits_per_key);

  if (bits < 64) {
    bits = 64;
  }

  bits = (bits + 7u) & ~static_cast<uint64_t>(7u);

  if (bits > 0xffffffffu) {
    return 0;
  }

  return static_cast<uint32_t>(bits);
}

static size_t EstimatedBloomSerializedSize(int key_count,
                                           int bits_per_key) {
  const uint32_t bit_count = BloomBitCount(key_count, bits_per_key);
  if (bit_count == 0) {
    return static_cast<size_t>(-1);
  }
  return static_cast<size_t>(bit_count / 8u) + kTrailerSize;
}

static void BuildBloomEncoding(const Slice* keys,
                               int key_count,
                               int bits_per_key,
                               std::string* encoded) {
  const uint32_t bit_count = BloomBitCount(key_count, bits_per_key);

  if (bit_count == 0) {
    AppendTrailer(encoded, 0, 0, 0, kEncodingAlwaysMatch);
    return;
  }

  const uint32_t probes = BloomProbeCount(bits_per_key);
  const size_t byte_count = bit_count / 8u;
  const size_t payload_offset = encoded->size();

  encoded->append(byte_count, static_cast<char>(0));

  for (int i = 0; i < key_count; ++i) {
    uint32_t hash = Hash(keys[i].data(), keys[i].size(), 0xbc9f1d34u);
    const uint32_t delta = (hash >> 17) | (hash << 15);

    for (uint32_t probe = 0; probe < probes; ++probe) {
      const uint32_t bit_position = hash % bit_count;
      (*encoded)[payload_offset + bit_position / 8u] |=
          static_cast<char>(1u << (bit_position % 8u));
      hash += delta;
    }
  }

  AppendTrailer(encoded,
                0,
                bit_count,
                static_cast<uint8_t>(probes),
                kEncodingBloom);
}

static bool BloomMayMatch(const Slice& key,
                          const char* payload,
                          size_t payload_size,
                          uint32_t bit_count,
                          uint8_t probes) {
  if (bit_count < 64 || probes < 1 || probes > 30 ||
      payload_size != static_cast<size_t>((bit_count + 7u) / 8u)) {
    return true;
  }

  uint32_t hash = Hash(key.data(), key.size(), 0xbc9f1d34u);
  const uint32_t delta = (hash >> 17) | (hash << 15);

  for (uint32_t probe = 0; probe < probes; ++probe) {
    const uint32_t bit_position = hash % bit_count;
    if ((static_cast<uint8_t>(payload[bit_position / 8u]) &
         (1u << (bit_position % 8u))) == 0) {
      return false;
    }
    hash += delta;
  }

  return true;
}

// -----------------------------------------------------------------------------
// Ribbon construction
// -----------------------------------------------------------------------------

struct RibbonRow {
  uint32_t start;
  uint64_t coefficients;
  uint32_t result;
};

struct RibbonRowStartLess {
  bool operator()(const RibbonRow& left, const RibbonRow& right) const {
    return left.start < right.start;
  }
};

static uint32_t RibbonSlotCount(int key_count) {
  if (key_count <= 0) {
    return 0;
  }

  uint64_t slot_count;
  if (key_count < static_cast<int>(kRibbonWidth)) {
    slot_count = kRibbonWidth + static_cast<uint32_t>(key_count);
  } else {
    slot_count =
        (static_cast<uint64_t>(key_count) * kOverheadNumerator +
         kOverheadDenominator - 1u) /
        kOverheadDenominator;
  }

  if (slot_count < kRibbonWidth) {
    slot_count = kRibbonWidth;
  }

  if (slot_count > 0xffffffffu) {
    return 0;
  }

  return static_cast<uint32_t>(slot_count);
}

static bool TryBuildRibbon(const Slice* keys,
                           int key_count,
                           uint32_t slot_count,
                           int result_bits,
                           uint32_t seed,
                           std::string* encoded) {
  if (slot_count < kRibbonWidth) {
    return false;
  }

  const uint32_t start_count = slot_count - kRibbonWidth + 1u;
  std::vector<RibbonRow> rows(static_cast<size_t>(key_count));

  for (int i = 0; i < key_count; ++i) {
    const uint64_t result_hash =
        RibbonHash64(keys[i].data(), keys[i].size(), seed);
    const uint64_t coefficient_hash = RibbonHash64(
        keys[i].data(), keys[i].size(), seed + kCoefficientSeedDelta);

    rows[i].start = DeriveStart(result_hash, start_count);
    rows[i].coefficients = DeriveCoefficient(coefficient_hash);
    rows[i].result = DeriveResult(result_hash, result_bits);
  }

  std::sort(rows.begin(), rows.end(), RibbonRowStartLess());

  std::vector<uint64_t> pivots(slot_count, 0);
  std::vector<uint32_t> pivot_results(slot_count, 0);

  for (int i = 0; i < key_count; ++i) {
    uint64_t coefficients = rows[i].coefficients;
    uint32_t result = rows[i].result;
    uint32_t position = rows[i].start;

    while (coefficients != 0) {
      const int leading_zeroes = CountTrailingZeros64(coefficients);
      position += static_cast<uint32_t>(leading_zeroes);
      coefficients >>= leading_zeroes;

      if (position >= slot_count) {
        if (result != 0) {
          return false;
        }
        coefficients = 0;
        break;
      }

      if (pivots[position] == 0) {
        pivots[position] = coefficients;
        pivot_results[position] = result;
        coefficients = 0;
        result = 0;
        break;
      }

      coefficients ^= pivots[position];
      result ^= pivot_results[position];
    }

    if (coefficients == 0 && result != 0) {
      return false;
    }
  }

  std::vector<uint32_t> solution(slot_count, 0);

  for (int position = static_cast<int>(slot_count) - 1;
       position >= 0;
       --position) {
    if (pivots[position] == 0) {
      continue;
    }

    uint32_t value = pivot_results[position];
    uint64_t remaining = pivots[position] >> 1;
    int relative = 1;

    while (remaining != 0 &&
           position + relative < static_cast<int>(slot_count)) {
      if ((remaining & 1u) != 0) {
        value ^= solution[position + relative];
      }
      remaining >>= 1;
      ++relative;
    }

    solution[position] = value & BitMask(result_bits);
  }

  AppendPackedValues(solution, result_bits, encoded);
  AppendTrailer(encoded,
                seed,
                slot_count,
                static_cast<uint8_t>(result_bits),
                kEncodingRibbon);
  return true;
}

// -----------------------------------------------------------------------------
// FilterPolicy
// -----------------------------------------------------------------------------

class RibbonFilterPolicy : public FilterPolicy {
 public:
  explicit RibbonFilterPolicy(int bloom_equivalent_bits_per_key)
      : bits_per_key_(bloom_equivalent_bits_per_key),
        result_bits_(ComputeResultBits(bloom_equivalent_bits_per_key)) {}

  const char* Name() const {
    // Change this name whenever the serialized representation changes.
    return "proyecto-soa.RibbonFilter64Packed.v1";
  }

  void CreateFilter(const Slice* keys,
                    int key_count,
                    std::string* dst) const {
    if (key_count <= 0) {
      std::string encoded;
      AppendTrailer(&encoded, 0, 0, 0, kEncodingEmpty);
      dst->append(encoded);
      return;
    }

    const uint32_t slot_count = RibbonSlotCount(key_count);

    if (slot_count == 0) {
      std::string encoded;
      BuildBloomEncoding(keys, key_count, bits_per_key_, &encoded);
      dst->append(encoded);
      return;
    }

    const size_t ribbon_size =
        PackedByteSize(slot_count, result_bits_) + kTrailerSize;
    const size_t bloom_size =
        EstimatedBloomSerializedSize(key_count, bits_per_key_);

    // A 64-position band has a fixed minimum cost. For small sets, Bloom is
    // more compact and avoids wasting construction CPU.
    if (ribbon_size >= bloom_size) {
      std::string encoded;
      BuildBloomEncoding(keys, key_count, bits_per_key_, &encoded);
      dst->append(encoded);
      return;
    }

    for (uint32_t seed = 0; seed < kMaxSeedTries; ++seed) {
      std::string encoded;
      if (TryBuildRibbon(keys,
                         key_count,
                         slot_count,
                         result_bits_,
                         seed,
                         &encoded)) {
        dst->append(encoded);
        return;
      }
    }

    // Safe fallback: Bloom preserves zero false negatives and remains useful,
    // unlike an always-match marker.
    std::string encoded;
    BuildBloomEncoding(keys, key_count, bits_per_key_, &encoded);
    dst->append(encoded);
  }

  bool KeyMayMatch(const Slice& key, const Slice& filter) const {
    if (filter.size() < kTrailerSize) {
      return true;
    }

    const char* data = filter.data();
    const size_t trailer_offset = filter.size() - kTrailerSize;
    const char* trailer = data + trailer_offset;

    const uint32_t seed = DecodeFixed32(trailer);
    const uint32_t slot_count = DecodeFixed32(trailer + 4);
    const uint8_t aux = static_cast<uint8_t>(trailer[8]);
    const uint8_t type = static_cast<uint8_t>(trailer[9]);
    const uint8_t version = static_cast<uint8_t>(trailer[10]);
    const uint8_t magic = static_cast<uint8_t>(trailer[11]);

    if (magic != kFormatMagic || version != kFormatVersion) {
      return true;
    }

    const char* payload = data;
    const size_t payload_size = trailer_offset;

    if (type == kEncodingEmpty) {
      return payload_size == 0 ? false : true;
    }

    if (type == kEncodingAlwaysMatch) {
      return true;
    }

    if (type == kEncodingBloom) {
      return BloomMayMatch(key, payload, payload_size, slot_count, aux);
    }

    if (type != kEncodingRibbon) {
      return true;
    }

    const int result_bits = static_cast<int>(aux);
    if (result_bits < 1 || result_bits > 16 ||
        slot_count < kRibbonWidth ||
        payload_size != PackedByteSize(slot_count, result_bits)) {
      return true;
    }

    const uint32_t start_count = slot_count - kRibbonWidth + 1u;
    const uint64_t result_hash =
        RibbonHash64(key.data(), key.size(), seed);
    const uint64_t coefficient_hash = RibbonHash64(
        key.data(), key.size(), seed + kCoefficientSeedDelta);

    uint32_t position = DeriveStart(result_hash, start_count);
    uint64_t coefficients = DeriveCoefficient(coefficient_hash);
    const uint32_t expected = DeriveResult(result_hash, result_bits);
    uint32_t actual = 0;

    while (coefficients != 0) {
      const int leading_zeroes = CountTrailingZeros64(coefficients);
      position += static_cast<uint32_t>(leading_zeroes);
      coefficients >>= leading_zeroes;

      if (position >= slot_count) {
        return true;
      }

      uint32_t value = 0;
      if (!ReadPackedValue(payload,
                           payload_size,
                           position,
                           result_bits,
                           &value)) {
        return true;
      }

      actual ^= value;
      coefficients >>= 1;
      ++position;
    }

    return (actual & BitMask(result_bits)) == expected;
  }

 private:
  static int ComputeResultBits(int bloom_equivalent_bits_per_key) {
    int result_bits = static_cast<int>(std::ceil(
        bloom_equivalent_bits_per_key * 0.6931471805599453));

    if (result_bits < 1) {
      result_bits = 1;
    }
    if (result_bits > 16) {
      result_bits = 16;
    }

    return result_bits;
  }

  const int bits_per_key_;
  const int result_bits_;
};

}  // namespace

const FilterPolicy* NewRibbonFilterPolicy(
    int bloom_equivalent_bits_per_key) {
  if (bloom_equivalent_bits_per_key <= 0) {
    return NULL;
  }

  return new RibbonFilterPolicy(bloom_equivalent_bits_per_key);
}

}  // namespace leveldb
