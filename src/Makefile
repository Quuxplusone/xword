CFLAGS ?= -std=c99 -pedantic -O2 -W -Wall -Wextra -Wno-unused-parameter

all: xdict xword-ent xword-fill xword-typeset

xdict: xdict.c xdictlib.c xdictlib.h
	$(CC) $(CFLAGS) -o $@ xdict.c xdictlib.c

xword-ent: xword-ent.c
	$(CC) $(CFLAGS) -o $@ xword-ent.c

xword-fill: dancing.c dancing.h xdictlib.c xdictlib.h xword-fill.c
	$(CC) $(CFLAGS) -o $@ dancing.c xdictlib.c xword-fill.c

xword-typeset: xword-typeset.c
	$(CC) $(CFLAGS) -o $@ xword-typeset.c

clean:
	rm -f *.o xdict xword-ent xword-fill xword-typeset

.PHONY: all clean
