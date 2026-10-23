/* DBYTEMEM R5 - portable disk policy tests. CC0-1.0 */
#include "../src/disk_policy.h"
#include <stdio.h>
#define CHECK(x) do { if (!(x)) {fprintf(stderr,"FAIL line=%d\n",__LINE__);return 1;} } while(0)

static int parses(int n, char **args, int expected)
{
    dbm_disk_opts o;
    return dbm_disk_parse(n, args, &o) == expected;
}

int main(void)
{
    dbm_disk_opts o;
    uint32_t n = 0u;
    unsigned int d = 0u;
    char *a[] = {"disk"};
    char *b[] = {"disk", "--drive", "C", "--drive", "d:", "--csv",
                 "--warn-percent", "18", "--critical-percent", "8",
                 "--critical-free-gib", "0"};
    char *c[] = {"disk", "--drive", "C", "--drive", "c"};
    char *dargs[] = {"disk", "--drive", "../C"};
    char *e[] = {"disk", "--warn-percent", "10", "--critical-percent", "10"};
    char *f[] = {"disk", "--critical-percent", "16"};
    char *g[] = {"disk", "--warn-percent", "0"};
    char *h[] = {"disk", "--critical-free-gib", "1025"};
    char *i[] = {"disk", "--critical-free-gib", "-1"};
    char *j[] = {"disk", "--critical-free-gib", "4294967296"};
    char *k[] = {"disk", "--warn-percent", "15", "--warn-percent", "17"};
    char *l[] = {"disk", "--csv", "--csv"};
    char *m[] = {"disk", "--drive"};
    char *p[] = {"disk", "C"};
    char *q[] = {"disk", "--critical-percent", "9", "--warn-percent", "10"};
    char *r[] = {"disk", "--critical-free-gib", "0"};
    uint64_t b100 = UINT64_C(100);
    CHECK(dbm_disk_parse(1, a, &o));
    CHECK(o.drives == 0u && o.warn_percent == 15u &&
          o.critical_percent == 10u && o.critical_free_gib == 5u &&
          o.csv == 0);
    CHECK(dbm_disk_parse(12, b, &o));
    CHECK(o.drives == ((UINT32_C(1) << 2u) | (UINT32_C(1) << 3u)));
    CHECK(o.warn_percent == 18u && o.critical_percent == 8u &&
          o.critical_free_gib == 0u && o.csv == 1);
    CHECK(parses(5, c, 0));
    CHECK(parses(3, dargs, 0));
    CHECK(parses(5, e, 0));
    CHECK(parses(3, f, 0));
    CHECK(parses(3, g, 0));
    CHECK(parses(3, h, 0));
    CHECK(parses(3, i, 0));
    CHECK(parses(3, j, 0));
    CHECK(parses(5, k, 0));
    CHECK(parses(3, l, 0));
    CHECK(parses(2, m, 0));
    CHECK(parses(2, p, 0));
    CHECK(parses(5, q, 1));
    CHECK(parses(3, r, 1));
    CHECK(dbm_disk_uint("4294967295", &n) && n == UINT32_MAX);
    CHECK(!dbm_disk_uint("4294967296", &n));
    CHECK(!dbm_disk_uint("+1", &n));
    CHECK(!dbm_disk_uint("", &n));
    CHECK(dbm_disk_letter("Z:", &d) && d == 25u);
    CHECK(!dbm_disk_letter("C:\\", &d));
    CHECK(!dbm_disk_letter("", &d));
    CHECK(!dbm_disk_letter("0", &d));
    CHECK(dbm_disk_parse(1, a, &o));
    o.critical_free_gib = 0u;
    CHECK(dbm_disk_rank(0u, b100, &o) == 2);
    CHECK(dbm_disk_rank(9u, b100, &o) == 2);
    CHECK(dbm_disk_rank(10u, b100, &o) == 1);
    CHECK(dbm_disk_rank(14u, b100, &o) == 1);
    CHECK(dbm_disk_rank(15u, b100, &o) == 0);
    CHECK(dbm_disk_rank(100u, b100, &o) == 0);
    CHECK(dbm_disk_rank(101u, b100, &o) == -1);
    CHECK(dbm_disk_rank(0u, 0u, &o) == -1);
    CHECK(dbm_disk_below(1u, 1u, 99u) == 0);
    CHECK(dbm_disk_below(0u, 1u, 99u) == 1);
    CHECK(dbm_disk_rank(UINT64_MAX, UINT64_MAX, &o) == 0);
    CHECK(dbm_disk_rank(UINT64_MAX / 2u, UINT64_MAX, &o) == 0);
    o.critical_free_gib = 5u;
    CHECK(dbm_disk_rank(UINT64_C(4) << 30u,
                        UINT64_C(20) << 30u, &o) == 2);
    CHECK(dbm_disk_rank(UINT64_C(5) << 30u,
                        UINT64_C(20) << 30u, &o) == 0);
    puts("disk_policy=PASS");
    return 0;
}
