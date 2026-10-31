/*
 * entropy_posix.c - entropy.h on Mac OS X and other BSD-sockets hosts.
 *
 * The same shape as entropy_win32.c, and the same argument: no single source
 * is good, and the pool is what makes them adequate. /dev/urandom is the
 * strong one and exists on every Mac OS X and Linux; it is still not the only
 * source, because a chroot or a descriptor limit can make the open fail, and
 * a pool that depended on it alone would then be seeded by nothing.
 */

#include "entropy.h"

#include <fcntl.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#include <bearssl.h>

#define POOL_BYTES 64

static unsigned char sPool[POOL_BYTES];
static size_t        sAt;
static int           sReady;

/*
 * Fold bytes into the pool rather than overwrite it. Every source is weak on
 * its own; mixing means a later good one cannot be undone by an earlier poor
 * one, and the rotation stops repeated small additions landing in one place.
 */
void entropy_add(const void *data, size_t len)
{
    const unsigned char *p = (const unsigned char *)data;
    size_t i;

    for (i = 0; i < len; i++) {
        sPool[sAt] ^= p[i];
        sAt = (sAt + 1) % POOL_BYTES;
        sPool[sAt] = (unsigned char)(sPool[sAt] * 31u + p[i]);
    }
}

static void add_urandom(void)
{
    unsigned char buf[32];
    ssize_t       n;
    int           fd = open("/dev/urandom", O_RDONLY);

    if (fd < 0)
        return;                 /* one weak source fewer, not a failure */
    n = read(fd, buf, sizeof(buf));
    close(fd);
    if (n > 0)
        entropy_add(buf, (size_t)n);
    memset(buf, 0, sizeof(buf));
}

static void add_timing(void)
{
    struct timeval  tv;
    struct rusage   ru;
    clock_t         c;
    pid_t           pid;
    volatile int    marker;
    void           *where;

    if (gettimeofday(&tv, NULL) == 0)
        entropy_add(&tv, sizeof(tv));

    pid = getpid();
    entropy_add(&pid, sizeof(pid));

    c = clock();
    entropy_add(&c, sizeof(c));

    if (getrusage(RUSAGE_SELF, &ru) == 0)
        entropy_add(&ru, sizeof(ru));

    /* Where the stack happens to sit: ASLR where there is any, else little. */
    where = (void *)&marker;
    entropy_add(&where, sizeof(where));
}

void entropy_init(void)
{
    if (sReady) return;

    add_urandom();
    add_timing();
    sReady = 1;
}

void entropy_seed_engine(br_ssl_engine_context *eng)
{
    unsigned char seed[32];
    br_sha256_context sha;

    entropy_init();

    /*
     * Stirred once more at the point of use, so two connections opened in the
     * same second do not start from the same pool state, and hashed so that
     * what BearSSL receives does not expose the pool itself. Fresh urandom
     * bytes go in every time: it is cheap and it is the best source here.
     */
    add_urandom();
    add_timing();

    br_sha256_init(&sha);
    br_sha256_update(&sha, sPool, sizeof(sPool));
    br_sha256_out(&sha, seed);

    br_ssl_engine_inject_entropy(eng, seed, sizeof(seed));

    /* Do not leave the seed we just handed out sitting in the pool. */
    entropy_add(seed, sizeof(seed));
    memset(seed, 0, sizeof(seed));
}
