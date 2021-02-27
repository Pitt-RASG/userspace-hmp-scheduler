#define _GNU_SOURCE

#include <fcntl.h>
#include <signal.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>

#include <sys/types.h>
#include <sys/stat.h>

#define die(msg) do { perror(msg); exit(-1); } while (0);

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

static void sigchld_handler(int signal)
{
	clock_gettime(CLOCK_MONOTONIC_RAW, &end_time);
	report_energy();
	report_core_mix();

	exit(0);
}

int main(int argc, char *argv[], char *envp[])
{
	if (argc < 2) {
		fprintf(stderr, "Usage: %s <progname> [<args>...]\n", argv[0]);
		exit(-1);
	}

	setup_energy_monitor();
	clock_gettime(CLOCK_MONOTONIC_RAW, &start_time);
	signal(SIGCHLD, sigchld_handler);

	pid_t child = fork();

	if (child < 0) die("fork");
	if (child == 0) {
		execve(argv[1], argv + 1, envp);
		die("execve");
	}

	setup_proc_stat(child);

	while (1) {
		read_power();
		read_proc_stat();
		num_samples++;
		usleep(200000);
	}

	return 0;
}
