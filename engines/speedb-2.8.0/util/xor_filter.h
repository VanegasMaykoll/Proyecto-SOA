// Copyright (c) 2026 Proyecto SOA. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "rocksdb/filter_policy.h"

namespace ROCKSDB_NAMESPACE {

// Creates a Xor Filter policy that uses approximately the specified number
// of bits per key.
extern const FilterPolicy* NewXorFilterPolicy(double bloom_equivalent_bits_per_key,
                                              int bloom_before_level);

class BuiltinFilterBitsReader;
extern BuiltinFilterBitsReader* GetXorFilterBitsReader(const Slice& contents);

}  // namespace ROCKSDB_NAMESPACE
