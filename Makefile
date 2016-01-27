
CC=g++
# To run on devrez machines, we need to build static as they use an old GLIBC.
# Building static will trigger a linker warning about getaddrinfo used by libnuma,
# which cannot be static. Safe to ignore.
CFLAGS=-static -O2 -g -D_GNU_SOURCE

all: cpubench jitter_test

cpubench: cpubench.cc hsort.cc util.cc
	$(CC) $(CFLAGS) -o $@ $^ -lpthread -lm -lrt -lnuma

jitter_test: jitter_test.cc util.cc
	$(CC) $(CFLAGS) -o $@ $^ -lpthread -lm -lrt -lnuma

clean:
	@rm -f cpubench jitter_test
