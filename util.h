
#pragma once

#include <sys/types.h>
#include <stdint.h>
#include <stdio.h>

#define GET_PCT(p, n, x) ((p)[(size_t) ((double) (n) * (double) (x))])

static __attribute__((always_inline)) inline uint64_t tsc_read(void)
{
    /* In Linux (ecx -> x): CPU = x & 0xfff , NODE = x >> 12
     */
    uint32_t l, h, x;

    asm volatile ("rdtscp" : "=a" (l), "=d" (h), "=c" (x) : : "memory");
    return ((uint64_t) h << 32) | (uint64_t) l;
}

uint64_t get_nstime(void);
double get_ticks_x_ns(long us_sleep);
void thread_set_cpu(int cpu);
void set_sched_policy(int policy, int prio);
void *numa_cpu_alloc(int cpu, size_t size);
void *numa_cpu_zalloc(int cpu, size_t size);
int enable_speed_step(int cpu, int on);
void setup_environment(int ac, const char * const *av);
size_t parse_cpu_list(const char *str, int *cpus, size_t ncpus);
