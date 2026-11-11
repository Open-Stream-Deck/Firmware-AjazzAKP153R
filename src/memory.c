#include "memory.h"
#include <stdint.h>

void *memset(void *destination, int value, size_t count)
{
    unsigned char *out = destination;
    while (count-- != 0) {
        *out++ = (unsigned char)value;
    }
    return destination;
}

typedef uint32_t loose_u32 __attribute__((aligned(1), may_alias));

__attribute__((optimize("no-tree-loop-distribute-patterns"))) void *
memcpy(void *destination, const void *source, size_t count)
{
    unsigned char *out = destination;
    const unsigned char *in = source;
    while (count != 0 && ((uintptr_t)out & 3u) != 0) {
        *out++ = *in++;
        count--;
    }
    for (; count >= 4; count -= 4, out += 4, in += 4) {
        *(uint32_t *)(void *)out = *(const loose_u32 *)(const void *)in;
    }
    while (count-- != 0) {
        *out++ = *in++;
    }
    return destination;
}
