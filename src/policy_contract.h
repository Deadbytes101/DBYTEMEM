/* DBYTEMEM R2 - portable policy journal contract. CC0-1.0 */
#ifndef DBMEM_POLICY_CONTRACT_H
#define DBMEM_POLICY_CONTRACT_H
#include <stdint.h>
#include <stddef.h>
#include <string.h>

#define DBM_JOURNAL_MAGIC UINT32_C(0x324D4244) /* DBM2 little endian */
#define DBM_JOURNAL_VERSION UINT32_C(1)
#define DBM_JOURNAL_TAIL UINT32_C(0x504F4C59) /* YLOP in little endian */

typedef struct dbm_journal {
    uint32_t magic;
    uint32_t version;
    uint32_t pid;
    uint32_t previous;
    uint32_t requested;
    uint32_t reserved;
    uint64_t created_100ns;
    uint32_t checksum;
    uint32_t tail;
} dbm_journal;

_Static_assert(sizeof(dbm_journal) == 40, "journal layout must stay fixed");
_Static_assert(offsetof(dbm_journal, checksum) == 32, "journal checksum offset changed");

static inline int dbm_priority_valid(uint32_t p) { return p >= 1 && p <= 5; }

static inline uint32_t dbm_checksum(const dbm_journal *j)
{
    const unsigned char *p = (const unsigned char *)j;
    size_t n;
    uint32_t h = UINT32_C(2166136261);
    for (n = 0; n < offsetof(dbm_journal, checksum); ++n) {
        h ^= (uint32_t)p[n];
        h *= UINT32_C(16777619);
    }
    return h;
}

static inline int dbm_journal_make(dbm_journal *j, uint32_t pid,
                                    uint64_t created, uint32_t oldp, uint32_t newp)
{
    if (!j || !pid || !created || !dbm_priority_valid(oldp) ||
        !dbm_priority_valid(newp) || oldp == newp) return 0;
    memset(j, 0, sizeof(*j));
    j->magic = DBM_JOURNAL_MAGIC;
    j->version = DBM_JOURNAL_VERSION;
    j->pid = pid;
    j->previous = oldp;
    j->requested = newp;
    j->created_100ns = created;
    j->tail = DBM_JOURNAL_TAIL;
    j->checksum = dbm_checksum(j);
    return 1;
}

static inline int dbm_journal_valid(const dbm_journal *j)
{
    return j && j->magic == DBM_JOURNAL_MAGIC &&
        j->version == DBM_JOURNAL_VERSION &&
        j->reserved == 0 && j->tail == DBM_JOURNAL_TAIL &&
        j->pid != 0 && j->created_100ns != 0 &&
        dbm_priority_valid(j->previous) &&
        dbm_priority_valid(j->requested) &&
        j->previous != j->requested &&
        j->checksum == dbm_checksum(j);
}

static inline int dbm_identity_matches(const dbm_journal *j,
                                        uint32_t pid, uint64_t created)
{
    return dbm_journal_valid(j) && j->pid == pid && j->created_100ns == created;
}

/* restore only if state is still the one we wrote, or already restored */
static inline int dbm_restore_allowed(const dbm_journal *j,
                                       uint32_t pid, uint64_t created,
                                       uint32_t current)
{
    return dbm_identity_matches(j, pid, created) &&
           (current == j->requested || current == j->previous);
}
#endif
