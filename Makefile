.PHONY: all clean  format test

BRAIN_CFLAGS = -std=c99 -Wall -pedantic -O0 -g

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
