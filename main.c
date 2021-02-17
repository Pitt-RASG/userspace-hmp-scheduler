#define _GNU_SOURCE

#include <pthread.h>
#include <string.h>

#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/wait.h>

#include "events.h"
#include "perf.h"
#include "scheduler.h"

// Controlling perf counter fd
static int main_perf_fd = -1;
static uint64_t main_perf_id;
static uint64_t main_perf_val;

// Barrier
static pthread_barrierattr_t attr;
static pthread_barrier_t *barrier;

// Traced process
static pid_t child;

/**
 * Configure the barrier used for event reporting, so that when performace
 * counting is enabled, event counting can enabled before the traced process
 * starts.
 */
static void setup_barrier()
{
	barrier = mmap(NULL, sizeof(pthread_barrier_t), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
	if (barrier == MAP_FAILED) {
		die("mmap");
	}

	pthread_barrierattr_init(&attr);
	pthread_barrierattr_setpshared(&attr, PTHREAD_PROCESS_SHARED);
	pthread_barrier_init(barrier, &attr, 2);
}

/**
 * Spawn the child process which will be run, wait on the barrier for events
 * to become enabled, and then execve() the child.
 */
static void spawn_child(const char *pathname, char *const argv[], char *const envp[])
{
	child = fork();

	if (child < 0) {
		die("fork");
	}

	if (child == 0) {
		// Set child affinity to something known here

		pthread_barrier_wait(barrier);

		if (execve(pathname, argv, envp) != 0) {
			die("execve");
		}
	}
}

/**
 * Set up the main perf event fd, which will be used to control other
 * perf reporting events.
 */
static void configure_main_perf_fd()
{
	struct perf_event_attr event_attr = {};
	event_attr.type = PERF_TYPE_HARDWARE;
	event_attr.size = sizeof(struct perf_event_attr);
	event_attr.config = PERF_COUNT_HW_CPU_CYCLES;
	event_attr.disabled = 1;
	event_attr.exclude_kernel = 1;
	event_attr.exclude_hv = 1;
	event_attr.read_format = PERF_FORMAT_GROUP | PERF_FORMAT_ID;

	main_perf_fd = perf_event_open(&event_attr, child, -1, -1, PERF_FLAG_FD_CLOEXEC);

	ioctl(main_perf_fd, PERF_EVENT_IOC_ID, &main_perf_id);
}

/**
 * Set up a raw event counter.
 */
static void configure_raw_counter(size_t num, uint64_t hardware_id)
{
	struct perf_event_attr event_attr = {};
	event_attr.type = PERF_TYPE_RAW;
	event_attr.size = sizeof(struct perf_event_attr);
	event_attr.config = hardware_id;
	event_attr.disabled = 1;
	event_attr.exclude_kernel = 1;
	event_attr.exclude_hv = 1;
	event_attr.read_format = PERF_FORMAT_GROUP | PERF_FORMAT_ID;

	// Install event counter
	counters[num].fd = perf_event_open(&event_attr, child, -1, main_perf_fd, PERF_FLAG_FD_CLOEXEC);

	// Track counter ID from kernel
	ioctl(counters[num].fd, PERF_EVENT_IOC_ID, &counters[num].id);
}

/**
 * Parse a string like "l1d_cache_refill,mem_access,br_pred"
 */
static void parse_event_list(char *event_list, size_t num)
{
	uint64_t code = -1;
	char *token = strtok(event_list, ",");

	if (token == NULL) {
		// No more to read, now allocate array
		num_counters = num;
		counters = calloc(num_counters, sizeof(struct perf_info));
		events_size = sizeof(struct read_format) + sizeof(struct read_values)*(num_counters + 1);
		events = (struct read_format *) calloc(1, events_size);
		return;
	}

	if ((code = event_type_code(token)) == -1) {
		fprintf(stderr, "Unknown event type %s\n", token);
		exit(-1);
	}

	// Store event code on stack and recurse to parse next value
	parse_event_list(NULL, num + 1);

	// Write back code and name to counters
	counters[num].code = code;
	counters[num].name = event_type_name(code);
}

int main(int argc, char *argv[], char *envp[])
{
	if (argc < 3) {
		fprintf(stderr, "Usage: %s <event1,event2...> <progname> [<args>...]\n", argv[0]);
		exit(1);
	}

	// Set up the event list
	parse_event_list(argv[1], 0);

	// Set up the execution barrier and get the child ready
	setup_barrier();
	spawn_child(argv[2], argv + 2, envp);

	// Configure the perf file descriptors
	configure_main_perf_fd();

	for (size_t i = 0; i < num_counters; i++) {
		configure_raw_counter(i, counters[i].code);
	}

	// Reset and enable events, kick off execution
	ioctl(main_perf_fd, PERF_EVENT_IOC_RESET, PERF_IOC_FLAG_GROUP);
	ioctl(main_perf_fd, PERF_EVENT_IOC_ENABLE, PERF_IOC_FLAG_GROUP);
	pthread_barrier_wait(barrier);

	while (waitpid(child, NULL, WNOHANG) == 0) {
		if (read(main_perf_fd, events, events_size) < 0) {
			die("read");
		}

		// Extract CPU cycles
		for (size_t i = 0; i < events->nr; i++) {
			if (main_perf_id == events->values[i].id) {
				main_perf_val = events->values[i].value;
			}
		}

		scheduler_round(main_perf_val);
	}

	ioctl(main_perf_fd, PERF_EVENT_IOC_DISABLE, PERF_IOC_FLAG_GROUP);

	return 0;
}
