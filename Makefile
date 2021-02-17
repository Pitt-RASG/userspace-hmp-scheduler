CFLAGS	:= -Wall -g3 -O0
LDFLAGS	:=
TGT	:= report
OBJS	:= report.o

.PHONY: all clean

all: $(TGT)

clean:
	rm -rf $(OBJS) $(OBJS:.o=.d) $(TGT)

-include $(OBJS:.o=.d)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -MMD -o $@

$(TGT): $(OBJS)
	$(CC) $(LDFLAGS) $^ -o $@
