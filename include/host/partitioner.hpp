#pragma once

#include <cstdint>
#include <iostream>
#include <cassert>
#include "number_conversion.hpp"

class Partitioner {
public:
    size_t num_partition_;
    uint64_t range_size_;

    Partitioner(size_t num_partition) : num_partition_(num_partition) {
        assert(num_partition_ >= 2);
        range_size_ = UINT64_MAX / num_partition_ + 1;
        assert((num_partition_ - 1) <= (UINT64_MAX / range_size_));
        assert(UINT64_MAX - (range_size_ * (num_partition_ - 1)) + 1 <= range_size_);
    }

    std::pair<uint64_t, uint64_t> KeyRangeForPartitionUI64(size_t partition_id) {
        uint64_t start = range_size_ * partition_id;
        if (UINT64_MAX - start < range_size_) {
            return {start, UINT64_MAX - start + 1};
        } else {
            return {start, range_size_};
        }
    }

    std::pair<int64_t, uint64_t> KeyRangeForPartitionI64(size_t partition_id) {
        auto [start, size] = KeyRangeForPartitionUI64(partition_id);
        return {ConvertI64UI64::UI64ToI64(start), size};
    }

    size_t PartitionForUI64(uint64_t key) {
        return key / range_size_;
    }

    size_t PartitionForI64(int64_t key) {
        return ConvertI64UI64::I64ToUI64(key) / range_size_;
    }
};