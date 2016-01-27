
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "hsort.h"

static inline size_t left(size_t p)
{
    return 2 * p + 1;
}

static inline size_t right(size_t p)
{
    return 2 * p + 2;
}

static inline size_t parent(size_t c)
{
    return (c - 1) / 2;
}

static inline char *obj_at(void *vals, size_t x, size_t objsize)
{
    return (char *) vals + x * objsize;
}

static void hswap(void *vals, size_t x, size_t y, size_t objsize)
{
    char *xo = obj_at(vals, x, objsize);
    char *yo = obj_at(vals, y, objsize);

    for (; objsize >= sizeof(uint64_t); objsize -= sizeof(uint64_t),
             xo += sizeof(uint64_t), yo += sizeof(uint64_t)) {
        uint64_t tmp = *(uint64_t *) xo;

        *(uint64_t *) xo = *(uint64_t *) yo;
        *(uint64_t *) yo = tmp;
    }
    for (; objsize; objsize--, xo++, yo++) {
        char tmp = *xo;

        *xo = *yo;
        *yo = tmp;
    }
}

static void push_up(void *vals, size_t x, size_t objsize, sortcmp_t scmp)
{
    if (x > 0) {
        size_t p = parent(x);

        if (scmp(obj_at(vals, x, objsize), obj_at(vals, p, objsize)) > 0) {
            hswap(vals, x, p, objsize);
            push_up(vals, p, objsize, scmp);
        }
    }
}

static void push_down(void *vals, size_t n, size_t x, size_t objsize,
                      sortcmp_t scmp)
{
    size_t l = left(x);

    if (l < n) {
        size_t r = right(x);
        size_t s = (r >= n || scmp(obj_at(vals, l, objsize),
                                   obj_at(vals, r, objsize)) > 0) ? l: r;

        if (scmp(obj_at(vals, s, objsize), obj_at(vals, x, objsize)) > 0) {
            hswap(vals, x, s, objsize);
            push_down(vals, n, s, objsize, scmp);
        }
    }
}

static void make_heap(void *vals, size_t n, size_t objsize, sortcmp_t scmp)
{
    size_t i;

    for (i = 1; i < n; i++)
        push_up(vals, i, objsize, scmp);
}

void hsort(void *vals, size_t nvals, size_t objsize, sortcmp_t scmp)
{
    size_t i;

    make_heap(vals, nvals, objsize, scmp);
    for (i = nvals; i > 1; i--) {
        hswap(vals, i - 1, 0, objsize);
        push_down(vals, i - 1, 0, objsize, scmp);
    }
}
