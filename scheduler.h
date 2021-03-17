#ifndef _SCHEDULER_H
#define _SCHEDULER_H

#include "perf.h"

/**
 * Scheduling callback.
 */
typedef int (*predict_phase)(long, long, long, long, long, int);

/**
 * Read performance counter data from the child.
 */
void scheduler_round(pid_t pid, int64_t power);

/**
 * Transfer to little core.
 */
void transfer_to_little(pid_t pid);

/**
 * Transfer to big core.
 */
void transfer_to_big(pid_t pid);

struct perf_info {
	int fd;
	uint64_t id;
	uint64_t val;
	uint64_t code;
	const char *name;
};

// Explicit counters
extern struct perf_info *counters;
extern size_t num_counters;

// Data for counters to be written into
extern struct read_format *events;
extern size_t events_size;

#endif
