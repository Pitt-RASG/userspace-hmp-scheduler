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

static int little_core = 0;
static int big_core = 6;
static int is_little = 0;

#define NELEM(x) (sizeof(x)/sizeof((x)[0]))

/**
 * Transfer to little core.
 */
void transfer_to_little(pid_t pid)
{
	cpu_set_t cpuset;
	CPU_ZERO(&cpuset);
	CPU_SET(little_core, &cpuset);

	is_little = 1;

	if (sched_setaffinity(pid, sizeof(cpuset), &cpuset) < 0) {
		die("sched_setaffinity");
	}
}

void set_little_core(int core_number)
{
	little_core = core_number;
}

/**
 * Transfer to big core.
 */
void transfer_to_big(pid_t pid)
{
	cpu_set_t cpuset;
	CPU_ZERO(&cpuset);
	CPU_SET(big_core, &cpuset);

	is_little = 0;

	if (sched_setaffinity(pid, sizeof(cpuset), &cpuset) < 0) {
		die("sched_setaffinity");
	}
}

void set_big_core(int core_number)
{
	big_core = core_number;
}

/**
 * Read performance counter data from the child.
 */
void scheduler_round(pid_t pid, int64_t power, int run_scheduler)
{
	static uint64_t g_cpu_cycles, g_inst_retired, g_l2d_cache, g_l2d_cache_refill, g_br_mis_pred;
	uint64_t cpu_cycles, inst_retired, l2d_cache, l2d_cache_refill, br_mis_pred;
	int predicted_phase = 0;

	cpu_cycles       = events[0].value - g_cpu_cycles;
	inst_retired     = events[1].value - g_inst_retired;
	l2d_cache        = events[2].value - g_l2d_cache;
	l2d_cache_refill = events[3].value - g_l2d_cache_refill;
	br_mis_pred      = events[4].value - g_br_mis_pred;

	g_cpu_cycles       = events[0].value;
	g_inst_retired     = events[1].value;
	g_l2d_cache        = events[2].value;
	g_l2d_cache_refill = events[3].value;
	g_br_mis_pred      = events[4].value;

	// Feed the raw data into the predictor model.
	//
	// Change the thread affinity if the predictor thinks a
	// migration is justified.

        if (inst_retired <= 0)
                return;

	// match trained model format
	// predicted_phase = predictor(cpu_cycles, inst_retired, l2d_cache, l2d_cache_refill, br_mis_pred, is_little ? 0 : 4);
	printf("%lu %lu %lu %lu %lu %lu\n", cpu_cycles, inst_retired, l2d_cache, l2d_cache_refill, br_mis_pred, power);

	if (!run_scheduler)
		return;

        predicted_phase = (l2d_cache_refill*1000/inst_retired) > 1;

	if (predicted_phase >= 1 && !is_little) {
		transfer_to_little(pid);
	} else if (predicted_phase < 1 && is_little) {
		transfer_to_big(pid);
	}
}
