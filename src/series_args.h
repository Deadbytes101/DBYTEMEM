/* DBYTEMEM R4 - bounded sampler arguments. CC0-1.0 */
#ifndef DBMEM_SERIES_ARGS_H
#define DBMEM_SERIES_ARGS_H
#include <stdint.h>
#include <string.h>

typedef struct dbm_series_opts {
    uint32_t samples;
    uint32_t interval_ms;
    const char *phase;
} dbm_series_opts;

static inline int dbm_series_decimal(const char *s, uint32_t *v)
{
    uint32_t n = 0;
    if (!s || !*s) return 0;
    while (*s) {
        uint32_t d;
        if (*s < '0' || *s > '9') return 0;
        d = (uint32_t)(*s - '0');
        if (n > (UINT32_MAX - d) / 10u) return 0;
        n = n * 10u + d;
        ++s;
    }
    *v = n;
    return 1;
}

static inline int dbm_series_parse(int argc, char **argv, dbm_series_opts *o)
{
    int i, have_samples = 0, have_interval = 0, have_phase = 0;
    if (!o || argc < 1 || !argv) return 0;
    o->samples = 3u;
    o->interval_ms = 1000u;
    o->phase = "unclassified";
    for (i = 1; i < argc; ++i) {
        uint32_t n;
        if (!strcmp(argv[i], "--samples") && !have_samples && i + 1 < argc) {
            if (!dbm_series_decimal(argv[++i], &n) || n < 1u || n > 120u) return 0;
            o->samples = n;
            have_samples = 1;
        } else if (!strcmp(argv[i], "--interval-ms") && !have_interval && i + 1 < argc) {
            if (!dbm_series_decimal(argv[++i], &n) || n < 100u || n > 10000u) return 0;
            o->interval_ms = n;
            have_interval = 1;
        } else if (!strcmp(argv[i], "--phase") && !have_phase && i + 1 < argc) {
            const char *s = argv[++i];
            if (strcmp(s,"unclassified") && strcmp(s,"idle") &&
                strcmp(s,"load") && strcmp(s,"recovery")) return 0;
            o->phase = s;
            have_phase = 1;
        } else return 0;
    }
    return (uint64_t)(o->samples - 1u) * o->interval_ms <= 300000u;
}
#endif
