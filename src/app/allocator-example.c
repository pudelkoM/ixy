#include <stdio.h>
#include <log.h>
#include "allocator_zoo.h"


int main() {
    struct allocator* a;

    a = &mallocator_t;

    struct mem_blk blk = a->allocate(a, 1024);
    printf("%p, %zu\n", blk.ptr, blk.size);
    a->deallocate(a, &blk);

    struct allocator* sta = stack_allocator_new(1024, 64, &mallocator_t);
    blk = sta->allocate(sta, 128);
    if (blk.ptr)
        error("mem blk larger than element size");
    for (int i = 0; i < 1024; ++i) {
        blk = sta->allocate(sta, 64);
        if (!blk.ptr)
            error("stack allocation failed at i %i", i);
    }
    sta->deallocate(sta, &blk);
    blk = sta->allocate(sta, 64);
    if (!blk.ptr)
        error("stack should have one slot left");
    blk = sta->allocate(sta, 64);
    if (blk.ptr)
        error("stack should be empty");
    stack_allocator_free(sta);
}
