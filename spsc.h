#include <thread>
#include <iostream>
#include <array>
#include <atomic>
#include <optional>
#include <cstddef>

template<typename T, std::size_t Size>
class SPSC {
private:
    static_assert(Size > 0 && (Size & (Size - 1)) == 0, "Size must be a power of 2");
    static constexpr std::size_t mask = Size - 1;

    alignas(64) std::array<T, Size> c_queue;

    alignas(64) std::atomic<std::size_t> w_pos{0};
    std::size_t cached_r{0};

    alignas(64) std::atomic<std::size_t> r_pos{0};
    std::size_t cached_w{0};
    

public:
    SPSC() {}
        
    bool push(const T& obj) {
        std::size_t w = w_pos.load(std::memory_order_relaxed);
        if (w - cached_r == Size) {
            cached_r = r_pos.load(std::memory_order_acquire);
            if (w - cached_r == Size) {
                return false;
            }
        }
        c_queue[w & mask] = obj;
        w_pos.store(w + 1, std::memory_order_release);
        return true;
    }
    
    std::optional<T> pop() {
        std::size_t r = r_pos.load(std::memory_order_relaxed);
        if (r == cached_w) { 
            cached_w = w_pos.load(std::memory_order_acquire);
            if (r == cached_w) {
                return std::nullopt;
            }
        }
        T obj = std::move(c_queue[r & mask]);
        r_pos.store(r + 1, std::memory_order_release);
        return obj;
    }
};

