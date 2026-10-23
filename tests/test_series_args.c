/* DBYTEMEM R4 portable parser contract tests. CC0-1.0 */
#include "../src/series_args.h"
#include <stdio.h>
#define CHECK(x) do { if (!(x)) {fprintf(stderr,"FAIL line=%d\n",__LINE__);return 1;} } while(0)
static int parse(int n, char **v, int expect)
{
    dbm_series_opts o;
    int result = dbm_series_parse(n,v,&o);
    return result == expect;
}
int main(void)
{
    dbm_series_opts o;
    uint32_t n = 0;
    char *a[]={"prog"};
    char *b[]={"prog","--samples","1","--interval-ms","10000","--phase","idle"};
    char *c[]={"prog","--samples","120","--interval-ms","10000"};
    char *d[]={"prog","--samples","0"};
    char *e[]={"prog","--samples","2","--samples","2"};
    char *f[]={"prog","--interval-ms","99"};
    char *g[]={"prog","--interval-ms","10001"};
    char *h[]={"prog","--samples","3","--interval-ms","100","--phase","recovery"};
    char *i[]={"prog","--phase","idle","--phase","load"};
    char *j[]={"prog","--samples","4294967296"};
    char *k[]={"prog","--samples","-1"};
    char *l[]={"prog","--phase","bad"};
    char *m[]={"prog","--samples","120","--interval-ms","2500"};
    char *p[]={"prog","--samples","120","--interval-ms","2600"};
    CHECK(dbm_series_parse(1,a,&o));
    CHECK(o.samples == 3 && o.interval_ms == 1000 &&
          !strcmp(o.phase,"unclassified"));
    CHECK(parse(7,b,1));
    CHECK(parse(5,c,0));
    CHECK(parse(3,d,0));
    CHECK(parse(5,e,0));
    CHECK(parse(3,f,0));
    CHECK(parse(3,g,0));
    CHECK(parse(7,h,1));
    CHECK(parse(5,i,0));
    CHECK(parse(3,j,0));
    CHECK(parse(3,k,0));
    CHECK(parse(3,l,0));
    CHECK(parse(5,m,1));
    CHECK(parse(5,p,0));
    CHECK(!dbm_series_decimal("4294967296",&n));
    CHECK(!dbm_series_decimal("1x",&n));
    CHECK(dbm_series_decimal("120",&n) && n == 120);
    puts("series_args=PASS");
    return 0;
}
