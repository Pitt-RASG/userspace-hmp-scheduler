#define _GNU_SOURCE
#include <assert.h>
#include <sched.h>
#include <signal.h>
#include <stdarg.h>
#include <sys/prctl.h>

#include "scheduler.h"

struct perf_info *counters = NULL;
struct read_format *events = NULL;

size_t num_counters = 0;
size_t events_size = 0;

static int little_cores[] = { 0, 1, 2, 3, 4, 5 };
static int big_cores[]    = { 6, 7 };
static int is_little      = 0;

#define NELEM(x) (sizeof(x)/sizeof((x)[0]))

/**
 * Transfer to little core.
 */
void transfer_to_little(pid_t pid)
{
	cpu_set_t cpuset;
	CPU_ZERO(&cpuset);

	is_little = 1;

	for (size_t i = 0; i < NELEM(little_cores); i++) {
		CPU_SET(little_cores[i], &cpuset);
	}

	if (sched_setaffinity(pid, sizeof(cpuset), &cpuset) < 0) {
		die("sched_setaffinity");
	}
}

/**
 * Transfer to big core.
 */
void transfer_to_big(pid_t pid)
{
	cpu_set_t cpuset;
	CPU_ZERO(&cpuset);

	is_little = 0;

	for (size_t i = 0; i < NELEM(big_cores); i++) {
		CPU_SET(big_cores[i], &cpuset);
	}

	if (sched_setaffinity(pid, sizeof(cpuset), &cpuset) < 0) {
		die("sched_setaffinity");
	}
}

/**
 * Read performance counter data from the child.
 */
void scheduler_round(pid_t pid, predict_phase predictor)
{
	uint64_t cpu_cycles, inst_retired, l2d_cache, l2d_cache_refill, br_mis_pred;
	int predicted_phase;

	cpu_cycles       = events[0].value;
	inst_retired     = events[1].value;
	l2d_cache        = events[2].value;
	l2d_cache_refill = events[3].value;
	br_mis_pred      = events[4].value;

	// Feed the raw data into the predictor model.
	//
	// Change the thread affinity if the predictor thinks a
	// migration is justified.

	// match trained model format
	predicted_phase = predictor(cpu_cycles, inst_retired, l2d_cache, l2d_cache_refill, br_mis_pred, is_little ? 0 : 4);

	if (predicted_phase >= 5 && !is_little) {
		puts("little transfer");
		transfer_to_little(pid);
	} else if (predicted_phase < 5 && is_little) {
		puts("big transfer");
		transfer_to_big(pid);
	}
}
