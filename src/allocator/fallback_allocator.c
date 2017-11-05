#include "allocator.h"
#include "allocator_common.h"

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