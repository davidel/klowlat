
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
#include "hsort.h"
#include "util.h"

#define RU_DIFF(p, r2, r1, f) (p)->f = (r2)->f - (r1)->f

struct bench_ctx {
    pthread_t tid;
    int cpu;
    time_t test_time;
    int sched_policy;
    int sched_prio;
    double ticks_x_ns;
    size_t nsamples;
    size_t count;
    uint64_t *samples;
    size_t compacted;
    unsigned long ru_nivcsw;
    unsigned long ru_minflt;
};

struct sstat {
    uint64_t smin;
    uint64_t smax;
    double savg;
    double sdev;
};

static void setup_cpu_environment(struct bench_ctx *ctx)
{
    thread_set_cpu(ctx->cpu);
    set_sched_policy(ctx->sched_policy, ctx->sched_prio);

    enable_speed_step(ctx->cpu, 0);
}

static void cleanup_cpu_environment(struct bench_ctx *ctx)
{
    enable_speed_step(ctx->cpu, 1);
}

static uint64_t accum(uint64_t sum) __attribute__((optimize(0)));

static uint64_t accum(uint64_t sum)
{
    uint64_t i;

    for (i = 0; i < 1000; i++)
        sum += i;
    for (i = 0; i < 1000; i++)
        sum -= i;

    return sum;
}

static uint64_t loop_cycles(size_t n) __attribute__((optimize(0)));

static uint64_t loop_cycles(size_t n)
{
    uint64_t scy, s = 0;

    scy = tsc_read();
    for (; n; n--)
        s += accum(s);
    return tsc_read() - scy + s;
}

static int cmp_samples(const void *s1, const void *s2)
{
    const uint64_t *u1 = (const uint64_t *) s1, *u2 = (const uint64_t *) s2;

    return *u1 < *u2 ? -1 : (*u1 > *u2 ? +1 : 0);
}

static uint64_t *compact_samples(uint64_t *sptr, uint64_t *tptr)
{
    size_t count = tptr - sptr;
    uint64_t *cptr, *fptr;

    /* Had to add heap sort as qsort triggers minor faults (does not sort
     * in place).
     */
    hsort(sptr, count, sizeof(uint64_t), cmp_samples);

    /* Keep top and botton 33%, discard the center 33%.
     */
    cptr = sptr + count / 3;
    fptr = tptr - count / 3;
    for (; fptr < tptr; cptr++, fptr++)
        *cptr = *fptr;

    asm volatile("mfence" : : : "memory");

    return cptr;
}

static void *bench_thread(void *data)
{
    static const size_t loops_per_cycle = 4;
    struct bench_ctx *ctx = (struct bench_ctx *) data;
    uint64_t test_cycles, lcy;
    size_t i, test_loops;
    uint64_t *sptr, *eptr;
    struct rusage ru1, ru2;

    setup_cpu_environment(ctx);

    ctx->ticks_x_ns = get_ticks_x_ns(500 * 1000);

    getrusage(RUSAGE_THREAD, &ru1);

    lcy = loop_cycles(loops_per_cycle) / loops_per_cycle;
    for (i = 0; i < 20; i++) {
        uint64_t c = loop_cycles(loops_per_cycle) / loops_per_cycle;

        if (c < lcy)
            lcy = c;
    }

    test_cycles = (uint64_t) ((double) ctx->test_time * 1e9 * ctx->ticks_x_ns);
    test_loops = (size_t) (test_cycles / lcy);

    sptr = ctx->samples;
    eptr = sptr + ctx->nsamples;

    for (i = 0; i < test_loops; i += loops_per_cycle) {
        if (__builtin_expect(sptr >= eptr, 0)) {
            sptr = compact_samples(ctx->samples, sptr);
            ctx->compacted++;
        }

        *sptr++ = loop_cycles(loops_per_cycle);

        asm volatile("mfence" : : : "memory");
    }
    ctx->count = sptr - ctx->samples;

    getrusage(RUSAGE_THREAD, &ru2);
    RU_DIFF(ctx, &ru2, &ru1, ru_nivcsw);
    RU_DIFF(ctx, &ru2, &ru1, ru_minflt);

    cleanup_cpu_environment(ctx);

    return NULL;
}

static void stat_compute(uint64_t *smps, size_t n, struct sstat *st)
{
    size_t i;
    double sum;

    memset(st, 0, sizeof(*st));
    if (n > 0) {
        hsort(smps, n, sizeof(uint64_t), cmp_samples);

        st->smin = smps[0];
        st->smax = smps[n - 1];

        sum = 0;
        for (i = 0; i < n; i++)
            sum += (double) smps[i];
        st->savg = sum / (double) n;
        sum = 0;
        for (i = 0; i < n; i++) {
            double d = (double) smps[i] - st->savg;

            sum += d * d;
        }
        st->sdev = sqrt(sum / (double) n);
    }
}

static void usage(const char *prg)
{
    fprintf(stderr, "Use %s -c CPU_LIST [-t TEST_TIME] [-n NSAMPLES] [-P PRIORITY] [-R]\n",
            prg);
    exit(1);
}

int main(int ac, const char * const *av)
{
    int i, sched_policy = SCHED_FIFO, sched_prio = -1;
    time_t test_time = 5;
    size_t n, ncpus = 0, nsamples = 100000, stk_size = 1024 * 1024;
    int cpus[MAX_CPUS];
    struct bench_ctx **ctxs;

    setup_environment(ac, av);

    for (i = 1; i < ac; i++) {
        if (!strcmp(av[i], "-c")) {
            if (++i < ac)
                ncpus += parse_cpu_list(av[i], cpus + ncpus, MAX_CPUS - ncpus);
        } else if (!strcmp(av[i], "-t")) {
            if (++i < ac)
                test_time = strtoul(av[i], NULL, 0);
        } else if (!strcmp(av[i], "-n")) {
            if (++i < ac)
                nsamples = strtoul(av[i], NULL, 0);
        } else if (!strcmp(av[i], "-R")) {
            sched_policy = SCHED_RR;
        } else if (!strcmp(av[i], "-P")) {
            if (++i < ac)
                sched_prio = atoi(av[i]);
        } else if (!strcmp(av[i], "-K")) {
            if (++i < ac)
                stk_size = strtoul(av[i], NULL, 0);
        } else {
            usage(av[0]);
        }
    }
    if (!ncpus)
        usage(av[0]);
    if (sched_prio < 0)
        sched_prio = sched_get_priority_max(sched_policy);

    ctxs = (struct bench_ctx **) calloc(ncpus, sizeof(struct bench_ctx *));

    for (n = 0; n < ncpus; n++) {
        struct bench_ctx *ctx = (struct bench_ctx *)
            numa_cpu_zalloc(cpus[n], sizeof(struct bench_ctx));
        pthread_attr_t attr;

        pthread_attr_init(&attr);
        if (pthread_attr_setstack(&attr,
                                  numa_cpu_alloc(cpus[n], stk_size),
                                  stk_size)) {
            perror("pthread_attr_setstack");
            exit(2);
        }

        ctx->cpu = cpus[n];
        ctx->test_time = test_time;
        ctx->sched_policy = sched_policy;
        ctx->sched_prio = sched_prio;
        ctx->nsamples = nsamples;
        ctx->samples = (uint64_t *)
			numa_cpu_zalloc(ctx->cpu, ctx->nsamples * sizeof(uint64_t));
        if (pthread_create(&ctx->tid, &attr, bench_thread, ctx)) {
            perror("pthread_create");
            exit(2);
        }
        pthread_attr_destroy(&attr);
        ctxs[n] = ctx;
    }

    for (n = 0; n < ncpus; n++) {
        struct bench_ctx *ctx = ctxs[n];

        pthread_join(ctx->tid, NULL);
    }
    for (n = 0; n < ncpus; n++) {
        struct bench_ctx *ctx = ctxs[n];
        struct sstat st;

        stat_compute(ctx->samples, ctx->count, &st);

        printf("[%3d/%3d] nisw=%lu minf=%lu smin=%lu smax=%lu sdev=%.4f "
               "savg=%.2f dpct=%.3f%% scnt=%lu p50=%lu p90=%lu p95=%lu "
               "p99=%lu c=%lu\n",
               ctx->cpu, numa_node_of_cpu(ctx->cpu), ctx->ru_nivcsw,
               ctx->ru_minflt, st.smin, st.smax, st.sdev, st.savg,
               100.0 * st.sdev / st.savg, ctx->count,
               GET_PCT(ctx->samples, ctx->count, 0.50),
               GET_PCT(ctx->samples, ctx->count, 0.90),
               GET_PCT(ctx->samples, ctx->count, 0.95),
               GET_PCT(ctx->samples, ctx->count, 0.99), ctx->compacted);
    }

    return 0;
}
