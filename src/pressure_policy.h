/* DBYTEMEM R7 - read-only memory pressure threshold policy. CC0-1.0 */
#ifndef DBMEM_PRESSURE_POLICY_H
#define DBMEM_PRESSURE_POLICY_H
#include <stdint.h>

typedef struct dbm_pressure_sample {
    uint64_t physical_total_bytes;
    uint64_t physical_available_bytes;
    uint64_t commit_total_pages;
    uint64_t commit_limit_pages;
} dbm_pressure_sample;

typedef struct dbm_pressure_policy {
    uint32_t warn_physical_pct;
    uint32_t critical_physical_pct;
    uint32_t warn_commit_headroom_pct;
    uint32_t critical_commit_headroom_pct;
} dbm_pressure_policy;

static inline dbm_pressure_policy dbm_pressure_defaults(void)
{
    dbm_pressure_policy p;
    p.warn_physical_pct = 15u;
    p.critical_physical_pct = 7u;
    p.warn_commit_headroom_pct = 10u;
    p.critical_commit_headroom_pct = 3u;
    return p;
}

/* true iff available / total < percent / 100, without uint64 overflow. */
static inline int dbm_pressure_below(uint64_t available,
                                     uint64_t total, uint32_t pct)
{
    uint64_t limit = (total / UINT64_C(100)) * pct;
    limit += ((total % UINT64_C(100)) * pct + 99u) / UINT64_C(100);
    return available < limit;
}
static inline int dbm_pressure_valid(const dbm_pressure_policy *p,
                                     const dbm_pressure_sample *s)
{
    return p && s &&
           p->warn_physical_pct >= 1u && p->warn_physical_pct <= 99u &&
           p->critical_physical_pct >= 1u &&
           p->critical_physical_pct < p->warn_physical_pct &&
           p->warn_commit_headroom_pct >= 1u &&
           p->warn_commit_headroom_pct <= 99u &&
           p->critical_commit_headroom_pct >= 1u &&
           p->critical_commit_headroom_pct < p->warn_commit_headroom_pct &&
           s->physical_total_bytes > 0u &&
           s->physical_available_bytes <= s->physical_total_bytes &&
           s->commit_limit_pages > 0u &&
           s->commit_total_pages <= s->commit_limit_pages;
}
/* returns -1 invalid, 0 OK, 1 WARN, 2 CRITICAL */
static inline int dbm_pressure_rank(const dbm_pressure_policy *p,
                                    const dbm_pressure_sample *s)
{
    uint64_t remain;
    if (!dbm_pressure_valid(p,s)) return -1;
    remain = s->commit_limit_pages - s->commit_total_pages;
    if (dbm_pressure_below(s->physical_available_bytes,
                           s->physical_total_bytes,p->critical_physical_pct) ||
        dbm_pressure_below(remain,s->commit_limit_pages,
                           p->critical_commit_headroom_pct))
        return 2;
    if (dbm_pressure_below(s->physical_available_bytes,
                           s->physical_total_bytes,p->warn_physical_pct) ||
        dbm_pressure_below(remain,s->commit_limit_pages,
                           p->warn_commit_headroom_pct))
        return 1;
    return 0;
}
/* majority-of-three severity; a one-sample spike never escalates alone. */
static inline int dbm_pressure_median(int a, int b, int c)
{
    if (a < 0 || a > 2 || b < 0 || b > 2 || c < 0 || c > 2)
        return -1;
    if (a > b) { int t=a; a=b; b=t; }
    if (b > c) { int t=b; b=c; c=t; }
    if (a > b) b=a;
    return b;
}
#endif
