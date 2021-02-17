#ifndef _EVENTS_H
#define _EVENTS_H

#include <stdint.h>

uint64_t armv8pmu_event_type_code(const char *name);
const char *armv8pmu_event_type_name(uint64_t code);

#endif // _EVENTS_H
