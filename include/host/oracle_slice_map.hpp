#pragma once

#include <parlay/primitives.h>
#include <parlay/range.h>
#include <parlay/sequence.h>
#include <cstring>
#include <vector>
#include <map>
#include <array>
#include "operation_def.hpp"
#include "value.hpp"
#include "partitioner.hpp"


class OracleSliceMap {
    public:
    inline const static size_t kNumSlice = (1ull << 20);
    std::map<int64_t, int64_t>* data;
    // std::array<std::map<int64_t, int64_t>, kNumSlice> data;
    std::array<std::mutex, kNumSlice> mutexes;
    std::unique_ptr<Partitioner> partitioner;

    std::atomic<bool> is_modifying;

    OracleSliceMap(){
        partitioner = std::make_unique<Partitioner>(OracleSliceMap::kNumSlice);
        data = new std::map<int64_t, int64_t>[kNumSlice];
    }

    ~OracleSliceMap() {
        delete[] data;
    }
    
    void Init() {
        for (size_t i = 0; i < kNumSlice; i++) {
            data[i].clear();
        }
        is_modifying = true;
        Insert(key_value(INT64_MIN, INT64_MIN));
        is_modifying = false;
    }

    size_t GetPos(int64_t key) {
        return partitioner->PartitionForI64(key);
    }

    void Insert(key_value kv) {
        assert(is_modifying);
        size_t pos = partitioner->PartitionForI64(kv.key);
        std::lock_guard<std::mutex> lock(mutexes[pos]);
        data[pos][kv.key] = kv.value;
    }

    void Remove(int64_t key) {
        assert(is_modifying);
        size_t pos = partitioner->PartitionForI64(key);
        std::lock_guard<std::mutex> lock(mutexes[pos]);
        data[pos].erase(key);
    }

    key_value Predecessor(int64_t key) {
        assert(!is_modifying);
        size_t pos = partitioner->PartitionForI64(key);
        for (int i = (int)pos; i >= 0; i --) {
            if (data[i].size() > 0) {
                auto itr = data[i].upper_bound(key);
                if (itr != data[i].begin()) {
                    itr--;
                    return (key_value)(*itr);
                }
            }
        }
        assert(false);
    }

    key_value Get(int64_t key) {
        assert(!is_modifying);
        size_t pos = partitioner->PartitionForI64(key);
        auto itr = data[pos].find(key);
        if (itr != data[pos].end()) {
            return (key_value)(*itr);
        } else {
            return key_value(INT64_MIN, INT64_MIN);
        }
    }

    template <typename GetKeyF>
    std::vector<key_value> RunBatchGet(size_t size, GetKeyF get_key) {
        static_assert(
            std::is_same<int64_t, decltype(get_key(0))>::value, "get_key(0) must return int64_t");
        std::vector<key_value> ret(size);
        parlay::parallel_for(0, size, [&](size_t i) {
            ret[i] = Get(get_key(i));
        });
        return ret;
    }

    template<typename GetKeyF>
    std::vector<key_value> RunBatchPredecessor(size_t size, GetKeyF get_key) {
        static_assert(
            std::is_same<int64_t, decltype(get_key(0))>::value, "get_key(0) must return int64_t");

        std::vector<key_value> ret(size);
        parlay::parallel_for(0, size, [&](size_t i) {
            ret[i] = Predecessor(get_key(i));
        });
        return ret;
    }

    // template<typename GetKVF>
    // void RunBatchInsert(size_t size, GetKVF get_kv) {
    //     static_assert(
    //         std::is_same<key_value, decltype(get_kv(0))>::value, "get_kv(0) must return key_value");
    //     std::vector<key_value> kvs(size);
    //     parlay::parallel_for(0, size, [&](size_t i) {
    //         kvs[i] = get_kv(i);
    //     });
    //     parlay::sort_inplace(kvs);
    //     is_modifying = true;
    //     auto is_first_to_pos = parlay::delayed_tabulate(size, [&](size_t i) {
    //         return (i == 0) || (GetPos(kvs[i - 1].key) != GetPos(kvs[i].key));
    //     });
    //     auto start_idx = parlay::pack_index(is_first_to_pos);
    //     std::cout << "start_idx size: " << start_idx.size() << std::endl;
    //     for (size_t i = 0; i < 10; i ++) {
    //         std::cout << "Start_idx[i]=" << start_idx[i] << std::endl;
    //         std::cout << "Pos[start_idx[i]]=" << GetPos(kvs[start_idx[i]].key) << std::endl;
    //     }
    //     // exit(0);
    //     parlay::parallel_for(0, start_idx.size(), [&](size_t i) {
    //         size_t start = start_idx[i];
    //         size_t end = (i + 1 == start_idx.size()) ? size : start_idx[i + 1];
    //         for (size_t j = start; j < end; j++) {
    //             if (j > 0 && kvs[j - 1].key == kvs[j].key) {
    //                 continue;
    //             } else {
    //                 assert(GetPos(kvs[j].key) == GetPos(kvs[start].key));
    //                 Insert(kvs[j]);
    //             }
    //         }
    //     });
    //     is_modifying = false;
    // }

    template <typename GetKVF>
    void RunBatchLoad(size_t size, GetKVF get_kv) {
        static_assert(
            std::is_same<key_value, decltype(get_kv(0))>::value, "get_kv(0) must return key_value");
        is_modifying = true;
        std::vector<key_value> kvs(size);
        parlay::parallel_for(0, size, [&](size_t i) {
            kvs[i] = get_kv(i);
        });
        parlay::sort_inplace(kvs);

        auto is_first_to_pos = parlay::delayed_tabulate(size, [&](size_t i) {
            return (i == 0) || (GetPos(kvs[i - 1].key) != GetPos(kvs[i].key));
        });
        auto start_idx = parlay::pack_index(is_first_to_pos);
        std::cout << "start_idx size: " << start_idx.size() << std::endl;
        for (size_t i = 0; i < 10; i ++) {
            std::cout << "Start_idx[i]=" << start_idx[i] << std::endl;
            std::cout << "Pos[start_idx[i]]=" << GetPos(kvs[start_idx[i]].key) << std::endl;
        }

        std::atomic<size_t> finished = 0;
        parlay::parallel_for(0, start_idx.size(), [&](size_t i) {
            size_t start = start_idx[i];
            size_t end = (i + 1 == start_idx.size()) ? size : start_idx[i + 1];
            size_t pos = i;
            for (size_t j = start; j < end; j ++) {
                if (j > 0) {
                    assert(kvs[j - 1].key < kvs[j].key);
                }
                assert(GetPos(kvs[j].key) == pos);
                data[pos][kvs[j].key] = kvs[j].value;
            }
            if (finished.fetch_add(1) % (start_idx.size() / 100) == 0) {
                std::cout << "Completed: " << (finished.load() * 100 / start_idx.size()) << "%" << std::endl;
            }
        }, 10);
        is_modifying = false;
    }

    template<typename GetKVF>
    void RunBatchInsert(size_t size, GetKVF get_kv) {
        static_assert(
            std::is_same<key_value, decltype(get_kv(0))>::value, "get_kv(0) must return key_value");
        is_modifying = true;
        std::vector<key_value> kvs(size);
        parlay::parallel_for(0, size, [&](size_t i) {
            kvs[i] = get_kv(i);
        });
        parlay::sort_inplace(kvs);

        parlay::parallel_for(0, size, [&](size_t i) {
            if (i > 0 && kvs[i - 1].key == kvs[i].key) {
                return;
            } else {
                Insert(kvs[i]);
            }
        });
        is_modifying = false;
    }

    template<typename GetKeyF>
    void RunBatchRemove(size_t size, GetKeyF get_key) {
        static_assert(
            std::is_same<int64_t, decltype(get_key(0))>::value, "get_key(0) must return int64_t");
        is_modifying = true;
        std::vector<int64_t> keys(size);
        parlay::parallel_for(0, size, [&](size_t i) {
            keys[i] = get_key(i);
        });
        parlay::sort_inplace(keys);
        parlay::parallel_for(0, size, [&](size_t i) {
            Remove(get_key(i));
        });
        is_modifying = false;
    }

    template<typename GetLRKeyF>
    std::vector<std::vector<key_value>> RunBatchScan(size_t size, GetLRKeyF get_lrkey) {
        std::vector<std::vector<key_value>> ret(size);
        assert(false);
        // parlay::parallel_for(0, size, [&](size_t i) {
        //     int64_t lkey, rkey;
        //     std::tie(lkey, rkey) = get_lrkey(i);
        //     auto lpos = data.lower_bound(lkey);
        //     auto rpos = data.upper_bound(rkey);
        //     for (auto itr = lpos; itr != rpos; itr++) {
        //         ret[i].push_back((key_value)(*itr));
        //     }
        // });
        return ret;
    }

    size_t Size() {
        size_t size = 0;
        for (size_t i = 0; i < kNumSlice; i ++) {
            size += data[i].size();
        }
        return size;
    }

    std::vector<key_value> Dump() {
        size_t cnt = 0;
        size_t size = Size();
        std::vector<key_value> ret(size);
        for (size_t i = 0; i < kNumSlice; i ++) {
            for (auto itr = data[i].begin(); itr != data[i].end(); itr++) {
                ret[cnt++] = (key_value)(*itr);
            }
        }
        assert(cnt == size);
        return ret;
    }
};