#include <atomic>
#include <cstddef>
#include <memory>
#include <mutex>
#include <random>
#include <thread>
#include <vector>
#include <array>
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

class EpochManager {
    static constexpr uint64_t INACTIVE = uint64_t(-1);
    std::atomic<uint64_t> global_epoch{0};
    std::array<std::atomic<uint64_t>, 128> thread_epochs;

public:
    EpochManager() {
        for (auto& e : thread_epochs) e.store(INACTIVE);
    }

    void enter(int tid) {
        thread_epochs[tid].store(global_epoch.load(std::memory_order_relaxed), std::memory_order_seq_cst);
    }

    void leave(int tid) {
        thread_epochs[tid].store(INACTIVE, std::memory_order_release);
    }

    void synchronize() {
        uint64_t current = global_epoch.fetch_add(1, std::memory_order_seq_cst);
        
        for (size_t i = 0; i < thread_epochs.size(); ++i) {
            while (true) {
                uint64_t e = thread_epochs[i].load(std::memory_order_acquire);
                if (e == INACTIVE || e > current) break;
                std::this_thread::yield(); 
            }
        }
    }
};

static EpochManager g_epoch_mgr;
static std::atomic<State*> g_state{nullptr};

void writer_thread_func(size_t state_size, std::atomic<bool>& stop_flag) {
    std::mt19937 rng(std::random_device{}());
    std::uniform_int_distribution<size_t> dist(0, state_size - 1);

    while (!stop_flag.load(std::memory_order_relaxed)) {
        
        State* old_state = g_state.load(std::memory_order_acquire);
        if (!old_state) continue;

        State* next_state = new State(*old_state);
        
        for (int i = 0; i < 10; ++i) {
            size_t idx = dist(rng);
            next_state->data[idx] = static_cast<int>(idx * 2);
        }

        g_state.store(next_state, std::memory_order_release);

        g_epoch_mgr.synchronize();
        delete old_state;

        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

static void BM_EpochRCURead(benchmark::State& state) {
    const size_t state_size = 10'000'000;
    const int reads_per_iter = 100;
    const int tid = state.thread_index();
    
    static std::thread writer_thread;
    static std::atomic<bool> stop_writer{false};

    if (tid == 0) {
        g_state.store(new State(state_size));
        stop_writer.store(false);
        writer_thread = std::thread(writer_thread_func, state_size, std::ref(stop_writer));
    }

    thread_local std::mt19937 local_rng(std::random_device{}());
    thread_local std::uniform_int_distribution<size_t> index_dist(0, state_size - 1);

    for (auto _ : state) {
        g_epoch_mgr.enter(tid);
        
        State* ptr = g_state.load(std::memory_order_acquire);
        
        int sum = 0;
        if (ptr) {
            for (int i = 0; i < reads_per_iter; ++i) {
                sum += ptr->data[index_dist(local_rng)];
            }
        }
        benchmark::DoNotOptimize(sum);
        
        g_epoch_mgr.leave(tid);
    }

    state.SetItemsProcessed(state.iterations() * reads_per_iter);

    if (tid == 0) {
        stop_writer.store(true);
        if (writer_thread.joinable()) writer_thread.join();
        delete g_state.exchange(nullptr);
    }
}

BENCHMARK(BM_EpochRCURead)->Threads(1)->UseRealTime();
BENCHMARK(BM_EpochRCURead)->Threads(4)->UseRealTime();
BENCHMARK(BM_EpochRCURead)->Threads(8)->UseRealTime();
BENCHMARK(BM_EpochRCURead)->Threads(16)->UseRealTime();

BENCHMARK_MAIN();