#include <atomic>
#include <cstddef>
#include <memory>
#include <mutex>
#include <random>
#include <shared_mutex>
#include <thread>
#include <vector>
#include <iostream>
#include <benchmark/benchmark.h>

struct State {
    std::vector<int> data;

    explicit State(size_t size) : data(size) {
        for (size_t i = 0; i < size; ++i) {
            data[i] = static_cast<int>(i);
        }
    }

    State(const State& other) : data(other.data) {}
    State& operator=(const State&) = delete;
};
// 

static std::atomic<std::shared_ptr<State>> g_state;



void writer_thread_func(size_t state_size, std::atomic<bool>& stop_flag) {
    std::mt19937 rng(std::random_device{}());
    std::uniform_int_distribution<size_t> dist(0, state_size - 1);
    while (!stop_flag.load(std::memory_order_relaxed)) {

        auto old_ptr = g_state.load(std::memory_order_acquire);

        if (!old_ptr) {
            std::this_thread::yield();
            continue;
        }

        auto new_state = std::make_shared<State>(*old_ptr);

        for (int i = 0; i < 10; ++i) {
            size_t idx = dist(rng);
            new_state->data[idx] = static_cast<int>(idx * 2);
        }

        g_state.store(std::move(new_state), std::memory_order_release);
        
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

static void BM_AtomicSharedRead(benchmark::State& state) {
    const size_t state_size = 10'000'000;
    const int reads_per_iter = 100;


    static std::thread writer_thread;
    static std::atomic<bool> stop_writer{false};

    if (state.thread_index() == 0) {
        g_state = std::make_shared<State>(state_size);

        stop_writer.store(false);
        writer_thread = std::thread(writer_thread_func, state_size, std::ref(stop_writer));
    }


    thread_local std::mt19937 local_rng(std::random_device{}());
    thread_local std::uniform_int_distribution<size_t> index_dist(0, state_size - 1);


    for (auto _ : state) {
        std::shared_ptr<State> ptr = g_state.load(std::memory_order_acquire);

        int sum = 0;
        for (int i = 0; i < reads_per_iter; ++i) {
            sum += ptr->data[index_dist(local_rng)];
        }
        benchmark::DoNotOptimize(sum);
    }


    if (state.thread_index() == 0) {
        stop_writer.store(true);
        if (writer_thread.joinable()) {
            writer_thread.join();
        }
        g_state.store(nullptr);
    }
    state.SetItemsProcessed(state.iterations() * reads_per_iter);
}


BENCHMARK(BM_AtomicSharedRead)->Threads(1)->Iterations(10000000/1)->UseRealTime();;
BENCHMARK(BM_AtomicSharedRead)->Threads(2)->Iterations(10000000/2)->UseRealTime();
BENCHMARK(BM_AtomicSharedRead)->Threads(4)->Iterations(10000000/4)->UseRealTime();
BENCHMARK(BM_AtomicSharedRead)->Threads(8)->Iterations(10000000/8)->UseRealTime();
BENCHMARK(BM_AtomicSharedRead)->Threads(16)->Iterations(10000000/16)->UseRealTime();

BENCHMARK_MAIN();