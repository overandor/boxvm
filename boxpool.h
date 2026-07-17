// boxpool.h — real work-stealing thread pool
#pragma once
#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>
#include <future>
#include <memory>

class WorkStealingPool {
public:
    explicit WorkStealingPool(size_t n = std::thread::hardware_concurrency()) : stop(false) {
        if (n == 0) n = 4;
        for (size_t i = 0; i < n; i++) queues.push_back(std::make_unique<Queue>());
        for (size_t i = 0; i < n; i++) workers.emplace_back([this, i] { workerLoop(i); });
    }
    ~WorkStealingPool() { shutdown(); }

    template <class F, class R = std::invoke_result_t<F>>
    std::future<R> submit(F&& f) {
        auto task = std::make_shared<std::packaged_task<R()>>(std::forward<F>(f));
        auto fut = task->get_future();
        size_t q = nextQueue++ % queues.size();
        {
            std::lock_guard<std::mutex> lk(queues[q]->mtx);
            queues[q]->tasks.emplace_back([task] { (*task)(); });
        }
        cv.notify_one();
        return fut;
    }

    void shutdown() {
        if (stop.exchange(true)) return;
        cv.notify_all();
        for (auto& t : workers) if (t.joinable()) t.join();
    }

private:
    struct Queue {
        std::deque<std::function<void()>> tasks;
        std::mutex mtx;
    };

    std::vector<std::unique_ptr<Queue>> queues;
    std::vector<std::thread> workers;
    std::condition_variable cv;
    std::mutex cvMtx;
    std::atomic<bool> stop;
    std::atomic<size_t> nextQueue{0};

    bool trySteal(size_t myIdx, std::function<void()>& out) {
        for (size_t i = 1; i < queues.size(); i++) {
            size_t idx = (myIdx + i) % queues.size();
            std::lock_guard<std::mutex> lk(queues[idx]->mtx);
            if (!queues[idx]->tasks.empty()) { out = std::move(queues[idx]->tasks.front()); queues[idx]->tasks.pop_front(); return true; }
        }
        return false;
    }

    void workerLoop(size_t myIdx) {
        while (true) {
            std::function<void()> task;
            {
                std::unique_lock<std::mutex> lk(queues[myIdx]->mtx);
                if (!queues[myIdx]->tasks.empty()) { task = std::move(queues[myIdx]->tasks.front()); queues[myIdx]->tasks.pop_front(); }
            }
            if (!task && trySteal(myIdx, task)) { /* got stolen task */ }
            if (task) { task(); continue; }
            if (stop) break;
            std::unique_lock<std::mutex> lk(cvMtx);
            cv.wait_for(lk, std::chrono::milliseconds(10));
        }
    }
};
