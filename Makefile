CFLAGS?=-O2 -g -Werror -Wall -Wextra
LDLIBS+=-lsdrplay_api
CC?=gcc
PROGNAME=dual_tuner_recorder

all: $(PROGNAME)

clean:
	rm -f *.o (PROGNAME)
