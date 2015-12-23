
#pragma once

typedef int (*sortcmp_t)(const void *, const void *);

void hsort(void *vals, size_t nvals, size_t objsize, sortcmp_t scmp);
