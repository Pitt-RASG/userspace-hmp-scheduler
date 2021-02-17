#define _GNU_SOURCE

#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <linux/perf_event.h>
#include <linux/hw_breakpoint.h>

#include <asm/unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/wait.h>

#define die(msg) do { perror(msg); abort(); } while (0)

pthread_barrierattr_t attr;
pthread_barrier_t *barrier;

struct read_values {
	uint64_t value;
	uint64_t id;
};

struct read_format {
	uint64_t nr;
	struct read_values values[];
};

static int perf_event_open(struct perf_event_attr *attr, pid_t pid, int cpu, int group_fd, unsigned long flags)
{
	int ret = syscall(__NR_perf_event_open, attr, pid, cpu, group_fd, flags);
	if (ret < 0) die("perf_event_open");
	return ret;
}

static void run_child(const char *pathname, char *const argv[], char *const envp[])
{
	pthread_barrier_wait(barrier);

	// replace this with proper process spawn
	// execve(pathname, argv, envp);

	volatile size_t i = 0;

	for (; i < 10000000000L; i++)
		;

	exit(0);
}

int main(int argc, char *argv[], char *envp[])
{
	struct perf_event_attr event_attr;
	int fd1, fd2;
	uint64_t id1, id2;
	uint64_t val1, val2;
	char buf[4096];
	struct read_format *events = (struct read_format *) buf;
	pid_t child;

	barrier = mmap(NULL, sizeof(pthread_barrier_t), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
	if (barrier == MAP_FAILED) die("mmap");

	pthread_barrierattr_init(&attr);
	pthread_barrierattr_setpshared(&attr, PTHREAD_PROCESS_SHARED);
	pthread_barrier_init(barrier, &attr, 2);

	child = fork();
	if (child < 0) die("fork");
	if (child == 0) run_child(NULL, argv, envp);

	memset(&event_attr, 0, sizeof(struct perf_event_attr));
	event_attr.type = PERF_TYPE_HARDWARE;
	event_attr.size = sizeof(struct perf_event_attr);
	event_attr.config = PERF_COUNT_HW_CPU_CYCLES;
	event_attr.disabled = 1;
	event_attr.exclude_kernel = 1;
	event_attr.exclude_hv = 1;
	event_attr.read_format = PERF_FORMAT_GROUP | PERF_FORMAT_ID;
	fd1 = perf_event_open(&event_attr, child, -1, -1, PERF_FLAG_FD_CLOEXEC);
	ioctl(fd1, PERF_EVENT_IOC_ID, &id1);

	memset(&event_attr, 0, sizeof(struct perf_event_attr));
	event_attr.type = PERF_TYPE_SOFTWARE;
	event_attr.size = sizeof(struct perf_event_attr);
	event_attr.config = PERF_COUNT_SW_PAGE_FAULTS;
	event_attr.disabled = 1;
	event_attr.exclude_kernel = 1;
	event_attr.exclude_hv = 1;
	event_attr.read_format = PERF_FORMAT_GROUP | PERF_FORMAT_ID;
	fd2 = perf_event_open(&event_attr, child, -1, fd1, PERF_FLAG_FD_CLOEXEC);
	ioctl(fd2, PERF_EVENT_IOC_ID, &id2);

	ioctl(fd1, PERF_EVENT_IOC_RESET, PERF_IOC_FLAG_GROUP);
	ioctl(fd1, PERF_EVENT_IOC_ENABLE, PERF_IOC_FLAG_GROUP);
	pthread_barrier_wait(barrier);

	while (waitpid(child, NULL, WNOHANG) == 0) {
		read(fd1, buf, sizeof(buf));
		for (size_t i = 0; i < events->nr; i++) {
			if (events->values[i].id == id1) {
				val1 = events->values[i].value;
			} else if (events->values[i].id == id2) {
				val2 = events->values[i].value;
			}
		}

		printf("cpu:%lu pf:%lu\n", val1, val2);
		sleep(1);
	}

	ioctl(fd1, PERF_EVENT_IOC_DISABLE, PERF_IOC_FLAG_GROUP);

	return 0;
}
