.PHONY: all native clean format test

CFLAGS = -O3
BRAIN_CFLAGS = -std=c99 -Wall -Wextra -Wpedantic

all: play

native: CFLAGS = -O3 -march=native -g -flto
native: LDFLAGS = -flto
native: play

play.o: play.c
	$(CC) $(BRAIN_CFLAGS) $(CFLAGS) -o $@ -c $<

play: play.o
	$(CC) $(LDFLAGS) -o $@ $<

test: play
	./play

format:
	clang-format -i $(wildcard *.c)

clean:
	rm -f play.o play
