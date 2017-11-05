#include "allocator/allocator.h"

#include <thread>
#include <vector>
#include <cassert>

// This is in C++ because dealing with threads through pthread is zero fun

// To be replaced by real test framework

void create_delete_test() {
    auto a = spinlock_stack_allocator_new(12, 64, &mallocator_t);
    spinlock_stack_allocator_free(a);
}

void mt_create_delete_test() {
    auto a = spinlock_stack_allocator_new(12, 64, &mallocator_t);
    auto t1 = std::thread([&]() {
        spinlock_stack_allocator_free(a);
    });
    t1.join();
}

void simple_alloc_test() {
    auto a = spinlock_stack_allocator_new(12, 64, &mallocator_t);
    auto blk = a->allocate(a, 64);
    assert(blk.ptr && blk.size >= 64);
    a->deallocate(a, &blk);
    spinlock_stack_allocator_free(a);
}

void mt_test() {
    auto a = spinlock_stack_allocator_new(12, 64, &mallocator_t);

    std::vector<mem_blk> blks(0);
    auto t1 = std::thread([&]() {
        mem_blk blk;
        do {
            blk = a->allocate(a, 5);
            blks.push_back(blk);
            std::this_thread::sleep_for(std::chrono::nanoseconds(10));
        } while (blk.ptr);
    });
    t1.join();

    spinlock_stack_allocator_free(a);
}

void mt_concurrent_alloc_free() {
    auto a = spinlock_stack_allocator_new(12, 64, &mallocator_t);
    auto f = [&]() {
        for (int i = 0; i < 10000; ++i) {
            mem_blk blk;
            blk = a->allocate(a, 5);
            std::this_thread::sleep_for(std::chrono::nanoseconds(1));
            a->deallocate(a, &blk);
            std::this_thread::sleep_for(std::chrono::nanoseconds(1));
        }
    };
    auto t1 = std::thread(f);
    auto t2 = std::thread(f);
    t1.join();
    t2.join();
    spinlock_stack_allocator_free(a);
}

int main() {
    create_delete_test();
    mt_create_delete_test();
    simple_alloc_test();
    mt_test();
    mt_concurrent_alloc_free();
}