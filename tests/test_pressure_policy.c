/* portable policy regression for DBYTEMEM R7. CC0-1.0 */
#include <stdio.h>
#include <stdint.h>
#include <limits.h>
#include "../src/pressure_policy.h"
#define REQUIRE(x) do { if (!(x)) { \
    fprintf(stderr, "failed_line=%d expression=%s\n",__LINE__,#x); return 1; \
} } while (0)
static dbm_pressure_sample sample(uint64_t av,uint64_t total,
                                  uint64_t used,uint64_t limit)
{
    dbm_pressure_sample s;
    s.physical_available_bytes=av;
    s.physical_total_bytes=total;
    s.commit_total_pages=used;
    s.commit_limit_pages=limit;
    return s;
}
int main(void)
{
    dbm_pressure_policy p=dbm_pressure_defaults();
    dbm_pressure_sample s;
    unsigned int a,b,c;
    uint64_t x;
    REQUIRE(p.warn_physical_pct==15 && p.critical_physical_pct==7);
    REQUIRE(p.warn_commit_headroom_pct==10 &&
            p.critical_commit_headroom_pct==3);
    s=sample(500,1000,500,1000);
    REQUIRE(dbm_pressure_rank(&p,&s)==0);
    s=sample(150,1000,500,1000);
    REQUIRE(dbm_pressure_rank(&p,&s)==0); /* boundary equality */
    s=sample(149,1000,500,1000);
    REQUIRE(dbm_pressure_rank(&p,&s)==1);
    s=sample(70,1000,500,1000);
    REQUIRE(dbm_pressure_rank(&p,&s)==1);
    s=sample(69,1000,500,1000);
    REQUIRE(dbm_pressure_rank(&p,&s)==2);
    s=sample(500,1000,900,1000);
    REQUIRE(dbm_pressure_rank(&p,&s)==0);
    s=sample(500,1000,901,1000);
    REQUIRE(dbm_pressure_rank(&p,&s)==1);
    s=sample(500,1000,970,1000);
    REQUIRE(dbm_pressure_rank(&p,&s)==1);
    s=sample(500,1000,971,1000);
    REQUIRE(dbm_pressure_rank(&p,&s)==2);
    s=sample(500,1000,1000,1000);
    REQUIRE(dbm_pressure_rank(&p,&s)==2);
    s=sample(0,1000,0,1000);
    REQUIRE(dbm_pressure_rank(&p,&s)==2);
    s=sample(1001,1000,0,1000);
    REQUIRE(dbm_pressure_rank(&p,&s)==-1);
    s=sample(1,0,0,1000);
    REQUIRE(dbm_pressure_rank(&p,&s)==-1);
    s=sample(1,1000,1001,1000);
    REQUIRE(dbm_pressure_rank(&p,&s)==-1);
    s=sample(1,1000,1,0);
    REQUIRE(dbm_pressure_rank(&p,&s)==-1);
    p.critical_physical_pct=15;
    REQUIRE(dbm_pressure_rank(&p,&s)==-1);
    p=dbm_pressure_defaults();
    p.critical_commit_headroom_pct=10;
    REQUIRE(dbm_pressure_rank(&p,&s)==-1);
    p=dbm_pressure_defaults();
    /* no overflow on UINT64_MAX, exact ceil-div threshold */
    REQUIRE(dbm_pressure_below(UINT64_MAX,UINT64_MAX,99)==0);
    REQUIRE(dbm_pressure_below(0,UINT64_MAX,1)==1);
    REQUIRE(dbm_pressure_below(UINT64_MAX/10,UINT64_MAX,10)==1);
    REQUIRE(dbm_pressure_below(UINT64_C(150),UINT64_C(1000),15)==0);
    REQUIRE(dbm_pressure_below(UINT64_C(149),UINT64_C(1000),15)==1);
    REQUIRE(dbm_pressure_below(0,1,1)==1);
    REQUIRE(dbm_pressure_below(1,1,1)==0);
    for(a=0;a<3;a++)for(b=0;b<3;b++)for(c=0;c<3;c++) {
        int rank=dbm_pressure_median((int)a,(int)b,(int)c);
        int sum=((int)a>=rank)+((int)b>=rank)+((int)c>=rank);
        REQUIRE(rank>=0 && rank<=2 && sum>=2);
    }
    REQUIRE(dbm_pressure_median(0,2,0)==0);
    REQUIRE(dbm_pressure_median(2,0,2)==2);
    REQUIRE(dbm_pressure_median(-1,0,1)==-1);
    REQUIRE(dbm_pressure_median(0,3,1)==-1);
    for (x=1;x<200000;x++) {
        uint32_t pct=(uint32_t)((x%99u)+1u);
        uint64_t got=(x/100u)*pct+((x%100u)*pct+99u)/100u;
        REQUIRE(dbm_pressure_below(got,x,pct)==0);
        REQUIRE(dbm_pressure_below(got-1u,x,pct)==1);
    }
    puts("pressure_policy_contract=PASS");
    return 0;
}
