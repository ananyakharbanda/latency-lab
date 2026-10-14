#include <iostream>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <atomic>
#include <optional>

template<typename T>
class BlockingQueue {
    std::mutex mtx;
    std::condition_variable not_empty;
    std::queue<T> q;
    bool closed = false;

public:
    void push(T v) {
        {
            std::lock_guard<std::mutex> lk(mtx);
            q.push(std::move(v));
        }
        not_empty.notify_one();
    }
    
    std::optional<T> pop() {
        std::unique_lock<std::mutex> lk(mtx);
        not_empty.wait(lk, [&]{return !q.empty() || closed; });
        if (q.empty()) {
            return std::nullopt;
        }
        T v = std::move(q.front());
        q.pop();
        return v;
    }
    
    void close() {
        {
            std::lock_guard<std::mutex> lk(mtx);
            closed = true;
        }
        not_empty.notify_all();
    }
};
    
