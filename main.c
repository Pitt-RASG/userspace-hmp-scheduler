#define _GNU_SOURCE

#include <pthread.h>
#include <signal.h>
#include <string.h>

#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/prctl.h>
#include <sys/types.h>
#include <sys/wait.h>

#include "events.h"
#include "perf.h"
#include "scheduler.h"

// Barrier
static pthread_barrierattr_t attr;
static pthread_barrier_t *barrier;

// Traced process
static pid_t child;
static useconds_t sleep_duration;

static char tracked_events[] = "cpu_cycles,inst_retired,l2d_cache,l2d_cache_refill,br_mis_pred";

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
		prctl(PR_SET_PDEATHSIG, SIGKILL);

		pthread_barrier_wait(barrier);

		if (execve(pathname, argv, envp) != 0) {
			die("execve");
		}
	}

	transfer_to_little(child);
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
	event_attr.exclude_kernel = 1;
	event_attr.exclude_hv = 1;

	// Install event counter
	counters[num].fd = perf_event_open(&event_attr, child, -1, -1, PERF_FLAG_FD_CLOEXEC);
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
		events_size = sizeof(struct read_format)*num_counters;
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

static void sigchld_handler(int signal)
{
	exit(0);
}

int main(int argc, char *argv[], char *envp[])
{
	if (argc < 2) {
		fprintf(stderr, "Usage: %s <progname> [<args>...]\n", argv[0]);
		exit(1);
	}

	// Spawn predictor program
	spawn_predictor("python3 ./predictor.py");

	// Set up the event list
	parse_event_list(tracked_events, 0);

	// 200 ms
	sleep_duration = 200000;

	// Set up the execution barrier and get the child ready
	setup_barrier();
	spawn_child(argv[1], argv + 1, envp);
	signal(SIGCHLD, sigchld_handler);

	// Configure the perf file descriptors
	for (size_t i = 0; i < num_counters; i++) {
		configure_raw_counter(i, counters[i].code);
	}

	// Kick off execution
	pthread_barrier_wait(barrier);

	while (waitpid(child, NULL, WNOHANG) == 0) {
		for (size_t i = 0; i < num_counters; i++) {
			if (read(counters[i].fd, &events[i], sizeof(struct read_format)) < 0) {
				die("read");
			}
		}

		scheduler_round(child);

		usleep(sleep_duration);
	}

	return 0;
}
