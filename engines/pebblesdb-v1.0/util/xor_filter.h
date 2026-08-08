// Copyright (c) 2026 Proyecto SOA. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Xor Filter policy for PebblesDB.
//
// The integer parameter is interpreted as Bloom-equivalent bits per key.
// For example, a value of 10 targets approximately the same false-positive
// probability as a Bloom filter configured with 10 bits per key.

#ifndef STORAGE_PEBBLESDB_UTIL_XOR_FILTER_H_
#define STORAGE_PEBBLESDB_UTIL_XOR_FILTER_H_

#include "pebblesdb/filter_policy.h"

namespace leveldb {

// Creates a packed three-way Xor filter policy.
//
// The filter is static: PebblesDB constructs it from the complete key set
// supplied to FilterPolicy::CreateFilter(). For sets where Xor would not be
// smaller, or when peeling fails after all seed attempts, the policy stores
// an internal Bloom fallback.
//
// The caller owns the returned policy and must delete it after closing every
// database that uses it.
const FilterPolicy* NewXorFilterPolicy(
    int bloom_equivalent_bits_per_key);

}  // namespace leveldb

#endif  // STORAGE_PEBBLESDB_UTIL_XOR_FILTER_H_
