/* DBYTEMEM portable deterministic arena tests. CC0-1.0. */
#include "../src/arena.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { \
    fprintf(stderr, "FAIL line=%d expr=%s\n", __LINE__, #x); return 1; \
} } while (0)

static int normal_and_boundaries(void)
{
    unsigned char buffer[260];
    dbm_arena a;
    void *p, *q;
    size_t m;
    memset(buffer, 0xA5, sizeof(buffer));
    CHECK(dbm_arena_init(&a, buffer + 1, 256));
    CHECK(a.capacity == 256 && a.used == 0);
    p = dbm_arena_alloc(&a, 3, 1);
    CHECK(p == buffer + 1);
    m = dbm_arena_mark(&a);
    q = dbm_arena_alloc(&a, 16, 16);
    CHECK(q != NULL && ((uintptr_t)q % 16) == 0);
    CHECK(a.used <= a.capacity && a.high_water == a.used);
    CHECK(dbm_arena_rewind(&a, m));
    CHECK(dbm_arena_alloc(&a, 16, 16) == q);
    CHECK(!dbm_arena_rewind(&a, 257));
    CHECK(dbm_arena_alloc(&a, 512, 1) == NULL);
    CHECK(dbm_arena_alloc(&a, 1, 3) == NULL);
    CHECK(dbm_arena_alloc(&a, 0, 1) == NULL);
    CHECK(a.used <= a.capacity);
    CHECK(dbm_arena_reset(&a) && a.used == 0);
    CHECK(a.high_water >= m);
    CHECK(dbm_arena_alloc(&a, 256, 1) == buffer + 1);
    CHECK(dbm_arena_alloc(&a, 1, 1) == NULL);
    CHECK(buffer[0] == 0xA5 && buffer[257] == 0xA5);
    return 0;
}

static int overflow_and_failure_atomicity(void)
{
    unsigned char buffer[16];
    dbm_arena a;
    size_t b = 71, e = 72, used;
    CHECK(!dbm_arena_init(NULL, buffer, sizeof(buffer)));
    CHECK(!dbm_arena_init(&a, NULL, sizeof(buffer)));
    CHECK(dbm_arena_init(&a, buffer, sizeof(buffer)));
    CHECK(!dbm_arena_plan(&a, SIZE_MAX, 8, &b, &e));
    CHECK(b == 71 && e == 72);
    CHECK(!dbm_arena_plan(&a, 1, 0, &b, &e));
    CHECK(!dbm_arena_plan(&a, 1, 1, NULL, &e));
    CHECK(!dbm_arena_plan(&a, 1, 1, &b, NULL));
    used = a.used;
    CHECK(dbm_arena_alloc(&a, SIZE_MAX, 1) == NULL);
    CHECK(a.used == used && a.high_water == 0);
    CHECK(dbm_arena_alloc(&a, 1, 1) != NULL);
    used = a.used;
    CHECK(!dbm_arena_rewind(&a, used + 1));
    CHECK(a.used == used);
    return 0;
}

int main(void)
{
    if (normal_and_boundaries() || overflow_and_failure_atomicity()) return 1;
    puts("arena_portable=PASS");
    return 0;
}
