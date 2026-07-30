// Copyright (c) 2026 Proyecto SOA. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Ribbon Filter implementation for LevelDB 1.23.
//
// Based on the algorithm described in:
//   "Ribbon filter: practically smaller than Bloom and Xor"
//   Peter C. Dillinger and Stefan Walzer, 2021
//   arXiv:2103.02515
//
// This implementation follows the reference from FastFilter/fastfilter_cpp
// and adapts it to LevelDB's FilterPolicy interface. It is a standalone
// implementation that does NOT depend on RocksDB.
//
// A Ribbon filter is a static (immutable) probabilistic data structure
// for approximate set membership queries. It offers ~30% space savings
// compared to a standard Bloom filter at the same false positive rate,
// at the cost of higher CPU during construction.
//
// The filter is constructed by solving a band-like linear system over
// GF(2) (Boolean variables) using incremental Gaussian elimination.

#ifndef STORAGE_LEVELDB_UTIL_RIBBON_FILTER_H_
#define STORAGE_LEVELDB_UTIL_RIBBON_FILTER_H_

#include "leveldb/filter_policy.h"

namespace leveldb {

// Return a new filter policy that uses a Ribbon filter with a false positive
// rate approximately equivalent to a Bloom filter with the specified number
// of bits per key.
//
// For example, NewRibbonFilterPolicy(10) creates a Ribbon filter whose
// false positive rate is approximately the same as NewBloomFilterPolicy(10),
// but uses ~30% less space.
//
// bits_per_key: controls the false positive rate equivalence with Bloom.
//   A value of 10 yields approximately 1% false positive rate.
//   Must be > 0.
//
// Callers must delete the result after any database that is using the
// result has been closed.
LEVELDB_EXPORT const FilterPolicy* NewRibbonFilterPolicy(int bits_per_key);

}  // namespace leveldb

#endif  // STORAGE_LEVELDB_UTIL_RIBBON_FILTER_H_
