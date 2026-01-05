#pragma once

#include <argparse/argparse.hpp>
#include <parlay/slice.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <string>
#include <shared_mutex>
#include "fcntl.h"
#include "file.hpp"
#include "operation_def.hpp"
#include "timer.hpp"
#include "oracle.hpp"
#include "operation.hpp"
#include "parlay/papi/papi_util_impl.h"
#include <unistd.h>
#include <sys/stat.h>
#include <filesystem>
#include "index_interface.hpp"

template <typename IndexType>
class Driver {
   public:
    void InitParser(argparse::ArgumentParser& cli_parser) {
        cli_parser.add_argument("--file", "-f")
            .help("--file [init_file] [test_file]")
            .required()
            .nargs(2);
        cli_parser.add_argument("--length", "-l")
            .help("-l [init_length] [test_length]")
            .nargs(2)
            .required()
            .scan<'i', int>();
        cli_parser.add_argument("--nocheck", "-c")
            .help(
                "stop checking the correctness of the tested data "
                "structure")
            .default_value(false)
            .implicit_value(true);
        cli_parser.add_argument("--noprint", "-t")
            .help("don't print timer name when timing")
            .default_value(false)
            .implicit_value(true);
        cli_parser.add_argument("--nodetail", "-d")
            .help("don't show detail")
            .default_value(false)
            .implicit_value(true);
        cli_parser.add_argument("--init_batch_size")
            .help("--init_batch_size [init batch size]")
            .default_value(1000000)
            .scan<'i', int>();
        cli_parser.add_argument("--test_batch_size")
            .help("--test_batch_size [test batch size]")
            .default_value(1000000)
            .scan<'i', int>();
        cli_parser.add_argument("--top_level_threads")
            .help("--top_level_threads [#threads]")
            .default_value(1)
            .scan<'i', int>();
        cli_parser.add_argument("--push_pull_limit_dynamic")
            .help(
                "--push_pull_limit_dynamic [limit] (limit for pull/push, limit "
                "* 2 for shadow push)")
            .default_value(L2_SIZE)
            .scan<'i', int>();
    }

    void dpu_energy_stats(bool flag = false) {
        #ifdef DPU_ENERGY
            uint64_t db_iter=0, op_iter=0, cycle_iter=0, instr_iter=0;
            uint64_t op_total = 0, db_size_total = 0, cycle_total = 0;
            DPU_FOREACH(dpu_set, dpu, each_dpu) {
                DPU_ASSERT(dpu_copy_from(dpu, "op_count", 0, &op_iter, sizeof(uint64_t)));
                DPU_ASSERT(dpu_copy_from(dpu, "db_size_count", 0, &db_iter, sizeof(uint64_t)));
                DPU_ASSERT(dpu_copy_from(dpu, "cycle_count", 0, &cycle_iter, sizeof(uint64_t)));
                op_total += op_iter;
                db_size_total += db_iter;
                cycle_total += cycle_iter;
                if(flag) {
                    cout<<"DPU ID: "<<each_dpu<<" "
                        <<op_iter<<" "<<db_iter<<" ";
                    cout<<((op_iter > 0) ? (db_iter / op_iter) : 0)
                        <<" "<<cycle_iter<<endl;
                }
            }
            cout<<"op_total: "<<op_total<<endl;
            cout<<"db_total: "<<db_size_total<<endl;
            cout<<"cy_total: "<<cycle_total<<endl;
        #endif
    }

    void ExecuteGetBatch(union_operation* ops, int size, int tid) {
        auto get_key = [&](size_t i) { return ops[i].tsk.g.key; };
        IndexType* index = indices_[tid];
        auto [index_results, result_size] = index->RunBatchGet(size, get_key);
        assert(result_size == size);
        if (check_result_) {
            std::vector<key_value> oracle_results = oracle.RunBatchGet(size, get_key);
            std::vector<bool> correct(size, true);
            bool all_correct = true;
            parlay::parallel_for(0, size, [&](size_t i) {
                if (index_results[i] != oracle_results[i]) {
                    correct[i] = false;
                    all_correct = false;
                }
            });
            if (!all_correct) {
                for (size_t i = 0; i < size; i ++) {
                    if (!correct[i]) {
                        std::cout << "Error at " << i << " " << ops[i].tsk.g.key << std::endl;
                        std::cout << "Index: " << index_results[i] << std::endl;
                        std::cout << "Oracle: " << oracle_results[i] << std::endl;
                    }
                }
                assert(false);
            }
        }
    }

    void ExecuteUpdateBatch(union_operation* ops, int size, int tid) {
        assert(false && "Pure update not supported");
    }

    void ExecutePredecessorBatch(union_operation* ops, int size, int tid) {
        auto get_key = [&](size_t i) { return ops[i].tsk.p.key; };
        IndexType* index = indices_[tid];
        auto [index_results, result_size] = index->RunBatchPredecessor(size, get_key);
        if (check_result_) {
            std::vector<key_value> oracle_results = oracle.RunBatchPredecessor(size, get_key);
            std::vector<bool> correct(size, true);
            bool all_correct = true;
            parlay::parallel_for(0, size, [&](size_t i) {
                if (index_results[i] != oracle_results[i]) {
                    correct[i] = false;
                    all_correct = false;
                }
            });
            if (!all_correct) {
                for (size_t i = 0; i < size; i ++) {
                    if (!correct[i]) {
                        std::cout << "Error at " << i << " " << ops[i].tsk.g.key << std::endl;
                        std::cout << "Index: " << index_results[i] << std::endl;
                        std::cout << "Oracle: " << oracle_results[i] << std::endl;
                    }
                }
                assert(false);
            }
        }
    }

    void ExecuteInsertBatch(union_operation* ops, int size, int tid) {
        auto get_KV = [&](size_t i) {
            key_value kv;
            kv.key = ops[i].tsk.i.key;
            kv.value = ops[i].tsk.i.value;
            return kv;
        };
        IndexType* index = indices_[tid];
        index->RunBatchInsert(size, get_KV);
        if (check_result_) {
            oracle.RunBatchInsert(size, get_KV);
        }
    }

    void ExecuteRemoveBatch(union_operation* ops, int size, int tid) {
        auto get_key = [&](size_t i) { return ops[i].tsk.r.key; };
        IndexType* index = indices_[tid];
        index->RunBatchRemove(size, get_key);
        if (check_result_) {
            oracle.RunBatchRemove(size, get_key);
        }
    }

    void ExecuteScanBatch(union_operation* ops, int size, int tid) {
        assert(false && "todo");
        // auto get_LRKey = [&](size_t i) { return std::make_pair(ops[i].tsk.s.lkey, ops[i].tsk.s.rkey); };
        // IndexType* index = indices_[tid];
        // auto [index_results, result_size] = index->RunBatchScan(size, get_LRKey);
        // if (check_result_) {
        //     std::vector<std::pair<int64_t, int64_t>> oracle_results = oracle.RunBatchScan(size, get_LRKey);
        //     std::vector<bool> correct(size, true);
        //     parlay::parallel_for(0, size, [&](size_t i) {
        //         if (index_results[i] != oracle_results[i]) {
        //             correct[i] = false;
        //         }
        //     });
        //     for (size_t i = 0; i < size; i ++) {
        //         if (!correct[i]) {
        //             std::cout << "Error at " << i << " " << ops[i].tsk.s.lkey << " " << ops[i].tsk.s.rkey << std::endl;
        //             std::cout << "Index: " << index_results[i].first << " " << index_results[i].second << std::endl;
        //             std::cout << "Oracle: " << oracle_results[i].first << " " << oracle_results[i].second << std::endl;
        //         }
        //     }
        // }
    }

    void ExecuteOperations(std::vector<union_operation>& ops, size_t batch_size, size_t num_threads, std::string caller_name) {
        std::cout << "Execute n=" << ops.size() << " batchsize=" << batch_size << std::endl;
        std::cout << "num_threads=" << num_threads << std::endl;

        assert(num_threads <= num_top_level_threads_);
        // memset(op_count, 0, sizeof(op_count));
        // init(ops, load_batch_size, execute_batch_size);

        time_nested_pass(caller_name + " global execution", [&](timer* timer) {
            std::atomic<size_t> next_batch_start_idx = 0;
            parlay::parallel_for(
                0, num_threads,
                [&](size_t tid) {
                    time_nested<true>("execution thread " + std::to_string(tid), [&]() {
                        cpu_coverage_timer->start();
                        pim_coverage_timer->start();
                        pim_coverage_timer->end();
                        while (true) {
                            size_t batch_start_idx =
                                next_batch_start_idx.fetch_add(batch_size);
                            if (batch_start_idx >= ops.size()) {
                                break;
                            }
                            printf("\n\n ******************** Start thread %llu / %llu, starting at task %llu of type %llu ******************** \n\n", tid,
                                num_threads, next_batch_start_idx.load(), ops[batch_start_idx].type);
                            size_t batch_end_idx =
                                min(batch_start_idx + batch_size, ops.size());
                            size_t batch_len = batch_end_idx - batch_start_idx;

                            union_operation* batch_ops = &ops[batch_start_idx];

#ifdef KHB_CPU_DEBUG
                            parlay::parallel_for(0, batch_len, [&](size_t i) {
                                assert(batch_ops[i].type == batch_ops[0].type);
                            });
#endif

                            switch(batch_ops[0].type) {
                                case operation_t::get_t: {
                                    ExecuteGetBatch(batch_ops, batch_len, tid);
                                    break;
                                }
                                case operation_t::update_t: {
                                    ExecuteUpdateBatch(batch_ops, batch_len, tid);
                                    break;
                                }
                                case operation_t::predecessor_t: {
                                    ExecutePredecessorBatch(batch_ops, batch_len, tid);
                                    break;
                                }
                                case operation_t::insert_t: {
                                    ExecuteInsertBatch(batch_ops, batch_len, tid);
                                    break;
                                }
                                case operation_t::remove_t: {
                                    ExecuteRemoveBatch(batch_ops, batch_len, tid);
                                    break;
                                }
                                case operation_t::scan_t: {
                                    ExecuteScanBatch(batch_ops, batch_len, tid);
                                    break;
                                }
                                default: {
                                    assert(false && "Unknown operation type");
                                    break;
                                }
                            }
                        }
                        cpu_coverage_timer->end();
                        pim_coverage_timer->start();
                        pim_coverage_timer->end();
                    }, timer);
                },
                1);
        });
        printf("global execution finish!\n");
        fflush(stdout);
        std::cout << "Oracle size: " << oracle.Size() << std::endl;
    }

    void Init(std::vector<union_operation>& init_ops,
                     int init_batch_size) {
        IO_Manager::init_io_managers();
        oracle.Init();
        indices_[0]->Init();
        parlay::parallel_for(0, init_ops.size(), [&](size_t i) {
            assert(init_ops[i].type == operation_t::insert_t);
        });
        parlay::sort_inplace(init_ops, [&](const union_operation& a, const union_operation& b) {
            return a.tsk.i.key < b.tsk.i.key;
        });
        ExecuteOperations(init_ops, init_batch_size, 1, "Init");
    }

    void Test(std::vector<union_operation>& test_ops,
                     int test_batch_size) {
        for (int i = 0; i < num_top_level_threads_; i++) {
            indices_[i]->SetPushPullLimit(push_pull_limit_dynamic_);
        }

        dpu_energy_stats(false);

        timer::active = false;
        reset_all_timers();
        timer::active = true;

        total_communication = 0;
        total_actual_communication = 0;

        cpu_coverage_timer->reset();
        pim_coverage_timer->reset();

#ifdef USE_PAPI
        papi_init_program(parlay::num_workers());
        papi_reset_counters();
        papi_turn_counters(true);
        papi_check_counters(parlay::worker_id());
        papi_wait_counters(true, parlay::num_workers());
#endif

        ExecuteOperations(test_ops, test_batch_size, num_top_level_threads_, "Test");

#ifdef USE_PAPI
        papi_turn_counters(false);
        papi_check_counters(parlay::worker_id());
        papi_wait_counters(false, parlay::num_workers());
#endif

        timer::active = false;
        print_all_timers(print_type::pt_full);
        print_all_timers(print_type::pt_name);
        print_all_timers(print_type::pt_time);

        cpu_coverage_timer->print(pt_full);
        pim_coverage_timer->print(pt_full);

#ifdef USE_PAPI
        papi_print_counters(1);
#endif

        dpu_energy_stats(false);
    }

    void exec(int argc, char* argv[]) {
        argparse::ArgumentParser cli_parser;
        InitParser(cli_parser);

        try {
            cli_parser.parse_args(argc, argv);
        } catch (const std::runtime_error& err) {
            std::cerr << err.what() << std::endl;
            std::cerr << cli_parser;
            std::exit(1);
        }

        check_result_ = (cli_parser["--nocheck"] == false);
        timer::print_when_time = (cli_parser["--noprint"] == false);
        timer::default_detail = (cli_parser["--nodetail"] == false);

        push_pull_limit_dynamic_ =
            cli_parser.get<int>("--push_pull_limit_dynamic");
        num_top_level_threads_ =
            cli_parser.get<int>("--top_level_threads");
        assert(num_top_level_threads_ >= 1);
        std::cout << "Thread: " << num_top_level_threads_ << std::endl;
        std::cout << "Push Pull Limit: " << push_pull_limit_dynamic_
                  << std::endl;

        auto files = cli_parser.get<vector<string>>("-f");
        auto file_lengths = cli_parser.get<vector<int>>("-l");
        assert(file_lengths.size() == 2);
        int init_file_length = file_lengths[0];
        int test_file_length = file_lengths[1];

        int init_batch_size = cli_parser.get<int>("--init_batch_size");
        int test_batch_size = cli_parser.get<int>("--test_batch_size");

        assert(files.size() == 2);
        printf("Test from file: [%s] [%s]\n", files[0].c_str(),
               files[1].c_str());

        init_ops = LoadElementsFromBinary<union_operation>(files[0], 0,
                                                           init_file_length);
        test_ops = LoadElementsFromBinary<union_operation>(files[1], 0,
                                                           test_file_length);

        for (size_t i = 0; i < num_top_level_threads_; i++) {
            indices_[i] = new IndexType();
        }

        init_root_timer();
        timer::active = true;

        std::cout << " ********** Start Init ********** " << std::endl;
        Init(init_ops, init_batch_size);
        std::cout << " ********** Start Test ********** " << std::endl;
        Test(test_ops, test_batch_size);

        for (size_t i = 0; i < num_top_level_threads_; i++) {
            delete indices_[i];
        }

        cout << "total communication" << total_communication.load() << endl;
        cout << "total actual communication"
             << total_actual_communication.load() << endl;
    }

    std::vector<union_operation> init_ops, test_ops;

    const static size_t kMaxIndexInterfaces = 5;
    IndexType* indices_[kMaxIndexInterfaces];
    size_t push_pull_limit_dynamic_, num_top_level_threads_;
    bool check_result_;
    Oracle oracle;
};
