#include "scheduler.h"

struct perf_info *counters = NULL;
struct read_format *events = NULL;

size_t num_counters = 0;
size_t events_size = 0;

static void set_counter(struct read_values *restrict v)
{
	for (size_t i = 0; i < num_counters; i++) {
		if (counters[i].id != v->id)
			continue;

		counters[i].val = v->value;

		// [comment this out to stop reporting]
		printf("%s:%lu\t", counters[i].name, counters[i].val);

		return;
	}
}

/**
 * Read performance counter data from the child.
 */
void scheduler_round()
{
	// Adjust this to feed the raw data into the predictor model.
	//
	// Change the thread affinity if the predictor thinks a
	// migration is justified.
	for (size_t i = 1; i < events->nr; i++) {
		set_counter(&events->values[i]);
	}

	// [comment this out to stop reporting]
	printf("\n");

	// Adjust this to sleep for a smaller time slice
	usleep(200000);
}
