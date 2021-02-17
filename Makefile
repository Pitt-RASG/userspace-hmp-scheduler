CFLAGS	:= -Wall -g3 -O0
LDFLAGS	:= -pthread
TGT	:= report
OBJS	:= events.o report.o

.PHONY: all clean

all: $(TGT)

clean:
	rm -rf $(OBJS) $(OBJS:.o=.d) $(TGT)

-include $(OBJS:.o=.d)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -MMD -o $@

$(TGT): $(OBJS)
	$(CC) $(LDFLAGS) $^ -o $@
