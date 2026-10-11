#pragma once
#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>
namespace t3 {
// The configured latency RNG is independent of every agent and the oracle.
// Prepare its exact scalar draw sequence in bounded blocks on one spare core.
// Same-sender messages still bypass this stream, exactly as in the reference.
class LatencyStream {
    std::mutex mutex;std::condition_variable ready;std::deque<std::vector<i64>> pending;
    std::vector<i64> current;size_t position=0;
    std::atomic<bool> stopping{false};std::exception_ptr failure;std::thread thread;
public:
    explicit LatencyStream(std::function<i64()> draw):thread([this,draw=std::move(draw)]() {
        try {
            while(!stopping.load(std::memory_order_relaxed)) {
                {std::unique_lock<std::mutex> lock(mutex);ready.wait(lock,[&]{return stopping.load()||pending.size()<2;});if(stopping.load()) break;}
                std::vector<i64> block;block.reserve(2048);
                for(int i=0;i<2048;++i) {
                    if(i%128==0&&stopping.load(std::memory_order_relaxed)) return;block.push_back(draw());
                }
                {std::lock_guard<std::mutex> lock(mutex);if(stopping.load()) break;pending.push_back(std::move(block));}ready.notify_all();
            }
        } catch(...) {std::lock_guard<std::mutex> lock(mutex);failure=std::current_exception();ready.notify_all();}
    }) {}
    ~LatencyStream() {stop();}
    i64 next() {
        if(position==current.size()) {
            std::unique_lock<std::mutex> lock(mutex);ready.wait(lock,[&]{return !pending.empty()||failure;});if(failure) std::rethrow_exception(failure);
            current=std::move(pending.front());pending.pop_front();position=0;ready.notify_all();
        }
        return current[position++];
    }
    void stop() {if(thread.joinable()) {stopping.store(true);ready.notify_all();thread.join();}}
};
}
