#ifndef _PERF_H
#define _PERF_H

#include <linux/perf_event.h>
#include <linux/hw_breakpoint.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/syscall.h>

#define die(msg) do { perror(msg); abort(); } while (0)

/**
 * Struct definition for unimplemented library function perf_event_open.
 * See manual for perf_event_open(2).
 */
struct read_format {
	uint64_t value;
};

/**
 * Syscall wrapper for unimplemented library function perf_event_open.
 * See manual for perf_event_open(2).
 */
static inline int perf_event_open(struct perf_event_attr *attr, pid_t pid, int cpu, int group_fd, unsigned long flags)
{
	int ret = syscall(__NR_perf_event_open, attr, pid, cpu, group_fd, flags);
	if (ret < 0) die("perf_event_open");
	return ret;
}

#endif // _PERF_H
