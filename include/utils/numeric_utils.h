#ifndef ATRCORE_NUMERIC_UTILS_H
#define ATRCORE_NUMERIC_UTILS_H
#include <math.h>
static inline double clamp_range(double value, double low, double high)
{
    return fmin(high, fmax(low, value));
}
#endif
