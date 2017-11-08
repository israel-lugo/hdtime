
CC ?= gcc
CFLAGS ?= -O2
#CFLAGS ?= -DDEBUG=1 -g -W -Wall
# librt required for clock_gettime and clock_getres, prior to glibc 2.17
LDLIBS = -lrt

hdtime_objs = benchmarks.o cli.o humanize.o

all: hdtime

hdtime: $(hdtime_objs)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LOADLIBES) $(LDLIBS)

clean:
	rm -f hdtime $(hdtime_objs)
