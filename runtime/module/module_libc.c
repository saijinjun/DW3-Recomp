/* Compiled into game modules that are linked without a C library (Android, where the module is a freestanding library
 * built on the device). The compiler still emits calls to these for large copies and clears, so the module carries
 * its own. Hidden: the program's C library keeps its own versions. Build with -ffreestanding so that these loops are
 * not turned back into calls to themselves. */
#include <stddef.h>
#include <stdint.h>

#define DW3_LIBC __attribute__((visibility("hidden")))

DW3_LIBC void* memcpy(void* restrict dst, const void* restrict src, size_t n) {
    unsigned char* d = (unsigned char*)dst;
    const unsigned char* s = (const unsigned char*)src;
    if ((((uintptr_t)d | (uintptr_t)s) & 7) == 0) {
        for (; n >= 8; n -= 8, d += 8, s += 8) *(uint64_t*)d = *(const uint64_t*)s;
    }
    while (n--) *d++ = *s++;
    return dst;
}

DW3_LIBC void* memmove(void* dst, const void* src, size_t n) {
    unsigned char* d = (unsigned char*)dst;
    const unsigned char* s = (const unsigned char*)src;
    if (d == s || n == 0) return dst;
    if (d < s || d >= s + n) return memcpy(dst, src, n);
    while (n--) d[n] = s[n];
    return dst;
}

DW3_LIBC void* memset(void* dst, int value, size_t n) {
    unsigned char* d = (unsigned char*)dst;
    const unsigned char v = (unsigned char)value;
    if (((uintptr_t)d & 7) == 0) {
        const uint64_t wide = 0x0101010101010101ull * v;
        for (; n >= 8; n -= 8, d += 8) *(uint64_t*)d = wide;
    }
    while (n--) *d++ = v;
    return dst;
}

DW3_LIBC int memcmp(const void* a, const void* b, size_t n) {
    const unsigned char* x = (const unsigned char*)a;
    const unsigned char* y = (const unsigned char*)b;
    for (; n; --n, ++x, ++y)
        if (*x != *y) return *x < *y ? -1 : 1;
    return 0;
}
