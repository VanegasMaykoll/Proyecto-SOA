// Copyright (c) 2026 Proyecto SOA. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Packed three-way Xor Filter implementation for PebblesDB.
//
// Algorithmic basis:
//   "Xor Filters: Faster and Smaller Than Bloom and Cuckoo Filters"
//   Thomas Mueller Graf and Daniel Lemire, 2020.
//
// This implementation is independent from the FastFilter source code. It
// follows the three-location peeling construction described for Xor filters
// and adapts it to PebblesDB's FilterPolicy interface.
//
// Important behavior:
//   * The public precision parameter is Bloom-equivalent bits per key.
//   * Fingerprints are bit-packed instead of restricted to 8 or 16 bits.
//   * Duplicate 64-bit key hashes are removed before construction.
//   * Small/non-beneficial filters use an internal Bloom fallback.
//   * Failed Xor construction retries with new seeds, then falls back to Bloom.
//   * Corrupt or unknown encodings conservatively return "may match".

#include "util/xor_filter.h"

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
// Xor construction parameters
// -----------------------------------------------------------------------------

// Standard Xor filters use a table with roughly 1.23 slots per key.
static const uint32_t kCapacityNumerator = 123;
static const uint32_t kCapacityDenominator = 100;

// Small additive slack improves construction probability for short sets.
static const uint32_t kCapacitySlack = 32;

// Retry peeling with different seeds before using Bloom fallback.
static const uint32_t kMaxSeedTries = 64;

// Explicit 32-bit constant used to produce the second half of a 64-bit hash.
static const uint32_t kSecondHashDelta = 0x7F4A7C15u;

// -----------------------------------------------------------------------------
// Serialized format
// -----------------------------------------------------------------------------
//
// [payload]
// [seed:         4 bytes little-endian]
// [block_length: 4 bytes little-endian]
// [aux:          1 byte]
// [type:         1 byte]
// [version:      1 byte]
// [magic:        1 byte]
//
// For Xor:
//   block_length = length of each of the three table blocks
//   aux          = fingerprint bits
//
// For Bloom fallback:
//   block_length = total Bloom bits
//   aux          = number of probes

static const size_t kTrailerSize = 12;
static const uint8_t kFormatMagic = 0xA9;
static const uint8_t kFormatVersion = 1;

enum FilterEncodingType {
  kEncodingEmpty = 0,
  kEncodingXor = 1,
  kEncodingBloom = 2,
  kEncodingAlwaysMatch = 3
};

// -----------------------------------------------------------------------------
// General helpers
// -----------------------------------------------------------------------------

static void AppendFixed32(std::string* dst, uint32_t value) {
  dst->push_back(static_cast<char>(value & 0xffu));
  dst->push_back(static_cast<char>((value >> 8) & 0xffu));
  dst->push_back(static_cast<char>((value >> 16) & 0xffu));
  dst->push_back(static_cast<char>((value >> 24) & 0xffu));
}

static uint32_t DecodeFixed32(const char* data) {
  return static_cast<uint32_t>(
             static_cast<uint8_t>(data[0])) |
         (static_cast<uint32_t>(
              static_cast<uint8_t>(data[1])) << 8) |
         (static_cast<uint32_t>(
              static_cast<uint8_t>(data[2])) << 16) |
         (static_cast<uint32_t>(
              static_cast<uint8_t>(data[3])) << 24);
}

static void AppendTrailer(std::string* dst,
                          uint32_t seed,
                          uint32_t length_or_bits,
                          uint8_t aux,
                          uint8_t type) {
  AppendFixed32(dst, seed);
  AppendFixed32(dst, length_or_bits);
  dst->push_back(static_cast<char>(aux));
  dst->push_back(static_cast<char>(type));
  dst->push_back(static_cast<char>(kFormatVersion));
  dst->push_back(static_cast<char>(kFormatMagic));
}

static uint32_t BitMask(int bits) {
  assert(bits >= 1 && bits <= 16);
  return (1u << bits) - 1u;
}

static uint64_t RotateLeft64(uint64_t value, int distance) {
  return (value << distance) |
         (value >> (64 - distance));
}

static uint32_t FastRange32(uint32_t hash, uint32_t range) {
  if (range == 0) {
    return 0;
  }

  return static_cast<uint32_t>(
      (static_cast<uint64_t>(hash) *
       static_cast<uint64_t>(range)) >> 32);
}

static size_t PackedByteSize(uint32_t value_count,
                             int bits_per_value) {
  const uint64_t total_bits =
      static_cast<uint64_t>(value_count) *
      static_cast<uint64_t>(bits_per_value);
  return static_cast<size_t>((total_bits + 7u) / 8u);
}

static void AppendPackedValues(
    const std::vector<uint32_t>& values,
    int bits_per_value,
    std::string* dst) {
  const uint32_t mask = BitMask(bits_per_value);
  uint64_t pending = 0;
  int pending_bits = 0;

  for (size_t i = 0; i < values.size(); ++i) {
    pending |=
        static_cast<uint64_t>(values[i] & mask)
        << pending_bits;
    pending_bits += bits_per_value;

    while (pending_bits >= 8) {
      dst->push_back(
          static_cast<char>(pending & 0xffu));
      pending >>= 8;
      pending_bits -= 8;
    }
  }

  if (pending_bits > 0) {
    dst->push_back(
        static_cast<char>(pending & 0xffu));
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
  const size_t byte_offset =
      static_cast<size_t>(bit_offset / 8u);
  const int shift =
      static_cast<int>(bit_offset % 8u);
  const int bytes_needed =
      (shift + bits_per_value + 7) / 8;

  if (byte_offset +
          static_cast<size_t>(bytes_needed) >
      payload_size) {
    return false;
  }

  uint64_t word = 0;
  for (int i = 0; i < bytes_needed; ++i) {
    word |=
        static_cast<uint64_t>(
            static_cast<uint8_t>(
                payload[byte_offset + i]))
        << (8 * i);
  }

  *value =
      static_cast<uint32_t>(
          (word >> shift) & BitMask(bits_per_value));
  return true;
}

// -----------------------------------------------------------------------------
// Hash, positions, and fingerprint
// -----------------------------------------------------------------------------

static uint64_t XorHash64(const char* data,
                          size_t size,
                          uint32_t seed) {
  const uint32_t high =
      Hash(data, size, seed);
  const uint32_t low =
      Hash(data, size, seed + kSecondHashDelta);

  // LevelDB provides a 32-bit hash. Combine two independently seeded hashes
  // and apply the MurmurHash3 64-bit finalizer used by the Xor-filter paper.
  // The finalizer is important for sequential and otherwise structured keys.
  uint64_t hash =
      (static_cast<uint64_t>(high) << 32) | low;
  hash ^= hash >> 33;
  hash *= 0xff51afd7ed558ccdULL;
  hash ^= hash >> 33;
  hash *= 0xc4ceb9fe1a85ec53ULL;
  hash ^= hash >> 33;
  return hash;
}

static uint32_t Position0(uint64_t hash,
                          uint32_t block_length) {
  return FastRange32(
      static_cast<uint32_t>(hash),
      block_length);
}

static uint32_t Position1(uint64_t hash,
                          uint32_t block_length) {
  return block_length +
         FastRange32(
             static_cast<uint32_t>(
                 RotateLeft64(hash, 21)),
             block_length);
}

static uint32_t Position2(uint64_t hash,
                          uint32_t block_length) {
  return 2u * block_length +
         FastRange32(
             static_cast<uint32_t>(
                 RotateLeft64(hash, 42)),
             block_length);
}

static void GetPositions(uint64_t hash,
                         uint32_t block_length,
                         uint32_t positions[3]) {
  positions[0] = Position0(hash, block_length);
  positions[1] = Position1(hash, block_length);
  positions[2] = Position2(hash, block_length);
}

static uint32_t Fingerprint(uint64_t hash,
                            int fingerprint_bits) {
  uint64_t mixed = hash;
  mixed ^= mixed >> 32;
  mixed ^= mixed >> 16;
  return static_cast<uint32_t>(mixed) &
         BitMask(fingerprint_bits);
}

static void BuildUniqueHashes(const Slice* keys,
                              int key_count,
                              uint32_t seed,
                              std::vector<uint64_t>* hashes) {
  hashes->clear();
  hashes->reserve(
      static_cast<size_t>(key_count));

  for (int i = 0; i < key_count; ++i) {
    hashes->push_back(
        XorHash64(
            keys[i].data(),
            keys[i].size(),
            seed));
  }

  std::sort(hashes->begin(), hashes->end());
  hashes->erase(
      std::unique(hashes->begin(), hashes->end()),
      hashes->end());
}

// -----------------------------------------------------------------------------
// Internal Bloom fallback
// -----------------------------------------------------------------------------

static uint32_t BloomProbeCount(int bits_per_key) {
  uint32_t probes =
      static_cast<uint32_t>(bits_per_key * 0.69);

  if (probes < 1) {
    probes = 1;
  }
  if (probes > 30) {
    probes = 30;
  }

  return probes;
}

static uint32_t BloomBitCount(size_t key_count,
                              int bits_per_key) {
  uint64_t bits =
      static_cast<uint64_t>(key_count) *
      static_cast<uint64_t>(bits_per_key);

  if (bits < 64) {
    bits = 64;
  }

  bits =
      (bits + 7u) &
      ~static_cast<uint64_t>(7u);

  if (bits > 0xffffffffu) {
    return 0;
  }

  return static_cast<uint32_t>(bits);
}

static size_t EstimatedBloomSerializedSize(
    size_t key_count,
    int bits_per_key) {
  const uint32_t bit_count =
      BloomBitCount(key_count, bits_per_key);

  if (bit_count == 0) {
    return static_cast<size_t>(-1);
  }

  return static_cast<size_t>(bit_count / 8u) +
         kTrailerSize;
}

static void BuildBloomEncoding(
    const std::vector<uint64_t>& hashes,
    int bits_per_key,
    uint32_t seed,
    std::string* encoded) {
  const uint32_t bit_count =
      BloomBitCount(hashes.size(), bits_per_key);

  if (bit_count == 0) {
    AppendTrailer(
        encoded, seed, 0, 0, kEncodingAlwaysMatch);
    return;
  }

  const uint32_t probes =
      BloomProbeCount(bits_per_key);
  const size_t byte_count =
      bit_count / 8u;
  const size_t payload_offset =
      encoded->size();

  encoded->append(
      byte_count,
      static_cast<char>(0));

  for (size_t i = 0; i < hashes.size(); ++i) {
    uint32_t hash =
        static_cast<uint32_t>(
            hashes[i] ^ (hashes[i] >> 32));
    const uint32_t delta =
        (hash >> 17) | (hash << 15);

    for (uint32_t probe = 0;
         probe < probes;
         ++probe) {
      const uint32_t bit_position =
          hash % bit_count;

      (*encoded)[
          payload_offset + bit_position / 8u] |=
          static_cast<char>(
              1u << (bit_position % 8u));

      hash += delta;
    }
  }

  AppendTrailer(
      encoded,
      seed,
      bit_count,
      static_cast<uint8_t>(probes),
      kEncodingBloom);
}

static bool BloomMayMatch(const Slice& key,
                          const char* payload,
                          size_t payload_size,
                          uint32_t seed,
                          uint32_t bit_count,
                          uint8_t probes) {
  if (bit_count < 64 ||
      probes < 1 ||
      probes > 30 ||
      payload_size !=
          static_cast<size_t>(
              (bit_count + 7u) / 8u)) {
    return true;
  }

  const uint64_t full_hash =
      XorHash64(
          key.data(),
          key.size(),
          seed);
  uint32_t hash =
      static_cast<uint32_t>(
          full_hash ^ (full_hash >> 32));
  const uint32_t delta =
      (hash >> 17) | (hash << 15);

  for (uint32_t probe = 0;
       probe < probes;
       ++probe) {
    const uint32_t bit_position =
        hash % bit_count;

    if ((static_cast<uint8_t>(
             payload[bit_position / 8u]) &
         (1u << (bit_position % 8u))) == 0) {
      return false;
    }

    hash += delta;
  }

  return true;
}

// -----------------------------------------------------------------------------
// Xor construction
// -----------------------------------------------------------------------------

static uint32_t ComputeBlockLength(size_t key_count) {
  if (key_count == 0) {
    return 0;
  }

  const uint64_t scaled =
      (static_cast<uint64_t>(key_count) *
           kCapacityNumerator +
       kCapacityDenominator - 1u) /
      kCapacityDenominator;

  const uint64_t capacity =
      scaled + kCapacitySlack;
  const uint64_t block_length =
      (capacity + 2u) / 3u;

  if (block_length == 0 ||
      block_length > 0xffffffffu / 3u) {
    return 0;
  }

  return static_cast<uint32_t>(block_length);
}

struct PeelEntry {
  uint64_t hash;
  uint32_t selected_position;
};

static bool TryBuildXor(
    const std::vector<uint64_t>& hashes,
    uint32_t block_length,
    int fingerprint_bits,
    std::string* encoded,
    uint32_t seed) {
  if (hashes.empty() || block_length == 0) {
    return false;
  }

  const uint32_t table_length =
      3u * block_length;

  std::vector<uint32_t> counts(
      table_length, 0);
  std::vector<uint64_t> hash_xors(
      table_length, 0);

  for (size_t i = 0; i < hashes.size(); ++i) {
    uint32_t positions[3];
    GetPositions(
        hashes[i],
        block_length,
        positions);

    for (int j = 0; j < 3; ++j) {
      ++counts[positions[j]];
      hash_xors[positions[j]] ^= hashes[i];
    }
  }

  std::vector<uint32_t> queue;
  queue.reserve(table_length);

  for (uint32_t position = 0;
       position < table_length;
       ++position) {
    if (counts[position] == 1) {
      queue.push_back(position);
    }
  }

  std::vector<PeelEntry> peel_order;
  peel_order.reserve(hashes.size());

  size_t queue_index = 0;
  while (queue_index < queue.size()) {
    const uint32_t selected_position =
        queue[queue_index++];

    if (counts[selected_position] != 1) {
      continue;
    }

    const uint64_t hash =
        hash_xors[selected_position];

    PeelEntry entry;
    entry.hash = hash;
    entry.selected_position = selected_position;
    peel_order.push_back(entry);

    uint32_t positions[3];
    GetPositions(hash, block_length, positions);

    for (int j = 0; j < 3; ++j) {
      const uint32_t position = positions[j];

      if (counts[position] == 0) {
        continue;
      }

      --counts[position];
      hash_xors[position] ^= hash;

      if (counts[position] == 1) {
        queue.push_back(position);
      }
    }
  }

  if (peel_order.size() != hashes.size()) {
    return false;
  }

  std::vector<uint32_t> fingerprints(
      table_length, 0);

  for (size_t reverse = peel_order.size();
       reverse > 0;
       --reverse) {
    const PeelEntry& entry =
        peel_order[reverse - 1];

    uint32_t positions[3];
    GetPositions(
        entry.hash,
        block_length,
        positions);

    uint32_t value =
        Fingerprint(
            entry.hash,
            fingerprint_bits);

    value ^= fingerprints[positions[0]];
    value ^= fingerprints[positions[1]];
    value ^= fingerprints[positions[2]];

    fingerprints[entry.selected_position] =
        value & BitMask(fingerprint_bits);
  }

  AppendPackedValues(
      fingerprints,
      fingerprint_bits,
      encoded);

  AppendTrailer(
      encoded,
      seed,
      block_length,
      static_cast<uint8_t>(fingerprint_bits),
      kEncodingXor);

  return true;
}

// -----------------------------------------------------------------------------
// FilterPolicy
// -----------------------------------------------------------------------------

class XorFilterPolicy : public FilterPolicy {
 public:
  explicit XorFilterPolicy(
      int bloom_equivalent_bits_per_key)
      : bits_per_key_(
            bloom_equivalent_bits_per_key),
        fingerprint_bits_(
            ComputeFingerprintBits(
                bloom_equivalent_bits_per_key)) {
  }

  const char* Name() const {
    // Change the name whenever the serialized representation changes.
    return "proyecto-soa.XorFilter3Packed.v1";
  }

  void CreateFilter(const Slice* keys,
                    int key_count,
                    std::string* dst) const {
    if (key_count <= 0) {
      std::string encoded;
      AppendTrailer(
          &encoded, 0, 0, 0, kEncodingEmpty);
      dst->append(encoded);
      return;
    }

    // Seed zero is also used to estimate unique count and Bloom size.
    std::vector<uint64_t> initial_hashes;
    BuildUniqueHashes(
        keys,
        key_count,
        0,
        &initial_hashes);

    if (initial_hashes.empty()) {
      std::string encoded;
      AppendTrailer(
          &encoded, 0, 0, 0, kEncodingEmpty);
      dst->append(encoded);
      return;
    }

    const uint32_t initial_block_length =
        ComputeBlockLength(
            initial_hashes.size());

    if (initial_block_length == 0) {
      std::string encoded;
      BuildBloomEncoding(
          initial_hashes,
          bits_per_key_,
          0,
          &encoded);
      dst->append(encoded);
      return;
    }

    const uint32_t initial_table_length =
        3u * initial_block_length;
    const size_t xor_size =
        PackedByteSize(
            initial_table_length,
            fingerprint_bits_) +
        kTrailerSize;
    const size_t bloom_size =
        EstimatedBloomSerializedSize(
            initial_hashes.size(),
            bits_per_key_);

    // The additive construction slack makes Xor unattractive for small sets.
    if (xor_size >= bloom_size) {
      std::string encoded;
      BuildBloomEncoding(
          initial_hashes,
          bits_per_key_,
          0,
          &encoded);
      dst->append(encoded);
      return;
    }

    for (uint32_t seed = 0;
         seed < kMaxSeedTries;
         ++seed) {
      std::vector<uint64_t> hashes;
      BuildUniqueHashes(
          keys,
          key_count,
          seed,
          &hashes);

      const uint32_t block_length =
          ComputeBlockLength(hashes.size());

      if (block_length == 0) {
        continue;
      }

      std::string encoded;
      if (TryBuildXor(
              hashes,
              block_length,
              fingerprint_bits_,
              &encoded,
              seed)) {
        dst->append(encoded);
        return;
      }
    }

    std::string encoded;
    BuildBloomEncoding(
        initial_hashes,
        bits_per_key_,
        0,
        &encoded);
    dst->append(encoded);
  }

  bool KeyMayMatch(const Slice& key,
                   const Slice& filter) const {
    if (filter.size() < kTrailerSize) {
      return true;
    }

    const char* data = filter.data();
    const size_t trailer_offset =
        filter.size() - kTrailerSize;
    const char* trailer =
        data + trailer_offset;

    const uint32_t seed =
        DecodeFixed32(trailer);
    const uint32_t length_or_bits =
        DecodeFixed32(trailer + 4);
    const uint8_t aux =
        static_cast<uint8_t>(trailer[8]);
    const uint8_t type =
        static_cast<uint8_t>(trailer[9]);
    const uint8_t version =
        static_cast<uint8_t>(trailer[10]);
    const uint8_t magic =
        static_cast<uint8_t>(trailer[11]);

    if (magic != kFormatMagic ||
        version != kFormatVersion) {
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
      return BloomMayMatch(
          key,
          payload,
          payload_size,
          seed,
          length_or_bits,
          aux);
    }

    if (type != kEncodingXor) {
      return true;
    }

    const uint32_t block_length =
        length_or_bits;
    const int fingerprint_bits =
        static_cast<int>(aux);

    if (block_length == 0 ||
        block_length > 0xffffffffu / 3u ||
        fingerprint_bits < 1 ||
        fingerprint_bits > 16) {
      return true;
    }

    const uint32_t table_length =
        3u * block_length;

    if (payload_size !=
        PackedByteSize(
            table_length,
            fingerprint_bits)) {
      return true;
    }

    const uint64_t hash =
        XorHash64(
            key.data(),
            key.size(),
            seed);

    uint32_t positions[3];
    GetPositions(
        hash,
        block_length,
        positions);

    uint32_t value0 = 0;
    uint32_t value1 = 0;
    uint32_t value2 = 0;

    if (!ReadPackedValue(
            payload,
            payload_size,
            positions[0],
            fingerprint_bits,
            &value0) ||
        !ReadPackedValue(
            payload,
            payload_size,
            positions[1],
            fingerprint_bits,
            &value1) ||
        !ReadPackedValue(
            payload,
            payload_size,
            positions[2],
            fingerprint_bits,
            &value2)) {
      return true;
    }

    const uint32_t actual =
        value0 ^ value1 ^ value2;
    const uint32_t expected =
        Fingerprint(
            hash,
            fingerprint_bits);

    return (actual & BitMask(fingerprint_bits)) ==
           expected;
  }

 private:
  static int ComputeFingerprintBits(
      int bloom_equivalent_bits_per_key) {
    int bits =
        static_cast<int>(
            std::ceil(
                bloom_equivalent_bits_per_key *
                0.6931471805599453));

    if (bits < 1) {
      bits = 1;
    }
    if (bits > 16) {
      bits = 16;
    }

    return bits;
  }

  const int bits_per_key_;
  const int fingerprint_bits_;
};

}  // namespace

const FilterPolicy* NewXorFilterPolicy(
    int bloom_equivalent_bits_per_key) {
  if (bloom_equivalent_bits_per_key <= 0) {
    return NULL;
  }

  return new XorFilterPolicy(
      bloom_equivalent_bits_per_key);
}

}  // namespace leveldb
