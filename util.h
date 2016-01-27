
#pragma once

#include <string>
#include <vector>
#include <map>
#include <sys/types.h>
#include <stdint.h>
#include <stdio.h>

#define MAX_CPUS 1024

#define GET_PCT(p, n, x) ((p)[(size_t) ((double) (n) * (double) (x))])

typedef std::vector<uint64_t> irq_vector;
typedef std::map<std::string, irq_vector> irq_map;

static __attribute__((always_inline)) inline uint64_t tsc_read()
{
    /* In Linux (ecx -> x): CPU = x & 0xfff , NODE = x >> 12
     */
    uint32_t l, h, x;

    asm volatile ("rdtscp" : "=a" (l), "=d" (h), "=c" (x) : : "memory");
    return ((uint64_t) h << 32) | (uint64_t) l;
}

uint64_t accum(uint64_t sum) __attribute__((optimize(0)));
uint64_t loop_cycles(size_t n) __attribute__((optimize(0)));
uint64_t get_nstime();
double get_ticks_x_ns(long us_sleep);
void thread_set_cpu(int cpu);
void set_sched_policy(int policy, int prio);
void *numa_cpu_alloc(int cpu, size_t size);
void *numa_cpu_zalloc(int cpu, size_t size);
int enable_speed_step(int cpu, int on);
void setup_environment(int ac, const char * const *av);
size_t parse_cpu_list(const char *str, int *cpus, size_t ncpus);
void parse_irqs(irq_map &irqm);
void diff_irqs(const irq_map &irqm1, const irq_map &irqm2, irq_map &irqmd);
void show_irqs(const irq_map &irqm, size_t cpuno, FILE *file);
