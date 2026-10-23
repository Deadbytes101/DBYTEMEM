/* DBYTEMEM R2 journal negative tests. CC0-1.0 */
#include "../src/policy_contract.h"
#include <stdio.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"FAIL line=%d %s\n",__LINE__,#x); return 1; } } while (0)
int main(void)
{
    dbm_journal a, b;
    CHECK(sizeof(dbm_journal) == 40);
    CHECK(!dbm_journal_make(NULL, 1, 1, 5, 1));
    CHECK(!dbm_journal_make(&a, 0, 1, 5, 1));
    CHECK(!dbm_journal_make(&a, 1, 0, 5, 1));
    CHECK(!dbm_journal_make(&a, 1, 1, 0, 1));
    CHECK(!dbm_journal_make(&a, 1, 1, 5, 6));
    CHECK(!dbm_journal_make(&a, 1, 1, 5, 5));
    CHECK(dbm_journal_make(&a, 1234, UINT64_C(0x1122334455667788), 5, 2));
    CHECK(dbm_journal_valid(&a));
    CHECK(dbm_identity_matches(&a, 1234, UINT64_C(0x1122334455667788)));
    CHECK(!dbm_identity_matches(&a, 1234, UINT64_C(0x1122334455667789)));
    CHECK(!dbm_identity_matches(&a, 1235, UINT64_C(0x1122334455667788)));
    CHECK(dbm_restore_allowed(&a,1234,a.created_100ns,2));
    CHECK(dbm_restore_allowed(&a,1234,a.created_100ns,5));
    CHECK(!dbm_restore_allowed(&a,1234,a.created_100ns,3));
    CHECK(!dbm_restore_allowed(&a,1234,a.created_100ns+1,2));
    b = a;
    b.checksum ^= 1;
    CHECK(!dbm_journal_valid(&b));
    b = a;
    b.created_100ns++;
    CHECK(!dbm_journal_valid(&b));
    b = a;
    b.reserved = 1;
    CHECK(!dbm_journal_valid(&b));
    b = a;
    b.version++;
    CHECK(!dbm_journal_valid(&b));
    b = a;
    b.tail++;
    CHECK(!dbm_journal_valid(&b));
    puts("policy_contract=PASS");
    return 0;
}
