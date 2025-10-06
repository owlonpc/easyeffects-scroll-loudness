CPPFLAGS = -D_DEFAULT_SOURCE -D_GNU_SOURCE -D_POSIX_C_SOURCE=200809L
CFLAGS = -std=c99 -pedantic -Wall -Wextra -Os ${CPPFLAGS}
CC = cc

loudness: loudness.c
	${CC} -o $@ ${CFLAGS} ${LDFLAGS} $<

loudness: Makefile config.h

clean:
	rm -f loudness

.PHONY: clean