#ifndef ATRCORE_BOUNDARY_PROFILE_H
#define ATRCORE_BOUNDARY_PROFILE_H
#include "solver/Solver.h"

typedef struct BoundaryProfile {
    SystemInput (*evaluate)(double t_s, void *ctx);
    void *ctx; /* Borrowed; caller keeps context alive and immutable while sampling. */
} BoundaryProfile;
typedef struct BoundaryRampConfig {
    SystemInput start, target;
    double delay_s, duration_s;
} BoundaryRampConfig;
typedef struct BoundarySnapshot {
    SystemInput input;
    double sampled_at_s;
} BoundarySnapshot;

int system_input_valid(const SystemInput *input);
/* Direct callbacks/constructors expect a valid context; sample also checks the
 * built-in ramp context. Prefer sample for error propagation. */
SystemInput boundary_profile_constant(double t_s, void *ctx);
SystemInput boundary_profile_ramp(double t_s, void *ctx);
BoundaryProfile boundary_profile_make_constant(const SystemInput *input);
BoundaryProfile boundary_profile_make_ramp(const BoundaryRampConfig *config);
/* One callback at the whole-step start. Reuse this value at k1..k4 and final.
 * On failure, output is untouched. S08 will connect it to the real solver. */
int boundary_profile_sample(const BoundaryProfile *profile, double t_s,
                            BoundarySnapshot *out);
#endif
