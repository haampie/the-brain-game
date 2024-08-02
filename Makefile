.PHONY: all clean  format test

CFLAGS = -O0 -g
BRAIN_CFLAGS = -std=c99 -Wall -Wextra -Wpedantic

all: play

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
