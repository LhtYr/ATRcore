#ifndef ATRCORE_NUMERIC_UTILS_H
#define ATRCORE_NUMERIC_UTILS_H
#include <math.h>
static inline double clamp_range(double value, double low, double high)
{
    return fmin(high, fmax(low, value));
}

/* Return 1 on success, 0 for null output or nonfinite z (output unchanged).
 * Branches only evaluate exp of a nonpositive argument, avoiding overflow.
 * Finite extreme inputs may round to 0 or 1; this is not a model clamp.
 */
static inline int numeric_sigmoid(double z, double *out)
{
    if (!out || !isfinite(z)) return 0;
    if (z >= 0.0) {
        *out = 1.0 / (1.0 + exp(-z));
    } else {
        const double e = exp(z);
        *out = e / (1.0 + e);
    }
    return 1;
}
#endif
