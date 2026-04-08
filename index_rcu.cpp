#include <atomic>
#include <iostream>
#include <cstddef>
#include <memory>
#include <mutex>
#include <random>
#include <shared_mutex>
#include <thread>
#include <vector>
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
static const int size_vector = 16;
static std::shared_ptr<State> g_slots[size_vector];
static std::atomic<int> g_index{-1};
static std::atomic<uint64_t> g_count{0};



void writer_thread_func(size_t state_size, std::atomic<bool>& stop_flag) {
    std::mt19937 rng(std::random_device{}());
    std::uniform_int_distribution<size_t> dist(0, state_size - 1);

    while (!stop_flag.load(std::memory_order_relaxed)) {

        if (g_index.load(std::memory_order_acquire) == -1) {
            std::this_thread::yield();
            continue;
        }

        auto curr_index = g_index.load(std::memory_order_relaxed);
        auto next_index = (curr_index + 1) % size_vector;

        auto new_state = std::make_shared<State>(*g_slots[curr_index]);
        
        for (int i = 0; i < 10; ++i) {
            size_t idx = dist(rng);
            new_state->data[idx] = static_cast<int>(idx * 2);
        }

        g_slots[next_index] = std::move(new_state);

        std::atomic_thread_fence(std::memory_order_release);

        g_index.store(next_index, std::memory_order_relaxed);

        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

static void BM_IndexRCURead(benchmark::State& state) {
    const size_t state_size = 10'000'000;
    const int reads_per_iter = 100;
    
    static std::thread writer_thread;
    static std::atomic<bool> stop_writer{false};
    std::atomic<uint64_t> count{0};

    if (state.thread_index() == 0) {
        g_index.store(-1, std::memory_order_relaxed);

        g_slots[0] = std::make_shared<State>(state_size);

        g_index.store(0, std::memory_order_release);

        stop_writer.store(false);
        writer_thread = std::thread(writer_thread_func, state_size, std::ref(stop_writer));
    }


    thread_local std::mt19937 local_rng(std::random_device{}());
    thread_local std::uniform_int_distribution<size_t> index_dist(0, state_size - 1);


    for (auto _ : state) {
        auto curr_index = g_index.load(std::memory_order_relaxed);

        std::atomic_thread_fence(std::memory_order_acquire);

        int sum = 0;
        for (int i = 0; i < reads_per_iter; ++i) {
            sum += g_slots[curr_index]->data[index_dist(local_rng)];
        }
        count++;
        g_count++;
        benchmark::DoNotOptimize(sum);
    }

    

    if (state.thread_index() == 0) {
        stop_writer.store(true);
        if (writer_thread.joinable()) {
            writer_thread.join();
        }
    }
    state.counters["allCount"] = count.load();
    state.SetItemsProcessed(state.iterations() * reads_per_iter);
}

BENCHMARK(BM_IndexRCURead)->Threads(1)->Iterations(1000000/1)->UseRealTime();
BENCHMARK(BM_IndexRCURead)->Threads(2)->Iterations(1000000/2)->UseRealTime();
BENCHMARK(BM_IndexRCURead)->Threads(4)->Iterations(1000000/4)->UseRealTime();
BENCHMARK(BM_IndexRCURead)->Threads(8)->Iterations(1000000/8)->UseRealTime();
BENCHMARK(BM_IndexRCURead)->Threads(16)->Iterations(1000000/16)->UseRealTime();

//BENCHMARK_MAIN();
int main(int argc, char** argv) {
    ::benchmark::Initialize(&argc, argv);

    ::benchmark::RunSpecifiedBenchmarks();

    ::benchmark::Shutdown();

    std::cout<< "g_count = " << g_count.load() << std::endl;
    
    return 0;
}