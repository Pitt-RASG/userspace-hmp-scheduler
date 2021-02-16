#define _GNU_SOURCE

#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <linux/perf_event.h>
#include <linux/hw_breakpoint.h>

#include <asm/unistd.h>
#include <sys/syscall.h>
#include <sys/ioctl.h>
