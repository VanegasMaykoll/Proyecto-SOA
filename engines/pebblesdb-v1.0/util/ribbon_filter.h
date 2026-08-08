// Copyright (c) 2026 Proyecto SOA. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Ribbon Filter policy for PebblesDB.
//
// The integer parameter is interpreted as Bloom-equivalent bits per key.
// For example, a value of 10 targets approximately the same false-positive
// probability as a Bloom filter configured with 10 bits per key.

#ifndef STORAGE_PEBBLESDB_UTIL_RIBBON_FILTER_H_
#define STORAGE_PEBBLESDB_UTIL_RIBBON_FILTER_H_

#include "pebblesdb/filter_policy.h"

namespace leveldb {

// Creates a packed Ribbon filter policy.
//
// The implementation uses a 64-bit banded linear system. For filters where
// the packed Ribbon representation would not be smaller, or if Ribbon
// construction fails, the policy serializes an internal Bloom fallback. This
// avoids excessive overhead for very small key sets while preserving zero
// false negatives.
//
// The caller owns the returned policy and must delete it after closing every
// database that uses it.
const FilterPolicy* NewRibbonFilterPolicy(
    int bloom_equivalent_bits_per_key);

}  // namespace leveldb

#endif  // STORAGE_PEBBLESDB_UTIL_RIBBON_FILTER_H_
