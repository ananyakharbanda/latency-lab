#include <thread>
#include <iostream>
#include <array>
#include <atomic>
#include <optional>

template<typename T, int Size>
class SPSC {
private:
    std::array<T, Size> c_queue;
    alignas(64) std::atomic<int> w_pos{0};
    alignas(64) std::atomic<int> r_pos{0};
    
    int next(int curr) {
        if (curr == Size - 1) {
            return 0;   
        } else {
            return curr + 1;
        }
    }
            
    
public:
    SPSC() {}
        
    bool push(const T& obj) {
        int w = w_pos.load(std::memory_order_relaxed);
        int n = next(w);
        if (n != r_pos.load(std::memory_order_acquire)) {
            c_queue[w] = obj;
            w_pos.store(n, std::memory_order_release);
            return true;
        }
        return false;
    }
    
    std::optional<T> pop() {
        int r = r_pos.load(std::memory_order_relaxed);
        if (r != w_pos.load(std::memory_order_acquire)) { 
            T obj = std::move(c_queue[r]);
            r_pos.store(next(r_pos), std::memory_order_release);
            return obj;
        }
        return std::nullopt;
    }
};


int main() {
    SPSC<int, 20> q;
    const int N = 1'000'000;
    
    std::thread producer([&] {
        for (int i = 1; i <= N; i++) {
            while (!q.push(i)) {}
        }
    });
    
    long long sum = 0;
    int expected = 1;

    bool in_order = true;
    std::thread consumer([&] {
        for (int i = 0; i < N; ) {
            if (auto v = q.pop()) {
                if (*v != expected) in_order = false;
                ++expected;
                sum += *v;
                i++;
            }
        }
    });
    
    producer.join();
    consumer.join();
    
    std::cout << "sum = " << sum << " (expected " << (long long)N * (N+1) / 2 << ")\n";
    std::cout << (in_order ? "order OK\n" : "ORDER BROKEN\n");
    
    return 0;
}
        
