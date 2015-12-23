
CC=gcc
# To run on devrez machines, we need to build static as they use an old GLIBC.
# Building static will trigger a linker warning about getaddrinfo used by libnuma,
# which cannot be static. Safe to ignore.
CFLAGS=-static -O2 -g -D_GNU_SOURCE

cpubench: cpubench.c hsort.c
	$(CC) $(CFLAGS) -o $@ $^ -lpthread -lm -lrt -lnuma

clean:
	@rm -f cpubench
