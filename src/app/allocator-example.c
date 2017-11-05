#include "allocator/allocator.h"

#include <stdio.h>
#include <log.h>


int main() {
    struct allocator* a;
    a = &mallocator_t;

    struct mem_blk blk = a->allocate(a, 1024);
    printf("%p, %zu\n", blk.ptr, blk.size);
    a->deallocate(a, &blk);

    a = stack_allocator_new(1024, 64, &mallocator_t);
    blk = a->allocate(a, 128);
    if (blk.ptr)
        error("mem blk larger than element size");
    for (int i = 0; i < 1024; ++i) {
        blk = a->allocate(a, 64);
        if (!blk.ptr)
            error("stack allocation failed at i %i", i);
    }
    a->deallocate(a, &blk);
    blk = a->allocate(a, 64);
    if (!blk.ptr)
        error("stack should have one slot left");
    blk = a->allocate(a, 64);
    if (blk.ptr)
        error("stack should be empty");
    stack_allocator_free(a);

    a = fallback_allocator_new(&null_allocator_t, &mallocator_t);
    blk = a->allocate(a, 64);
    if (!blk.ptr)
        error("Should have fallen back to malloc");
    a->deallocate(a, &blk);
    fallback_allocator_free(a);

    a = spinlock_stack_allocator_new(10, 8, &mallocator_t);
    blk = a->allocate(a, 8);
    if (!blk.ptr)
        error("stack should have one slot left");
    if (!a->owns(a, &blk))
        error("stack must own allocated block");
    a->deallocate(a, &blk);
    spinlock_stack_allocator_free(a);
}
