#ifndef ATRCORE_CHECK_S02_H
#define ATRCORE_CHECK_S02_H
#include <math.h>
#include <stdio.h>
typedef struct CheckCount { int passed, failed; } CheckCount;
static inline void check_assert(CheckCount *count, int condition, const char *name)
{
    if (condition) ++count->passed;
    else ++count->failed;
    printf("%s %s\n", condition ? "PASS" : "FAIL", name);
}
static inline int check_near(double actual, double expected, double atol)
{
    return isfinite(actual) && fabs(actual - expected) <= atol + 1e-9 * fabs(expected);
}
static inline int check_finish(const CheckCount *count, const char *group)
{
    printf("%s %s: passed=%d failed=%d\n", count->failed ? "FAIL" : "PASS",
           group, count->passed, count->failed);
    return count->failed ? 1 : 0;
}
int check_contracts(void);
int check_params(void);
int check_boundary(void);
#endif
