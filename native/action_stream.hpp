#pragma once
#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>
namespace t3 {
struct NoiseAction {i64 size,offset;int buy;};
// One producer prepares independent per-agent draws and the latency stream.
// There is at most one pending noise block per agent and two latency blocks.
class ActionStream {
    struct Buffer {std::vector<NoiseAction> current,pending;size_t position=0;};
    std::vector<Buffer> noise;
    std::deque<int> requests;
    std::deque<std::vector<i64>> latency;
    std::vector<i64> current_latency;size_t latency_position=0;
    std::mutex mutex;std::condition_variable ready;std::atomic<bool> stopping{false};
    std::exception_ptr failure;std::thread thread;
public:
    ActionStream(size_t agents,const std::vector<int>& ids,std::function<NoiseAction(int)> draw_noise,std::function<i64()> draw_latency):noise(agents),requests(ids.begin(),ids.end()) {
        thread=std::thread([this,draw_noise=std::move(draw_noise),draw_latency=std::move(draw_latency)] {
            try {
                while(!stopping.load(std::memory_order_relaxed)) {
                    int agent=-1;
                    {std::unique_lock<std::mutex> lock(mutex);ready.wait(lock,[&]{return stopping.load()||!requests.empty()||(draw_latency&&latency.size()<2);});
                     if(stopping.load()) break;
                     if(!requests.empty()&&(!draw_latency||!latency.empty())) {agent=requests.front();requests.pop_front();}}
                    if(agent>=0) {
                        std::vector<NoiseAction> block;block.reserve(512);
                        for(int i=0;i<512;++i) {if(i%64==0&&stopping.load()) return;block.push_back(draw_noise(agent));}
                        {std::lock_guard<std::mutex> lock(mutex);noise[agent].pending=std::move(block);}ready.notify_all();
                    } else {
                        std::vector<i64> block;block.reserve(2048);
                        for(int i=0;i<2048;++i) {if(i%128==0&&stopping.load()) return;block.push_back(draw_latency());}
                        {std::lock_guard<std::mutex> lock(mutex);latency.push_back(std::move(block));}ready.notify_all();
                    }
                }
            } catch(...) {std::lock_guard<std::mutex> lock(mutex);failure=std::current_exception();ready.notify_all();}
        });
    }
    ~ActionStream() {{std::lock_guard<std::mutex> lock(mutex);stopping.store(true);}ready.notify_all();if(thread.joinable()) thread.join();}
    NoiseAction next_noise(int agent) {
        auto& b=noise[agent];
        if(b.position==b.current.size()) {
            std::unique_lock<std::mutex> lock(mutex);ready.wait(lock,[&]{return !b.pending.empty()||failure;});if(failure) std::rethrow_exception(failure);
            b.current=std::move(b.pending);b.pending.clear();b.position=0;requests.push_back(agent);ready.notify_all();
        }
        return b.current[b.position++];
    }
    i64 next_latency() {
        if(latency_position==current_latency.size()) {
            std::unique_lock<std::mutex> lock(mutex);ready.wait(lock,[&]{return !latency.empty()||failure;});if(failure) std::rethrow_exception(failure);
            current_latency=std::move(latency.front());latency.pop_front();latency_position=0;ready.notify_all();
        }
        return current_latency[latency_position++];
    }
};
}
