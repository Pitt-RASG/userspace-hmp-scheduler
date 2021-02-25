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

static pid_t predictor_pid;
static int predictor_input_pipe;
static int predictor_output_pipe;

#define NELEM(x) (sizeof(x)/sizeof((x)[0]))

static void send_to_predictor(const char* fmt, ...)
{
	char buffer[512];

	va_list va;
	va_start(va, fmt);
	int count = vsnprintf(buffer, sizeof(buffer) - 1, fmt, va);
	va_end(va);

	if (count < 0) {
		die("vnsprintf");
	}

	buffer[count++] = '\n';
	buffer[count] = '\0';

	int written = write(predictor_input_pipe, buffer, count);
	if (written < 0) die("scheduler write");
	else if (written != count) die("scheduler write");

}

static void recv_from_predictor(const char* fmt, ...)
{
	int count = 0;
	char buffer[512];

	while (count == 0 || buffer[count-1] != '\n') {
		int result = read(predictor_output_pipe, buffer, sizeof(buffer) - count);
		if (result <= 0) die("scheduler read");

		count += result;
		assert(count < (int) sizeof(buffer) - 1);
	}

	va_list va;
	va_start(va, fmt);
	vsscanf(buffer, fmt, va);
	va_end(va);
}

// TODO: use Python FFI
void spawn_predictor(const char *command)
{
	int inpipefd[2];
	int outpipefd[2];

	if (pipe(inpipefd) < 0) die("pipe");
	if (pipe(outpipefd) < 0) die("pipe");

	predictor_pid = fork();
	if (predictor_pid < 0) die("fork");

	if (predictor_pid == 0) {
		dup2(outpipefd[0], STDIN_FILENO);
		dup2(inpipefd[1], STDOUT_FILENO);
		close(outpipefd[1]);
		close(inpipefd[0]);
		
		// receive SIGKILL once the parent process dies
		prctl(PR_SET_PDEATHSIG, SIGKILL);

		// execute predictor script
		execl("/bin/sh", "sh", "-c", command, NULL);
		die("execl");
	} else {
		close(outpipefd[0]);
		close(inpipefd[1]);
		predictor_input_pipe = outpipefd[1];
		predictor_output_pipe = inpipefd[0];
	}
}

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
void scheduler_round(pid_t pid)
{
	uint64_t cpu_cycles, inst_retired, l2d_cache, l2d_cache_refill, br_mis_pred;
	int predicted_phase;

	cpu_cycles       = events->values[0].value;
	inst_retired     = events->values[1].value;
	l2d_cache        = events->values[2].value;
	l2d_cache_refill = events->values[3].value;
	br_mis_pred      = events->values[4].value;

	/*
	for (size_t i = 0; i < num_counters; i++) {
		printf("%ld 0x%lx\n", counters[i].id, counters[i].code);
	}

	printf("---\n");

	for (size_t i = 1; i < events->nr; i++) {
		printf("%ld\n", events->values[i].id);
	}

	die("done");*/

	// Feed the raw data into the predictor model.
	//
	// Change the thread affinity if the predictor thinks a
	// migration is justified.

	// match trained model format
	send_to_predictor("%d,%d,%d,%d,%d,%d", cpu_cycles, inst_retired, l2d_cache, l2d_cache_refill, br_mis_pred, is_little ? 0 : 4);
	recv_from_predictor("%d", &predicted_phase);

	if (predicted_phase >= 5 && !is_little) {
		puts("little transfer");
		transfer_to_little(pid);
	} else if (predicted_phase < 5 && is_little) {
		puts("big transfer");
		transfer_to_big(pid);
	}
}
