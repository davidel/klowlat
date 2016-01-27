
#include <string>
#include <vector>
#include <map>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>
#include <math.h>
#include <sched.h>
#include <numa.h>
#include <numaif.h>
#include "util.h"

uint64_t get_nstime(void)
{
    struct timespec ts;

    if (clock_gettime(CLOCK_MONOTONIC, &ts)) {
        perror("clock_gettime");
        exit(2);
    }

    return (uint64_t) ts.tv_sec * 1000 * 1000000 + (uint64_t) ts.tv_nsec;
}

double get_ticks_x_ns(long us_sleep)
{
    uint64_t sns, ens, scy, ecy;

    sns = get_nstime();
    scy = tsc_read();
    usleep(us_sleep);
    ecy = tsc_read();
    ens = get_nstime();

    return (double) (ecy - scy) / (double) (ens - sns);
}

void thread_set_cpu(int cpu)
{
    static const int bits_per_long = 8 * sizeof(unsigned long);
    int node, nbits;
    cpu_set_t *cpus;
    unsigned long *nmask;

    cpus = CPU_ALLOC(cpu + 1);
    CPU_ZERO_S(CPU_ALLOC_SIZE(cpu + 1), cpus);
    CPU_SET(cpu, cpus);
    if (sched_setaffinity(0, CPU_ALLOC_SIZE(cpu + 1), cpus)) {
        perror("sched_setaffinity");
        exit(2);
    }
    CPU_FREE(cpus);

    node = numa_node_of_cpu(cpu);
    nbits = ((node + bits_per_long) / bits_per_long) * bits_per_long;
    nmask = (unsigned long *) calloc(nbits / bits_per_long, sizeof(unsigned long));
    nmask[node / bits_per_long] |= 1 << (node % bits_per_long);
    if (set_mempolicy(MPOL_BIND, nmask, nbits)) {
        perror("set_mempolicy");
        exit(2);
    }
    free(nmask);
}

void set_sched_policy(int policy, int prio)
{
    struct sched_param sp;

    memset(&sp, 0, sizeof(sp));
    sp.sched_priority = prio;
    if (sched_setscheduler(0, policy, &sp)) {
        perror("sched_setscheduler");
        exit(2);
    }
}

void *numa_cpu_alloc(int cpu, size_t size)
{
    void *addr = numa_alloc_onnode(size, numa_node_of_cpu(cpu));

    if (!addr) {
        perror("numa_alloc_onnode");
        exit(2);
    }

    return addr;
}

void *numa_cpu_zalloc(int cpu, size_t size)
{
    void *addr = numa_cpu_alloc(cpu, size);

    memset(addr, 0, size);

    return addr;
}

int enable_speed_step(int cpu, int on)
{
    static const uint64_t ss_bit = (uint64_t) 1 << 32;
    static const off_t perf_ctl_msr = 0x199;
    int fd, status;
    uint64_t val, xval;
    char msrdev[256];

    snprintf(msrdev, sizeof(msrdev), "/dev/cpu/%d/msr", cpu);
    fd = open(msrdev, O_RDWR);
    if (fd < 0) {
        fprintf(stderr, "MSR device not available, leaving speed step as it was!\n");
        return -1;
    }
    if (pread(fd, &val, sizeof(val), perf_ctl_msr) != sizeof(val)) {
        fprintf(stderr, "Unable to read MSR device register 0x%lx: %s\n",
                perf_ctl_msr, strerror(errno));
        exit(2);
    }
    status = (val & ss_bit) ? 0 : 1;
    if (status ^ (on != 0)) {
        if (on)
            val &= ~ss_bit;
        else
            val |= ss_bit;
        if (pwrite(fd, &val, sizeof(val), perf_ctl_msr) != sizeof(val)) {
            fprintf(stderr, "Unable to write MSR device: %s\n", strerror(errno));
            exit(2);
        }
        if (pread(fd, &xval, sizeof(xval), perf_ctl_msr) != sizeof(xval)) {
            fprintf(stderr, "Unable to read MSR device: %s\n", strerror(errno));
            exit(2);
        }
        if (val != xval) {
            fprintf(stderr, "Unable to write MSR device. "
                    "Value 0x%lx did not stick at MSR 0x%lx!\n", val, perf_ctl_msr);
            exit(2);
        }
    }
    close(fd);

    return status;
}

void setup_environment(int ac, const char * const *av)
{
    numa_set_strict(1);
    if (mlockall(MCL_CURRENT | MCL_FUTURE)) {
        perror("mlockall");
        exit(2);
    }
}

size_t parse_cpu_list(const char *str, int *cpus, size_t ncpus)
{
    size_t n = 0;
    char *dstr = strdup(str), *sptr, *tok;

    tok = strtok_r(dstr, ",", &sptr);
    while (tok && n < ncpus) {
        int s, e;

        if (sscanf(tok, "%d-%d", &s, &e) == 2) {
            for (; s <= e && n < ncpus; s++)
                cpus[n++] = s;
        } else if (sscanf(tok, "%d", &s) == 1) {
            cpus[n++] = s;
        } else {
            fprintf(stderr, "Bad CPU list format: %s\n", tok);
            exit(2);
        }
        tok = strtok_r(NULL, ",", &sptr);
    }
    free(dstr);

    return n;
}

void parse_irqs(irq_map &irqm)
{
	size_t ncpus = sysconf(_SC_NPROCESSORS_ONLN);
	FILE *file = fopen("/proc/interrupts", "r");
	char buffer[4096];

	if (fgets(buffer, sizeof(buffer), file)) {
		while (fgets(buffer, sizeof(buffer), file)) {
			char *name, *ptr, *sptr;

			if (!(name = strtok_r(buffer, ": \t\r\n", &sptr)))
				continue;

			irq_map::iterator
				it = irqm.insert(irq_map::value_type(name, irq_vector())).first;

			it->second.reserve(ncpus + 1);
			while ((ptr = strtok_r(NULL, ": \t\r\n", &sptr)))
				it->second.push_back(strtoul(ptr, NULL, 10));
		}
	}
	fclose(file);
}
