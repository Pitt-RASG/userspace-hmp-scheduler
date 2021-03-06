#define _GNU_SOURCE

#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <string.h>
#include <time.h>

#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/prctl.h>
#include <sys/types.h>
#include <sys/stat.h>
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

// Energy monitoring
static int voltage_dev_fd = -1;
static int current_dev_fd = -1;
static int proc_stat_fd = -1;

static const char *voltage_dev = "/sys/class/power_supply/bms/voltage_now";
static const char *current_dev = "/sys/class/power_supply/bms/current_now";

static int64_t voltage_sum = 0;
static int64_t current_sum = 0;
static int64_t core_num = 0;
static int64_t num_samples = 0;

static struct timespec start_time;
static struct timespec end_time;

static void setup_proc_stat(pid_t pid)
{
	char *filename;
	asprintf(&filename, "/proc/%d/stat", pid);

	proc_stat_fd = open(filename, O_RDONLY);
	if (proc_stat_fd < 0) die("open stat");

	free(filename);
}

static void read_proc_stat()
{
	char buf[800];
	int proc;

	lseek(proc_stat_fd, 0, SEEK_SET);
	if (read(proc_stat_fd, buf, sizeof(buf)-1) < 0) die("read stat");

	sscanf(buf,
		"%*d %*s "		// pid, comm
		"%*c "			// state
		"%*d %*d %*d %*d %*d"	// ppid, pgrp, session, tty_nr, tpgid
		"%*u %*u %*u %*u"	// minflt, cminflt, majflt, cmajflt
		"%*u %*u %*d %*d"	// utime, stime, cutime, cstime
		"%*d %*d %*d %*d"	// priority, nice, num_threads, itrealvalue
		"%*u %*u %*d %*u"	// starttime, vsize, rss, rsslim
		"%*u %*u %*u %*u"	// startcode, endcode, startstack, kstkesp
		"%*u %*u %*u %*u"	// kstkeip, signal, blocked, sigignore
		"%*u %*u %*u %*u"	// sigcatch, wchan, nswap, cnswap
		"%*d %*d %d",		// exit_signal, processor.
		&proc);

	core_num += (proc >= 6);
}

static void setup_energy_monitor()
{
	voltage_dev_fd = open(voltage_dev, O_RDONLY);
	if (voltage_dev_fd < 0) die("open voltage");

	current_dev_fd = open(current_dev, O_RDONLY);
	if (current_dev_fd < 0) die("open current");
}

static void read_power()
{
	char buf[22];

	lseek(voltage_dev_fd, 0, SEEK_SET);
	lseek(current_dev_fd, 0, SEEK_SET);

	if (read(voltage_dev_fd, buf, sizeof(buf)) < 0) die("read voltage");
	voltage_sum += atol(buf) / 1000;

	if (read(current_dev_fd, buf, sizeof(buf)) < 0) die("read current");
	current_sum += atol(buf) / 1000;

	num_samples++;
}

static int64_t current_power()
{
	return (voltage_sum/num_samples)*(current_sum/num_samples) / 1000;
}

static void report_energy()
{
	// voltage is reported in millivolts, current in milliamps

	// milliwatts
	int64_t avg_power = (voltage_sum/num_samples) * (current_sum/num_samples) / 1000;

	// milliseconds
	int64_t elapsed_time = ((end_time.tv_sec - start_time.tv_sec)*1000000000 + (end_time.tv_nsec - start_time.tv_nsec)) / 1000000;

	// millijoules
	int64_t energy = avg_power * elapsed_time / 1000;

	printf("%ld mJ\n", energy);
	printf("%ld ms\n", elapsed_time);
}

static void report_core_mix()
{
	float mix = core_num / (float) num_samples;

	printf("%f big, %f LITTLE\n", mix, 1 - mix);
}


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
static void spawn_child(int run_scheduler, const char *pathname, char *const argv[], char *const envp[])
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

	if (run_scheduler) {
		transfer_to_little(child);
	}
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
	clock_gettime(CLOCK_MONOTONIC_RAW, &end_time);
	report_energy();
	report_core_mix();

	exit(0);
}

int main(int argc, char *argv[])
{
	int run_scheduler = 1;

	if (argc < 4) {
		fprintf(stderr, "Usage: %s <run-scheduler> <big-core> <little-core> <program> [<args>...]\n", argv[0]);
	}

	// Set up the event list
	parse_event_list(tracked_events, 0);
	run_scheduler = atoi(argv[1]);
	set_big_core(atoi(argv[2]));
	set_little_core(atoi(argv[3]));

	// 200 ms
	sleep_duration = 200000;

	// Set up the execution barrier and get the child ready
	setup_barrier();
	spawn_child(run_scheduler, argv[4], argv + 4, environ);
	signal(SIGCHLD, sigchld_handler);

	// Configure the perf file descriptors
	for (size_t i = 0; i < num_counters; i++) {
		configure_raw_counter(i, counters[i].code);
	}

	setup_energy_monitor();
	setup_proc_stat(child);

	// Kick off execution
	pthread_barrier_wait(barrier);
	clock_gettime(CLOCK_MONOTONIC_RAW, &start_time);

	while (1) {
		read_power();
		read_proc_stat();

		for (size_t i = 0; i < num_counters; i++) {
			if (read(counters[i].fd, &events[i], sizeof(struct read_format)) < 0) {
				die("read");
			}
		}

		scheduler_round(child, current_power(), run_scheduler);

		usleep(sleep_duration);
	}

	return 0;
}
