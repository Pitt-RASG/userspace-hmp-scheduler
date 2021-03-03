CFLAGS	:= -Wall -g3 -O0 -fPIC
LDFLAGS	:= -pthread
SCHED	:= libschedule.so
REPT	:= report
SOBJS	:= events.o main-scheduler.o scheduler.o
ROBJS	:= main-report.o

.PHONY: all clean

all: $(SCHED) $(REPT)

clean:
	rm -rf $(ROBJS) $(SOBJS) $(ROBJS:.o=.d) $(SOBJS:.o=.d) $(REPT) $(SCHED)

-include $(ROBJS:.o=.d)
-include $(SOBJS:.o=.d)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -MMD -o $@

$(REPT): $(ROBJS)
	$(CC) $(LDFLAGS) $^ -o $@

$(SCHED): $(SOBJS)
	$(CC) $(LDFLAGS) -shared $^ -o $@
