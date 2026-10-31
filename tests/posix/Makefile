# The POSIX network path, proved against live servers (CI: net-posix.yml).
#
# GazetteNetTest is the application's own fetch -- src/net/gazette_fetch.c over
# gazette_net.c over Certainly's BSD-sockets transport -- built with a plain
# host cc. It is the same code the Mac OS X 10.4+ build links, so a green run
# on Linux and on a current macOS shows the transport, the entropy pool and
# the compat clock work before they meet Tiger.
#
#   make -C tests/posix && tests/posix/GazetteNetTest <url> ...

CC     ?= cc
TOP     = ../..
CERT    = $(TOP)/third_party/certainly
BEAR    = $(CERT)/bearssl

# _GNU_SOURCE: clock_gettime and friends under -std=c99 on Linux. Darwin
# needs nothing here; the transport sets its own feature macros.
CFLAGS += -std=c99 -O2 -g -D_GNU_SOURCE -DGAZETTE_POSIX=1 -DCERTAINLY_POSIX \
          -I$(TOP)/src -I$(CERT)/include -I$(CERT)/src -I$(BEAR)/inc
LDLIBS += -lpthread

BEARSSL_SRC = $(wildcard $(BEAR)/src/*/*.c) $(BEAR)/src/settings.c

CERTAINLY_SRC = \
	$(CERT)/src/certainly.c \
	$(CERT)/src/transport_posix.c \
	$(CERT)/src/entropy_posix.c \
	$(CERT)/src/ca_roots.c \
	$(CERT)/src/tls13_keysched.c \
	$(CERT)/src/tls13_record.c \
	$(CERT)/src/tls13_handshake.c

ENGINE_SRC = \
	$(TOP)/src/net/gazette_fetch.c \
	$(TOP)/src/net/gazette_net.c \
	$(TOP)/src/net/gazette_net_posix.c \
	$(TOP)/src/feeds/gazette_feed_parse.c \
	$(TOP)/src/extract/gazette_extract.c \
	$(TOP)/src/portable/gazette_portable.c \
	$(TOP)/src/portable/gazette_url.c \
	$(TOP)/src/portable/gazette_http.c

BEARSSL_OBJ   = $(BEARSSL_SRC:.c=.o)
CERTAINLY_OBJ = $(CERTAINLY_SRC:.c=.o)
ENGINE_OBJ    = $(ENGINE_SRC:.c=.o)

all: GazetteNetTest

# BearSSL is third-party and noisy under -Wall; it is built as it ships.
$(BEARSSL_OBJ): CFLAGS += -w -I$(BEAR)/src

GazetteNetTest: gazette_nettest.o $(ENGINE_OBJ) $(CERTAINLY_OBJ) $(BEARSSL_OBJ)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS) $(LDLIBS)

# The transport and entropy sources must stay clean under the strictest flags
# the OS X build will see.
strict:
	$(CC) $(CFLAGS) -Wall -Wextra -Werror -c -o /dev/null $(CERT)/src/transport_posix.c
	$(CC) $(CFLAGS) -Wall -Wextra -Werror -c -o /dev/null $(CERT)/src/entropy_posix.c

clean:
	rm -f GazetteNetTest gazette_nettest.o $(BEARSSL_OBJ) $(CERTAINLY_OBJ) $(ENGINE_OBJ)

.PHONY: all strict clean
