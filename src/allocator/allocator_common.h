#ifndef IXY_ALLOCATOR_UTILS_HPP
#define IXY_ALLOCATOR_UTILS_HPP

#include <stddef.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>

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


#endif //IXY_ALLOCATOR_UTILS_HPP
