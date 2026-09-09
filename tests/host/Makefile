# Host tests for Gazette's portable code.
#
# Constraint 5: the feed parsing, the transliteration and the prefs grammar
# include no system headers at all, so they build with a plain Linux or macOS
# cc and can be exercised without Retro68 or an emulator. Anything that needs
# the Toolbox (store/, ui/, main.cpp) is deliberately absent from SOURCES.

CC     ?= cc
SRC     = ../../src
CFLAGS += -std=c99 -Wall -Wextra -Wno-unused-parameter -I$(SRC) -g

SOURCES = \
	$(SRC)/portable/gazette_portable.c \
	$(SRC)/portable/gazette_url.c \
	$(SRC)/portable/gazette_http.c \
	$(SRC)/prefs/gazette_prefs.c \
	$(SRC)/feeds/gazette_feed_parse.c \
	$(SRC)/feeds/gazette_googlenews.c \
	$(SRC)/feeds/gazette_gnews_topics.c

all: test

run_tests: run_tests.c $(SOURCES)
	$(CC) $(CFLAGS) -o $@ $^

test: run_tests
	./run_tests

clean:
	rm -f run_tests
	rm -rf run_tests.dSYM

.PHONY: all test clean
