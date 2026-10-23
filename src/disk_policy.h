/* DBYTEMEM R5 - portable disk thresholds and strict CLI contract. CC0-1.0 */
#ifndef DBMEM_DISK_POLICY_H
#define DBMEM_DISK_POLICY_H
#include <stdint.h>
#include <string.h>

typedef struct dbm_disk_opts {
    uint32_t drives;
    uint32_t warn_percent;
    uint32_t critical_percent;
    uint32_t critical_free_gib;
    int csv;
} dbm_disk_opts;

static inline int dbm_disk_uint(const char *s, uint32_t *out)
{
    uint32_t n = 0u;
    if (!s || !*s || !out) return 0;
    for (; *s; ++s) {
        uint32_t d;
        if (*s < '0' || *s > '9') return 0;
        d = (uint32_t)(*s - '0');
        if (n > (UINT32_MAX - d) / 10u) return 0;
        n = n * 10u + d;
    }
    *out = n;
    return 1;
}

static inline int dbm_disk_letter(const char *s, unsigned int *index)
{
    unsigned char c;
    if (!s || !index || !s[0] || (s[1] != '\0' &&
        (s[1] != ':' || s[2] != '\0'))) return 0;
    c = (unsigned char)s[0];
    if (c >= 'a' && c <= 'z') c = (unsigned char)(c - 'a' + 'A');
    if (c < 'A' || c > 'Z') return 0;
    *index = (unsigned int)(c - 'A');
    return 1;
}

static inline int dbm_disk_parse(int argc, char **argv, dbm_disk_opts *o)
{
    int i;
    int have_warn = 0, have_crit = 0, have_gib = 0, have_csv = 0;
    if (!o || !argv || argc < 1) return 0;
    o->drives = 0u;
    o->warn_percent = 15u;
    o->critical_percent = 10u;
    o->critical_free_gib = 5u;
    o->csv = 0;
    for (i = 1; i < argc; ++i) {
        uint32_t v;
        unsigned int index;
        if (!strcmp(argv[i], "--drive") && i + 1 < argc) {
            if (!dbm_disk_letter(argv[++i], &index) ||
                (o->drives & (UINT32_C(1) << index))) return 0;
            o->drives |= UINT32_C(1) << index;
        } else if (!strcmp(argv[i], "--warn-percent") &&
                   !have_warn && i + 1 < argc) {
            if (!dbm_disk_uint(argv[++i], &v) || v < 1u || v > 99u)
                return 0;
            o->warn_percent = v;
            have_warn = 1;
        } else if (!strcmp(argv[i], "--critical-percent") &&
                   !have_crit && i + 1 < argc) {
            if (!dbm_disk_uint(argv[++i], &v) || v < 1u || v > 98u)
                return 0;
            o->critical_percent = v;
            have_crit = 1;
        } else if (!strcmp(argv[i], "--critical-free-gib") &&
                   !have_gib && i + 1 < argc) {
            if (!dbm_disk_uint(argv[++i], &v) || v > 1024u)
                return 0;
            o->critical_free_gib = v;
            have_gib = 1;
        } else if (!strcmp(argv[i], "--csv") && !have_csv) {
            o->csv = 1;
            have_csv = 1;
        } else {
            return 0;
        }
    }
    return o->critical_percent < o->warn_percent;
}

/* return 1 only when free bytes are strictly below the percentage.
   division before multiplication avoids overflow for UINT64_MAX volumes. */
static inline int dbm_disk_below(uint64_t free_bytes, uint64_t total_bytes,
                                 uint32_t percent)
{
    uint64_t whole = (total_bytes / UINT64_C(100)) * percent;
    uint64_t fraction =
        ((total_bytes % UINT64_C(100)) * percent + 99u) / UINT64_C(100);
    return free_bytes < whole + fraction;
}

/* -1 invalid, 0 ok, 1 warn, 2 critical */
static inline int dbm_disk_rank(uint64_t free_bytes, uint64_t total_bytes,
                                const dbm_disk_opts *o)
{
    if (!o || total_bytes == 0u || free_bytes > total_bytes ||
        o->warn_percent < 1u || o->warn_percent > 99u ||
        o->critical_percent < 1u || o->critical_percent > 98u ||
        o->critical_percent >= o->warn_percent ||
        o->critical_free_gib > 1024u) return -1;
    if (dbm_disk_below(free_bytes, total_bytes, o->critical_percent) ||
        free_bytes < (uint64_t)o->critical_free_gib * UINT64_C(1073741824))
        return 2;
    if (dbm_disk_below(free_bytes, total_bytes, o->warn_percent))
        return 1;
    return 0;
}
#endif
