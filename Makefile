
CC ?= gcc
CFLAGS ?= -O2
#CFLAGS ?= -DDEBUG=1 -g -W -Wall
LDLIBS = -lrt

all: hdtime

hdtime: hdtime.o

clean:
	rm -f hdtime hdtime.o
