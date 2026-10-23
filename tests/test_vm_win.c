/* Windows-only backing/page-state test. CC0-1.0. */
#include "../src/arena_win.h"
#include <stdio.h>
#include <stdint.h>
#define CHECK(x) do { if (!(x)) { \
    fprintf(stderr, "FAIL line=%d expr=%s error=%lu\n", __LINE__, #x, \
        (unsigned long)GetLastError()); return 1; \
} } while (0)

int main(void)
{
    dbm_vm_arena v;
    MEMORY_BASIC_INFORMATION info;
    unsigned char *p;
    CHECK(dbm_vm_init(&v, 8192));
    CHECK(v.committed == 0 && v.region.used == 0);
    CHECK(VirtualQuery(v.region.base, &info, sizeof(info)) == sizeof(info));
    CHECK(info.State == MEM_RESERVE);
    p = (unsigned char *)dbm_vm_alloc(&v, 128, 64);
    CHECK(p != NULL && ((uintptr_t)p % 64) == 0);
    p[0] = 0x5A;
    CHECK(v.committed >= v.page_size);
    CHECK(VirtualQuery(p, &info, sizeof(info)) == sizeof(info));
    CHECK(info.State == MEM_COMMIT && info.Protect == PAGE_READWRITE);
    CHECK(dbm_vm_alloc(&v, 8193, 1) == NULL);
    CHECK(v.region.used == 128);
    CHECK(dbm_vm_reset(&v));
    CHECK(v.region.used == 0 && v.committed == 0);
    CHECK(VirtualQuery(v.region.base, &info, sizeof(info)) == sizeof(info));
    CHECK(info.State == MEM_RESERVE);
    CHECK(dbm_vm_destroy(&v));
    CHECK(v.region.base == NULL);
    puts("arena_win32=PASS");
    return 0;
}
