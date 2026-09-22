CC ?= gcc
CFLAGS ?= -O2 -Wall -Wextra
cBench: bench.c fb.c
	$(CC) $(CFLAGS) -o cBench bench.c fb.c -lm -lpthread
clean:
	rm -f cBench
