
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <stdint.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>
#include <math.h>
#include <sched.h>
#include "util.h"

static size_t calibrate(double tt)
{
	size_t n = 20000;
	uint64_t ts = get_nstime();

	loop_cycles(n);

	uint64_t dt = get_nstime() - ts;
	double txl = (double) dt / (double) n;

	return (size_t) ((1e9 * tt) / txl);
}

static void usage(const char *prg)
{
    fprintf(stderr, "Use %s [-c CPUNO] [-t TEST_TIME] [-P PRIORITY] [-R]\n",
            prg);
    exit(1);
}

int main(int ac, const char * const *av)
{
    int i, sched_policy = SCHED_FIFO, sched_prio = -1, cpuno = 0;
    double test_time = 5.0;

    setup_environment(ac, av);

    for (i = 1; i < ac; i++) {
        if (!strcmp(av[i], "-c")) {
            if (++i < ac)
                cpuno = atoi(av[i]);
        } else if (!strcmp(av[i], "-t")) {
            if (++i < ac)
                test_time = atof(av[i]);
        } else if (!strcmp(av[i], "-R")) {
            sched_policy = SCHED_RR;
        } else if (!strcmp(av[i], "-P")) {
            if (++i < ac)
                sched_prio = atoi(av[i]);
        } else {
            usage(av[0]);
        }
    }
    if (sched_prio < 0)
        sched_prio = sched_get_priority_max(sched_policy);

	printf("Running test on CPU %d for %" PRId64 " seconds ...\n", cpuno,
		   (int64_t) test_time);

    thread_set_cpu(cpuno);
    set_sched_policy(sched_policy, sched_prio);

	size_t nloops = calibrate(test_time);
	irq_map birqm, airqm, irqmd;

	parse_irqs(birqm);

	loop_cycles(nloops);

	parse_irqs(airqm);
	diff_irqs(birqm, airqm, irqmd);

	show_irqs(irqmd, cpuno, stdout);

    return 0;
}
