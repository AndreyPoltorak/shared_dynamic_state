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
static const int size_buffer = 5;
static std::shared_ptr<State> g_state[size_buffer];
static std::atomic<int> g_index{-1};
//HZ
static unsigned const max_hazard_pointer = 100;

struct HazardPointer 
{
	std::atomic<std::thread::id> id;
	std::atomic<void*> pointer;
};

HazardPointer g_hz_pointers[max_hazard_pointer];

class HPOwner 
{
	HazardPointer* hp;
public:
	HPOwner(const HPOwner& other) = delete;
	HPOwner operator= (const HPOwner& other) = delete;
	HPOwner() :hp(nullptr)
	{
		for (unsigned i = 0; i < max_hazard_pointer; i++)
		{
			std::thread::id old_id;
			if (g_hz_pointers[i].id.compare_exchange_strong(old_id, std::this_thread::get_id()))
			{
				hp = &g_hz_pointers[i];
				break;
			}
		}

		if (!hp)
		{
			abort();
		}
	}

	std::atomic<void*>& get_pointer() 
	{
		return hp->pointer;
	}

	~HPOwner()
	{
		hp->pointer.store(nullptr);
		hp->id.store(std::thread::id());
	}
};

std::atomic<void*>& get_hazard_pointer_for_current_thread()
{
	thread_local static HPOwner hazard;
	return hazard.get_pointer();
}

bool is_active_hz(void* p)
{
	for (unsigned i = 0; i < max_hazard_pointer; i++)
	{
		if (g_hz_pointers[i].pointer.load() == p)
			return true;
	}

	return false;
}

std::pair<size_t,size_t> get_curr_and_next_index()
{
	size_t curr_index = g_index.load(std::memory_order_relaxed);
	while (true)
	{
		for (size_t i = 0; i < size_buffer - 1; i++)
		{
			size_t index = (i + 1) % size_buffer;
			if (!is_active_hz(&g_state[index]))
				return {curr_index,index};
		}

		std::this_thread::yield();
	}
	
}


void writer_thread_func(size_t state_size, std::atomic<bool>& stop_flag) {
    std::mt19937 rng(std::random_device{}());
    std::uniform_int_distribution<size_t> dist(0, state_size - 1);

    while (!stop_flag.load(std::memory_order_relaxed)) {

        if (g_index.load(std::memory_order_acquire) == -1) {
            std::this_thread::yield();
            continue;
        }
        auto [curr_index,next_index] = get_curr_and_next_index();

        auto new_state = std::make_shared<State>(*g_state[curr_index]);
        
        for (int i = 0; i < 10; ++i) {
            size_t idx = dist(rng);
            new_state->data[idx] = static_cast<int>(idx * 2);
        }

        g_state[next_index] = std::move(new_state);

        std::atomic_thread_fence(std::memory_order_release);

        g_index.store(next_index, std::memory_order_relaxed);

        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

static void BM_BufferHZRead(benchmark::State& state) {
    const size_t state_size = 10'000'000;
    const int reads_per_iter = 100;
    
    static std::thread writer_thread;
    static std::atomic<bool> stop_writer{false};

    if (state.thread_index() == 0) {
        g_index.store(-1, std::memory_order_relaxed);

        g_state[0] = std::make_shared<State>(state_size);

        g_index.store(0, std::memory_order_release);

        stop_writer.store(false);
        writer_thread = std::thread(writer_thread_func, state_size, std::ref(stop_writer));
    }


    thread_local std::mt19937 local_rng(std::random_device{}());
    thread_local std::uniform_int_distribution<size_t> index_dist(0, state_size - 1);


    for (auto _ : state) {

        std::atomic<void*>& hp = get_hazard_pointer_for_current_thread();

	    size_t curr_index;
	    do 
	    {
		    curr_index = g_index.load(std::memory_order_relaxed);
		    hp.store(&g_state[curr_index]);
	    } 
	    while (curr_index != g_index.load(std::memory_order_relaxed));

        std::atomic_thread_fence(std::memory_order_acquire);

        int sum = 0;
        for (int i = 0; i < reads_per_iter; ++i) {
            sum += g_state[curr_index]->data[index_dist(local_rng)];
        }
        hp.store(nullptr);
        benchmark::DoNotOptimize(sum);
    }

    

    if (state.thread_index() == 0) {
        stop_writer.store(true);
        if (writer_thread.joinable()) {
            writer_thread.join();
        }
    }
    state.SetItemsProcessed(state.iterations() * reads_per_iter);
}

BENCHMARK(BM_BufferHZRead)->Threads(1)->Iterations(10000000/1)->UseRealTime();
BENCHMARK(BM_BufferHZRead)->Threads(2)->Iterations(10000000/2)->UseRealTime();
BENCHMARK(BM_BufferHZRead)->Threads(4)->Iterations(10000000/4)->UseRealTime();
BENCHMARK(BM_BufferHZRead)->Threads(8)->Iterations(10000000/8)->UseRealTime();
BENCHMARK(BM_BufferHZRead)->Threads(16)->Iterations(10000000/16)->UseRealTime();

BENCHMARK_MAIN();
