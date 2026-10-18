#include "blocking_queue.h"

void push(BlockingQueue<int>& bqi) {
    for (int i = 0; i < 1'000'000; i++) {
        bqi.push(i);
    }
}

void pop(BlockingQueue<int>& bqi) {
    while (!bqi.is_closed() || !bqi.is_empty()) {
        auto x = bqi.pop();
        if (x) {
            std::cout << *x << std::endl;
        }
    }
}
        
int main() {
    BlockingQueue<int> bq;
    std::thread t1(push, std::ref(bq));
    std::thread t2(pop, std::ref(bq));
    t1.join();
    bq.close();
    t2.join();
}
     
