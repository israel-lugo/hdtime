
CC ?= gcc
CFLAGS ?= -O2
#CFLAGS ?= -DDEBUG=1 -g -W -Wall
# librt required for clock_gettime and clock_getres, prior to glibc 2.17
LDLIBS = -lrt

all: hdtime

hdtime: hdtime.o

clean:
	rm -f hdtime hdtime.o
