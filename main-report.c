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

static const char *voltage_dev = "/sys/class/power_supply/bms/voltage_now";
static const char *current_dev = "/sys/class/power_supply/bms/current_now";

static uint64_t voltage_sum = 0;
static uint64_t current_sum = 0;
static uint64_t num_samples = 0;

static struct timespec start_time;
static struct timespec end_time;

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
	voltage_sum += atol(buf);

	if (read(current_dev_fd, buf, sizeof(buf)) < 0) die("read current");
	current_sum += atol(buf);

	num_samples++;
}

static void report_energy()
{
	// voltage is reported in microvolts, current in microamps

	// milliwatts
	uint64_t avg_power = (voltage_sum * current_sum) / num_samples / 1000000000;

	// milliseconds
	uint64_t elapsed_time = ((end_time.tv_sec - start_time.tv_sec)*1000000000 + (end_time.tv_nsec - start_time.tv_nsec)) / 1000000;

	// millijoules
	uint64_t energy = avg_power * elapsed_time / 1000000;

	printf("%ld mJ\n", energy);
	printf("%ld ms\n", elapsed_time);
}

static void sigchld_handler(int signal)
{
	clock_gettime(CLOCK_MONOTONIC_RAW, &end_time);
	report_energy();

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

	while (1) {
		read_power();
		usleep(200000);
	}

	return 0;
}
