/* DBYTEMEM - Win32 reserve-once, commit-on-demand arena. CC0-1.0. */
#ifndef DBMEM_ARENA_WIN_H
#define DBMEM_ARENA_WIN_H
#ifndef _WIN32
#error arena_win.h requires Win32
#endif
#include <windows.h>
#include "arena.h"

typedef struct dbm_vm_arena {
    dbm_arena region;
    size_t reserved;
    size_t committed;
    size_t page_size;
} dbm_vm_arena;

static inline int dbm_round_page(size_t n, size_t page, size_t *out)
{
    size_t rem;
    if (!page || !out) return 0;
    rem = n % page;
    if (rem && n > SIZE_MAX - (page - rem)) return 0;
    *out = rem ? n + (page - rem) : n;
    return 1;
}

static inline int dbm_vm_init(dbm_vm_arena *v, size_t bytes)
{
    SYSTEM_INFO si;
    size_t rounded;
    void *p;
    if (!v || !bytes) return 0;
    ZeroMemory(v, sizeof(*v));
    GetSystemInfo(&si);
    if (!dbm_round_page(bytes, (size_t)si.dwPageSize, &rounded)) return 0;
    p = VirtualAlloc(NULL, rounded, MEM_RESERVE, PAGE_NOACCESS);
    if (!p) return 0;
    if (!dbm_arena_init(&v->region, p, bytes)) {
        VirtualFree(p, 0, MEM_RELEASE);
        return 0;
    }
    v->reserved = rounded;
    v->page_size = (size_t)si.dwPageSize;
    return 1;
}

static inline void *dbm_vm_alloc(dbm_vm_arena *v, size_t bytes, size_t alignment)
{
    size_t begin, end, need;
    if (!v || !v->page_size ||
        !dbm_arena_plan(&v->region, bytes, alignment, &begin, &end) ||
        !dbm_round_page(end, v->page_size, &need)) return NULL;
    if (need > v->committed) {
        if (!VirtualAlloc(v->region.base + v->committed,
                          need - v->committed, MEM_COMMIT, PAGE_READWRITE))
            return NULL;
        v->committed = need;
    }
    v->region.used = end;
    if (end > v->region.high_water) v->region.high_water = end;
    return v->region.base + begin;
}

/* Reset invalidates all old pointers. Decommit failure leaves metadata unchanged. */
static inline int dbm_vm_reset(dbm_vm_arena *v)
{
    if (!v || !v->region.base) return 0;
    if (v->committed && !VirtualFree(v->region.base, v->committed, MEM_DECOMMIT))
        return 0;
    v->committed = 0;
    v->region.used = 0;
    return 1;
}

/* Release invalidates all old pointers. Failure preserves metadata. */
static inline int dbm_vm_destroy(dbm_vm_arena *v)
{
    if (!v || !v->region.base) return 0;
    if (!VirtualFree(v->region.base, 0, MEM_RELEASE)) return 0;
    ZeroMemory(v, sizeof(*v));
    return 1;
}
#endif
