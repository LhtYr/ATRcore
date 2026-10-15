#include "solver/BoundaryProfile.h"
#include "utils/numeric_utils.h"
#include <math.h>

int system_input_valid(const SystemInput *in)
{
    return in && isfinite(in->p_chamber) && in->p_chamber > 0.0 &&
        isfinite(in->W_ox) && isfinite(in->Wg) &&
        isfinite(in->p_out) && in->p_out > 0.0 && isfinite(in->T_load);
}

SystemInput boundary_profile_constant(double t_s, void *ctx)
{
    (void)t_s;
    return *(const SystemInput *)ctx;
}

static double interpolate(double start, double target, double fraction)
{
    /* Weighted form also handles opposite-sign finite endpoints without
     * overflowing the target-start difference. Exact endpoint readback. */
    if (fraction <= 0.0) return start;
    if (fraction >= 1.0) return target;
    return (1.0 - fraction) * start + fraction * target;
}

SystemInput boundary_profile_ramp(double t_s, void *ctx)
{
    const BoundaryRampConfig *ramp = ctx;
    SystemInput in = ramp->start;
    if (t_s < ramp->delay_s) {
        /* Reference delayed supply convention: requested flows are zero.
         * Future GG effective floors still apply; this does not model extinction. */
        in.W_ox = 0.0;
        in.Wg = 0.0;
        return in;
    }
    const double f = ramp->duration_s > 0.0
        ? clamp_range((t_s - ramp->delay_s) / ramp->duration_s, 0.0, 1.0) : 1.0;
#define INTERPOLATE(field) in.field = interpolate(ramp->start.field, ramp->target.field, f)
    INTERPOLATE(p_chamber);
    INTERPOLATE(W_ox);
    INTERPOLATE(Wg);
    INTERPOLATE(p_out);
    INTERPOLATE(T_load);
#undef INTERPOLATE
    return in;
}

BoundaryProfile boundary_profile_make_constant(const SystemInput *input)
{
    const BoundaryProfile profile = {boundary_profile_constant, (void *)input};
    return profile;
}
BoundaryProfile boundary_profile_make_ramp(const BoundaryRampConfig *config)
{
    const BoundaryProfile profile = {boundary_profile_ramp, (void *)config};
    return profile;
}
int boundary_profile_sample(const BoundaryProfile *profile, double t_s,
                            BoundarySnapshot *out)
{
    if (!profile || !profile->evaluate || !profile->ctx || !out ||
        !isfinite(t_s) || t_s < 0.0) return 0;
    if (profile->evaluate == boundary_profile_ramp) {
        const BoundaryRampConfig *ramp = profile->ctx;
        if (!system_input_valid(&ramp->start) || !system_input_valid(&ramp->target) ||
            !isfinite(ramp->delay_s) || ramp->delay_s < 0.0 ||
            !isfinite(ramp->duration_s) || ramp->duration_s < 0.0) return 0;
    }
    const BoundarySnapshot sampled = {profile->evaluate(t_s, profile->ctx), t_s};
    if (!system_input_valid(&sampled.input)) return 0;
    *out = sampled;
    return 1;
}
