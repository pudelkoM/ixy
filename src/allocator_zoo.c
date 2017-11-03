#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "allocator_zoo.h"
#include "log.h"

/**
 * container_of - cast a member of a structure out to the containing structure
 * @ptr:        the pointer to the member.
 * @type:       the type of the container struct this is embedded in.
 * @member:     the name of the member within the struct.
 *
 */
#define container_of(ptr, type, member) ({                  \
    const typeof( ((type *)0)->member ) *__mptr = (ptr);    \
    (type *)( (char *)__mptr - offsetof(type,member) );})

/* Indirect macros required for expanded argument pasting, eg. __LINE__. */
#define ___PASTE(a, b) a##b
#define __PASTE(a, b) ___PASTE(a,b)

#define __UNIQUE_ID(prefix) __PASTE(__PASTE(__UNIQUE_ID_, prefix), __COUNTER__)

/*
 * min()/max()/clamp() macros that also do
 * strict type-checking.. See the
 * "unnecessary" pointer comparison.
 */
#define __min(t1, t2, min1, min2, x, y) ({              \
        t1 min1 = (x);                                  \
        t2 min2 = (y);                                  \
        (void) (&min1 == &min2);                        \
        min1 < min2 ? min1 : min2; })
#define min(x, y)                                       \
        __min(typeof(x), typeof(y),                     \
              __UNIQUE_ID(min1_), __UNIQUE_ID(min2_),   \
              x, y)

#define __max(t1, t2, max1, max2, x, y) ({              \
        t1 max1 = (x);                                  \
        t2 max2 = (y);                                  \
        (void) (&max1 == &max2);                        \
        max1 > max2 ? max1 : max2; })
#define max(x, y)                                       \
        __max(typeof(x), typeof(y),                     \
              __UNIQUE_ID(max1_), __UNIQUE_ID(max2_),   \
              x, y)


/* wrapper around malloc/free */

struct mallocator {
    struct allocator vfs;
};

static struct mem_blk mallocator_allocate(struct allocator* a, size_t size) {
    return (struct mem_blk) {
            malloc(size),
            size
    };
}

static void mallocator_deallocate(struct allocator* a, struct mem_blk* blk) {
    free(blk->ptr);
#if !defined(NDEBUG)
    blk->ptr = NULL;
    blk->size = 0;
#endif
}

static bool mallocator_owns(struct allocator* a, const struct mem_blk* blk) {
    return true;
}

struct allocator* mallocator_new() {
    struct allocator a = {
            1u,
            mallocator_allocate,
            mallocator_deallocate,
            mallocator_owns
    };
    struct mallocator* ma = malloc(sizeof(*ma));
    memcpy(&ma->vfs, &a, sizeof(a));
    return &ma->vfs;
}

void mallocator_free(struct allocator* a) {
    struct mallocator* self = container_of(a, struct mallocator, vfs);
    free(self);
}

struct allocator mallocator_t = {
        1u,
        mallocator_allocate,
        mallocator_deallocate,
        mallocator_owns
};

/* fallback_allocator - falls back to secondary if primary fails to allocate */

struct fallback_allocator {
    struct allocator self;
    struct allocator* primary;
    struct allocator* secondary;
};

static struct mem_blk fallback_allocator_allocate(struct allocator* a, size_t size) {
    struct fallback_allocator* self = container_of(a, struct fallback_allocator, self);
    struct mem_blk blk = self->primary->allocate(self->primary, size);
    if (!blk.ptr)
        blk = self->secondary->allocate(self->secondary, size);
    return blk;
}

static void fallback_allocator_deallocate(struct allocator* a, struct mem_blk* blk) {
    struct fallback_allocator* self = container_of(a, struct fallback_allocator, self);
    if (self->primary->owns(self->primary, blk))
        self->primary->deallocate(self->primary, blk);
    else
        self->secondary->deallocate(self->secondary, blk);
}

static bool fallback_allocator_owns(struct allocator* a, const struct mem_blk* blk) {
    struct fallback_allocator* self = container_of(a, struct fallback_allocator, self);
    return self->primary->owns(self->primary, blk) || self->secondary->owns(self->secondary, blk);
}

/**
 * @primary:    first allocator, must define valid allocator::owns
 * @secondary:  fallback allocator, used if primary fails
 */
struct allocator* fallback_allocator_new(struct allocator* primary, struct allocator* secondary) {
    struct allocator a = {
            min(primary->alignment, secondary->alignment),
            fallback_allocator_allocate,
            fallback_allocator_deallocate,
            fallback_allocator_owns,
    };
    struct fallback_allocator* fb = malloc(sizeof(*fb));
    memcpy(&fb->self, &a, sizeof(a));
    fb->primary = primary;
    fb->secondary = secondary;
    return &fb->self;
}

void fallback_allocator_free(struct allocator* a) {
    struct fallback_allocator* self = container_of(a, struct fallback_allocator, self);
    free(self);
}


/* null allocator - does nothing */

struct null_allocator {
    struct allocator vfs;
};

static struct mem_blk null_allocator_allocate(struct allocator* a, size_t size) {
    return (struct mem_blk) {NULL, 0};
}

static void null_allocator_deallocate(struct allocator* a, struct mem_blk* blk) {
    if (blk->ptr || blk->size)
        error("Attempting to free not NULL blk with null allocator");
}

static bool null_allocator_owns(struct allocator* a, const struct mem_blk* blk) {
    return false;
}

struct allocator* null_allocator_new() {
    struct allocator a = {
            1u,
            null_allocator_allocate,
            null_allocator_deallocate,
            null_allocator_owns
    };
    struct null_allocator* na = malloc(sizeof(*na));
    memcpy(&na->vfs, &a, sizeof(a));
    return &na->vfs;
}

void null_allocator_free(struct allocator* a) {
    struct null_allocator* self = container_of(a, struct null_allocator, vfs);
    free(self);
}

struct allocator null_allocator_t = {
        1u,
        null_allocator_allocate,
        null_allocator_deallocate,
        null_allocator_owns
};


/* stack allocator */

struct stack_allocator {
    struct allocator vfs;
    struct allocator* parent;
    void* base_addr;
    uintptr_t base_addr_phy;
    uint32_t entry_size;
    uint32_t num_entries;
    uint32_t free_stack_top;
    uint32_t free_stack[];
};

static struct mem_blk stack_allocator_allocate(struct allocator* a, size_t size) {
    struct stack_allocator* self = container_of(a, struct stack_allocator, vfs);
    if (self->free_stack_top == 0 || size > self->entry_size)
        return (struct mem_blk) {NULL, 0};
    uint32_t entry_id = self->free_stack[--self->free_stack_top];
    return (struct mem_blk) {((uint8_t*) self->base_addr) + entry_id * self->entry_size, self->entry_size};
}

static void stack_allocator_deallocate(struct allocator* a, struct mem_blk* blk) {
    static_assert(PTRDIFF_MAX >= UINT32_MAX, "PTRDIFF_MAX < UINT32_MAX");
    struct stack_allocator* self = container_of(a, struct stack_allocator, vfs);
    if (self->entry_size != blk->size)
        error("Size of returned block (%zu) does not match stack element size (%u)", blk->size, self->entry_size);
    ptrdiff_t entry_id = (blk->ptr - self->base_addr) / self->entry_size;
    if (entry_id > self->num_entries || entry_id < 0)
        error("Calculated entry id (%lu) is outside of stack range", entry_id);
    self->free_stack[self->free_stack_top++] = (uint32_t) entry_id;
#if !defined(NDEBUG)
    blk->ptr = NULL;
    blk->size = 0;
#endif
}

static bool stack_allocator_owns(struct allocator* a, const struct mem_blk* blk) {
    struct stack_allocator* self = container_of(a, struct stack_allocator, vfs);
    return blk->ptr >= self->base_addr && blk->ptr < self->base_addr + self->num_entries * self->entry_size;
}

struct allocator* stack_allocator_new(uint32_t num_entries, uint32_t entry_size, struct allocator* parent) {
    entry_size = entry_size ? entry_size : 2048;
    struct allocator a = {
            1u,
            stack_allocator_allocate,
            stack_allocator_deallocate,
            stack_allocator_owns
    };
    struct stack_allocator* sa = malloc(sizeof(*sa) + sizeof(uint32_t) * num_entries);
    if (!sa)
        return NULL;
    memcpy(&sa->vfs, &a, sizeof(a));

    sa->parent = parent;
    struct mem_blk mem = parent->allocate(parent, num_entries * entry_size);
    if (!mem.ptr)
        goto error;
    sa->num_entries = num_entries;
    sa->entry_size = entry_size;
    sa->base_addr = mem.ptr;
    sa->free_stack_top = num_entries;
    for (uint32_t i = 0; i < num_entries; i++) {
        sa->free_stack[i] = i;
    }
    return &sa->vfs;
    error:
    free(sa);
    return NULL;
}

void stack_allocator_free(struct allocator* a) {
    struct stack_allocator* self = container_of(a, struct stack_allocator, vfs);
    struct mem_blk blk = {self->base_addr, self->num_entries * self->entry_size};
    self->parent->deallocate(self->parent, &blk);
    free(self);
}
