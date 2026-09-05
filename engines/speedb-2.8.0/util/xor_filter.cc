// Copyright (c) 2026 Proyecto SOA. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "util/xor_filter.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "rocksdb/slice.h"
#include "table/block_based/filter_policy_internal.h"
#include "util/hash.h"
#include "util/coding.h"

namespace ROCKSDB_NAMESPACE {
namespace {

// -----------------------------------------------------------------------------
// Xor construction parameters
// -----------------------------------------------------------------------------

static const uint32_t kCapacityNumerator = 123;
static const uint32_t kCapacityDenominator = 100;
static const uint32_t kCapacitySlack = 32;
static const uint32_t kMaxSeedTries = 64;

static const size_t kTrailerSize = 5 + 9; // 5 for marker, 9 for Xor specific
static const int8_t kRocksDbMarkerXor = -3;

// -----------------------------------------------------------------------------
// General helpers
// -----------------------------------------------------------------------------

static uint32_t BitMask(int bits) {
  assert(bits >= 1 && bits <= 16);
  return (1u << bits) - 1u;
}

static uint64_t RotateLeft64(uint64_t value, int distance) {
  return (value << distance) | (value >> (64 - distance));
}

static uint32_t FastRange32(uint32_t hash, uint32_t range) {
  if (range == 0) return 0;
  return static_cast<uint32_t>((static_cast<uint64_t>(hash) * static_cast<uint64_t>(range)) >> 32);
}

static void AppendPackedValues(
    const std::vector<uint32_t>& values,
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

static bool ReadPackedValue(const char* payload, size_t payload_size, uint32_t index, int bits_per_value, uint32_t* value) {
  const uint64_t bit_offset = static_cast<uint64_t>(index) * static_cast<uint64_t>(bits_per_value);
  const size_t byte_offset = static_cast<size_t>(bit_offset / 8u);
  const int shift = static_cast<int>(bit_offset % 8u);
  const int bytes_needed = (shift + bits_per_value + 7) / 8;

  if (byte_offset + static_cast<size_t>(bytes_needed) > payload_size) {
    return false;
  }

  uint64_t word = 0;
  for (int i = 0; i < bytes_needed; ++i) {
    word |= static_cast<uint64_t>(static_cast<uint8_t>(payload[byte_offset + i])) << (8 * i);
  }
  *value = static_cast<uint32_t>((word >> shift) & BitMask(bits_per_value));
  return true;
}

// -----------------------------------------------------------------------------
// Hash, positions, and fingerprint
// -----------------------------------------------------------------------------

static uint64_t MixHash(uint64_t hash, uint32_t seed) {
  uint64_t mixed = hash ^ (static_cast<uint64_t>(seed) * 0x9e3779b97f4a7c15ULL);
  mixed *= 0xff51afd7ed558ccdULL;
  mixed ^= mixed >> 33;
  mixed *= 0xc4ceb9fe1a85ec53ULL;
  mixed ^= mixed >> 33;
  return mixed;
}

static uint32_t Position0(uint64_t hash, uint32_t block_length) {
  return FastRange32(static_cast<uint32_t>(hash), block_length);
}

static uint32_t Position1(uint64_t hash, uint32_t block_length) {
  return block_length + FastRange32(static_cast<uint32_t>(RotateLeft64(hash, 21)), block_length);
}

static uint32_t Position2(uint64_t hash, uint32_t block_length) {
  return 2u * block_length + FastRange32(static_cast<uint32_t>(RotateLeft64(hash, 42)), block_length);
}

static void GetPositions(uint64_t hash, uint32_t block_length, uint32_t positions[3]) {
  positions[0] = Position0(hash, block_length);
  positions[1] = Position1(hash, block_length);
  positions[2] = Position2(hash, block_length);
}

static uint32_t Fingerprint(uint64_t hash, int fingerprint_bits) {
  uint64_t mixed = hash;
  mixed ^= mixed >> 32;
  mixed ^= mixed >> 16;
  return static_cast<uint32_t>(mixed) & BitMask(fingerprint_bits);
}

// -----------------------------------------------------------------------------
// Internal Bloom fallback
// -----------------------------------------------------------------------------

static uint32_t BloomProbeCount(int bits_per_key) {
  uint32_t probes = static_cast<uint32_t>(bits_per_key * 0.69);
  if (probes < 1) probes = 1;
  if (probes > 30) probes = 30;
  return probes;
}

static uint32_t BloomBitCount(size_t key_count, int bits_per_key) {
  uint64_t bits = static_cast<uint64_t>(key_count) * static_cast<uint64_t>(bits_per_key);
  if (bits < 64) bits = 64;
  bits = (bits + 7u) & ~static_cast<uint64_t>(7u);
  if (bits > 0xffffffffu) return 0;
  return static_cast<uint32_t>(bits);
}

static int ComputeFingerprintBits(int bits_per_key) {
  int bits = static_cast<int>(std::ceil(bits_per_key * 0.6931471805599453));
  if (bits < 1) bits = 1;
  if (bits > 16) bits = 16;
  return bits;
}

static void BuildBloomFallback(const std::vector<uint64_t>& hashes, int bits_per_key, std::string* dst) {
  const uint32_t bit_count = BloomBitCount(hashes.size(), bits_per_key);
  const uint32_t byte_count = bit_count / 8;
  const uint32_t probes = BloomProbeCount(bits_per_key);

  dst->assign(byte_count, 0);
  char* array = &(*dst)[0];

  for (uint64_t h : hashes) {
    uint32_t h32 = static_cast<uint32_t>(h);
    const uint32_t delta = (h32 >> 17) | (h32 << 15);
    for (uint32_t j = 0; j < probes; j++) {
      const uint32_t bit_pos = h32 % bit_count;
      array[bit_pos / 8] |= (1 << (bit_pos % 8));
      h32 += delta;
    }
  }

  // Use kRocksDbMarkerXor (-3) so RocksDB routes to XorFilterBitsReader.
  // We signal Bloom fallback by setting block_length = 0,
  // storing bit_count in seed, and probes in fingerprint_bits.
  PutFixed32(dst, bit_count);
  PutFixed32(dst, 0);
  dst->push_back(static_cast<char>(probes));
  dst->push_back(static_cast<char>(kRocksDbMarkerXor));
  dst->push_back(static_cast<char>(0));
  dst->push_back(static_cast<char>(0));
  dst->push_back(static_cast<char>(0));
  dst->push_back(static_cast<char>(0));
}

// -----------------------------------------------------------------------------
// Core Builder & Reader
// -----------------------------------------------------------------------------

class XorFilterBitsBuilder : public BuiltinFilterBitsBuilder {
 private:
  int bits_per_key_;
  std::vector<uint64_t> hash_entries_;

 public:
  XorFilterBitsBuilder(int bits_per_key)
      : bits_per_key_(bits_per_key) {}

  void AddKey(const Slice& key) override {
    uint64_t hash = GetSliceHash64(key);
    if (hash_entries_.empty() || hash != hash_entries_.back()) {
      hash_entries_.push_back(hash);
    }
  }

  size_t EstimateEntriesAdded() override {
    return hash_entries_.size();
  }

  size_t CalculateSpace(size_t num_entries) override {
    const int fp_bits = ComputeFingerprintBits(bits_per_key_);
    const uint64_t capacity = (static_cast<uint64_t>(num_entries) * kCapacityNumerator / kCapacityDenominator) + kCapacitySlack;
    const uint32_t block_length = static_cast<uint32_t>(capacity / 3);
    const uint32_t table_size = 3 * block_length;
    return (static_cast<size_t>(table_size) * fp_bits + 7) / 8 + kTrailerSize;
  }

  double EstimatedFpRate(size_t /*num_entries*/, size_t /*bytes*/) override {
    const int fp_bits = ComputeFingerprintBits(bits_per_key_);
    return 1.0 / (1ULL << fp_bits);
  }

  size_t ApproximateNumEntries(size_t bytes) override {
    if (bytes < kTrailerSize) return 0;
    const int fp_bits = ComputeFingerprintBits(bits_per_key_);
    const double bytes_for_table = static_cast<double>(bytes - kTrailerSize);
    return static_cast<size_t>((bytes_for_table * 8.0) / (1.23 * fp_bits));
  }

  using FilterBitsBuilder::Finish;

  Slice Finish(std::unique_ptr<const char[]>* buf) override {
    std::vector<uint64_t>& hashes = hash_entries_;
    std::sort(hashes.begin(), hashes.end());
    hashes.erase(std::unique(hashes.begin(), hashes.end()), hashes.end());

    size_t key_count = hashes.size();
    std::string filter_data;

    if (key_count == 0) {
      // Empty filter
      PutFixed32(&filter_data, 0);
      PutFixed32(&filter_data, 0);
      filter_data.push_back(static_cast<char>(0));
      filter_data.push_back(static_cast<char>(kRocksDbMarkerXor));
      filter_data.push_back(static_cast<char>(0));
      filter_data.push_back(static_cast<char>(0));
      filter_data.push_back(static_cast<char>(0));
      filter_data.push_back(static_cast<char>(0));
      goto done;
    }

    if (bits_per_key_ <= 0 || bits_per_key_ > 16) {
      BuildBloomFallback(hashes, bits_per_key_, &filter_data);
      goto done;
    }

    {
      int fingerprint_bits = ComputeFingerprintBits(bits_per_key_);

      const uint64_t capacity = (static_cast<uint64_t>(key_count) * kCapacityNumerator / kCapacityDenominator) + kCapacitySlack;
      const uint32_t block_length = static_cast<uint32_t>(capacity / 3);
      const uint32_t table_size = 3 * block_length;

      if (table_size > 0xffffffffu || block_length == 0 || key_count > table_size) {
        BuildBloomFallback(hashes, bits_per_key_, &filter_data);
        goto done;
      }

      struct PeelEntry {
        uint64_t hash;
        uint32_t selected_position;
      };

      std::vector<uint32_t> counts(table_size, 0);
      std::vector<uint64_t> hash_xors(table_size, 0);
      std::vector<uint32_t> queue;
      queue.reserve(table_size);
      std::vector<PeelEntry> peel_order;
      peel_order.reserve(key_count);

      uint32_t best_seed = 0;
      bool success = false;

      for (uint32_t seed = 0; seed < kMaxSeedTries; ++seed) {
        std::fill(counts.begin(), counts.end(), 0);
        std::fill(hash_xors.begin(), hash_xors.end(), 0);
        queue.clear();
        peel_order.clear();

        for (size_t i = 0; i < key_count; ++i) {
          uint64_t mixed = MixHash(hashes[i], seed);
          uint32_t pos[3];
          GetPositions(mixed, block_length, pos);
          for (int j = 0; j < 3; ++j) {
            ++counts[pos[j]];
            hash_xors[pos[j]] ^= mixed;
          }
        }

        for (uint32_t i = 0; i < table_size; ++i) {
          if (counts[i] == 1) {
            queue.push_back(i);
          }
        }

        size_t queue_index = 0;
        while (queue_index < queue.size()) {
          const uint32_t selected_position = queue[queue_index++];
          if (counts[selected_position] != 1) {
            continue;
          }

          const uint64_t mixed = hash_xors[selected_position];
          peel_order.push_back({mixed, selected_position});

          uint32_t pos[3];
          GetPositions(mixed, block_length, pos);

          for (int j = 0; j < 3; ++j) {
            const uint32_t p = pos[j];
            if (counts[p] == 0) continue;
            --counts[p];
            hash_xors[p] ^= mixed;
            if (counts[p] == 1) {
              queue.push_back(p);
            }
          }
        }

        if (peel_order.size() == key_count) {
          best_seed = seed;
          success = true;
          break;
        }
      }

      if (!success) {
        BuildBloomFallback(hashes, bits_per_key_, &filter_data);
        goto done;
      }

      std::vector<uint32_t> B(table_size, 0);
      for (size_t reverse = peel_order.size(); reverse > 0; --reverse) {
        const PeelEntry& entry = peel_order[reverse - 1];
        uint32_t pos[3];
        GetPositions(entry.hash, block_length, pos);
        uint32_t val = Fingerprint(entry.hash, fingerprint_bits);
        val ^= B[pos[0]];
        val ^= B[pos[1]];
        val ^= B[pos[2]];
        B[entry.selected_position] = val & BitMask(fingerprint_bits);
      }

      AppendPackedValues(B, fingerprint_bits, &filter_data);
      PutFixed32(&filter_data, best_seed);
      PutFixed32(&filter_data, block_length);
      filter_data.push_back(static_cast<char>(fingerprint_bits));
      // RocksDB 5-byte metadata trailer:
      filter_data.push_back(static_cast<char>(kRocksDbMarkerXor));
      filter_data.push_back(static_cast<char>(0));
      filter_data.push_back(static_cast<char>(0));
      filter_data.push_back(static_cast<char>(0));
      filter_data.push_back(static_cast<char>(0));
    }

done:
    char* raw_buf = new char[filter_data.size()];
    memcpy(raw_buf, filter_data.data(), filter_data.size());
    buf->reset(raw_buf);
    return Slice(raw_buf, filter_data.size());
  }
};

class XorFilterBitsReader : public BuiltinFilterBitsReader {
 private:
  Slice filter_;
  uint32_t seed_;
  uint32_t block_length_;
  uint8_t fingerprint_bits_;
  size_t payload_size_;

 public:
  XorFilterBitsReader(const Slice& contents) : filter_(contents) {
    if (contents.size() < kTrailerSize) {
      payload_size_ = 0;
      return;
    }
    const char* trailer = contents.data() + contents.size() - kTrailerSize;
    seed_ = DecodeFixed32(trailer);
    block_length_ = DecodeFixed32(trailer + 4);
    fingerprint_bits_ = static_cast<uint8_t>(trailer[8]);

    payload_size_ = contents.size() - kTrailerSize;
  }

  using FilterBitsReader::MayMatch;

  bool MayMatch(const Slice& entry) override {
    return HashMayMatch(GetSliceHash64(entry));
  }

  bool HashMayMatch(const uint64_t h) override {
    if (payload_size_ == 0) return true;

    if (block_length_ == 0) {
      // Bloom fallback
      uint32_t bit_count = seed_;
      uint32_t probes = fingerprint_bits_;
      if (bit_count == 0 || probes == 0) return true;
      uint32_t h32 = static_cast<uint32_t>(h);
      const uint32_t delta = (h32 >> 17) | (h32 << 15);
      const char* array = filter_.data();
      for (uint32_t j = 0; j < probes; j++) {
        const uint32_t bit_pos = h32 % bit_count;
        if ((static_cast<uint8_t>(array[bit_pos / 8]) & (1u << (bit_pos % 8))) == 0) {
          return false;
        }
        h32 += delta;
      }
      return true;
    }

    uint64_t mixed = MixHash(h, seed_);
    uint32_t pos[3];
    GetPositions(mixed, block_length_, pos);
    int fingerprint_bits = fingerprint_bits_;
    uint32_t v0 = 0, v1 = 0, v2 = 0;
    if (!ReadPackedValue(filter_.data(), payload_size_, pos[0], fingerprint_bits, &v0) ||
        !ReadPackedValue(filter_.data(), payload_size_, pos[1], fingerprint_bits, &v1) ||
        !ReadPackedValue(filter_.data(), payload_size_, pos[2], fingerprint_bits, &v2)) {
      return true;
    }
    uint32_t fp = Fingerprint(mixed, fingerprint_bits);
    return fp == (v0 ^ v1 ^ v2);
  }
};

}  // anonymous namespace

XorFilterPolicy::XorFilterPolicy(double bloom_equivalent_bits_per_key, int bloom_before_level)
    : BloomLikeFilterPolicy(bloom_equivalent_bits_per_key),
      bloom_before_level_(bloom_before_level) {}

FilterBitsBuilder* XorFilterPolicy::GetBuilderWithContext(const FilterBuildingContext&) const {
  return new XorFilterBitsBuilder(static_cast<int>(GetMillibitsPerKey() / 1000.0 + 0.5));
}

const char* XorFilterPolicy::kClassName() { return "xorfilter"; }
const char* XorFilterPolicy::kNickName() { return "rocksdb.XorFilter"; }

std::string XorFilterPolicy::GetId() const {
  return BloomLikeFilterPolicy::GetId() + ":" +
         std::to_string(bloom_before_level_);
}

const FilterPolicy* NewXorFilterPolicy(double bloom_equivalent_bits_per_key, int bloom_before_level) {
  return new XorFilterPolicy(bloom_equivalent_bits_per_key, bloom_before_level);
}

BuiltinFilterBitsReader* GetXorFilterBitsReader(const Slice& contents) {
  return new XorFilterBitsReader(contents);
}

}  // namespace ROCKSDB_NAMESPACE
