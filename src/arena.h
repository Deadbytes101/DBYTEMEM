/* DBYTEMEM - bounded caller-owned arena. CC0-1.0. */
#ifndef DBMEM_ARENA_H
#define DBMEM_ARENA_H
#include <stddef.h>
#include <stdint.h>

typedef struct dbm_arena {
    unsigned char *base;
    size_t capacity;
    size_t used;
    size_t high_water;
} dbm_arena;

static inline int dbm_arena_init(dbm_arena *a, void *memory, size_t bytes)
{
    if (!a || !memory || !bytes) return 0;
    a->base = (unsigned char *)memory;
    a->capacity = bytes;
    a->used = 0;
    a->high_water = 0;
    return 1;
}

/* Plan does not mutate state; failure leaves outputs untouched. */
static inline int dbm_arena_plan(const dbm_arena *a, size_t bytes,
                                 size_t alignment, size_t *begin, size_t *end)
{
    uintptr_t base, current, mask, aligned;
    size_t offset;
    if (!a || !a->base || !begin || !end || !bytes || !alignment ||
        (alignment & (alignment - 1u)) || alignment > UINTPTR_MAX ||
        a->used > a->capacity) return 0;
    base = (uintptr_t)a->base;
    if (a->used > UINTPTR_MAX - base) return 0;
    current = base + a->used;
    mask = (uintptr_t)(alignment - 1u);
    if (current > UINTPTR_MAX - mask) return 0;
    aligned = (current + mask) & ~mask;
    offset = (size_t)(aligned - base);
    if (offset > a->capacity || bytes > a->capacity - offset) return 0;
    *begin = offset;
    *end = offset + bytes;
    return 1;
}

static inline void *dbm_arena_alloc(dbm_arena *a, size_t bytes, size_t alignment)
{
    size_t begin, end;
    if (!dbm_arena_plan(a, bytes, alignment, &begin, &end)) return NULL;
    a->used = end;
    if (end > a->high_water) a->high_water = end;
    return a->base + begin;
}

static inline size_t dbm_arena_mark(const dbm_arena *a)
{
    return a ? a->used : 0;
}

static inline int dbm_arena_rewind(dbm_arena *a, size_t mark)
{
    if (!a || !a->base || mark > a->used) return 0;
    a->used = mark;
    return 1;
}

static inline int dbm_arena_reset(dbm_arena *a)
{
    return dbm_arena_rewind(a, 0);
}
#endif
