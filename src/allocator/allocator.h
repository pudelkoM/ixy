#ifndef IXY_ALLOCATOR_H
#define IXY_ALLOCATOR_H

#include <stdbool.h>
#include <stdint.h>
#include <unistd.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Unit of memory. Allocators trade in these.
 * @ptr Pointer to the first byte of the memory block.
 * @size Explicit size of the memory block.
 */
struct mem_blk {
    void* ptr;
    size_t size;
};

/**
 * The common core of and public interface to each allocator.
 * @alingment
 * @allocate
 * @deallocate
 * @owns
 */
struct allocator {
    const unsigned alignment; //< bla

    struct mem_blk (* allocate)(struct allocator*, size_t);

    void (* deallocate)(struct allocator*, struct mem_blk*);

    bool (* owns)(struct allocator*, const struct mem_blk*);
};

/**
 * Creates a new stack (LIFO) allocator for fixed sized elements.
 * Not thread-safe.
 * @param num_entries Number of slots
 * @param entry_size  Size of each element
 * @param parent      Allocator to source memory from
 * @return
 * @see stack_allocator_free()
 */
struct allocator* stack_allocator_new(uint32_t num_entries, uint32_t entry_size, struct allocator* parent);

/**
 *
 * @param a
 */
void stack_allocator_free(struct allocator* a);

/**
 *
 * Thread-safe.
 * @param num_entries
 * @param entry_size
 * @param parent
 * @return
 * @see
 */
struct allocator* spinlock_stack_allocator_new(uint32_t num_entries, uint32_t entry_size, struct allocator* parent);

/**
 *
 * @param a
 * @see spinlock_stack_allocator()
 */
void spinlock_stack_allocator_free(struct allocator* a);

/**
 * Creates a new fallback allocator.
 * A fallback allocator is thread-safe if both primary and secondary are.
 * @param primary    First allocator, must define a valid allocator::owns function
 * @param secondary  Fallback allocator, used if primary fails
 * @return
 */
struct allocator* fallback_allocator_new(struct allocator* primary, struct allocator* secondary);

/**
 * Destroys and frees a fallback allocator. All objects allocated with it remain valid.
 * Primary and secondary are not touched.
 * @param a Allocator to destroy
 */
void fallback_allocator_free(struct allocator* a);

/**
 * Creates a new null allocator. Null allocators don't allocate memory and always return
 * memory blocks with pointer to NULL and size 0. Mostly useful for debugging or testing.
 * Thread-safe.
 * @return New null allocator object
 * @see    null_allocator_free()
 */
struct allocator* null_allocator_new();

/**
 * Frees a null allocator and all associated memory (i.e. none) with it.
 * @param a Null allocator to free. Must be an object created with null_allocator_new(), undefined behavior otherwise.
 * @see     null_allocator_new()
 */
void null_allocator_free(struct allocator* a);

extern struct allocator null_allocator_t;

/**
 *
 * A mallocator object is thread-safe if the underlying malloc/free functions are.
 * @return
 * @see mallocator_free()
 */
struct allocator* mallocator_new();

/**
 *
 * @param a
 * @see mallocator_new()
 */
void mallocator_free(struct allocator* a);

/**
 * Static mallocator object for simple use. Since mallocator is just a wrapper around malloc/free,
 * one usually does not need an own instance of it.
 */
extern struct allocator mallocator_t;

/**
 *
 * @return
 */
struct allocator* dma_allocator_new();

/**
 *
 * @param a
 */
void dma_allocator_free(struct allocator* a);

extern struct allocator dma_allocator_t;

#ifdef __cplusplus
}
#endif

#endif //IXY_ALLOCATOR_H
