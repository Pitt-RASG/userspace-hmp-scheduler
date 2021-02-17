#ifndef _SCHEDULER_H
#define _SCHEDULER_H

#include "perf.h"

/**
 * Read performance counter data from the child.
 */
void scheduler_round();

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
