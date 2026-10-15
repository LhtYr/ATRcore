#include "check_s02.h"
#include "utils/param_file.h"
#include <float.h>
#include <string.h>

static int same_input(SystemInput a, SystemInput b)
{
    return check_near(a.p_chamber, b.p_chamber, 1e-5) &&
        check_near(a.W_ox, b.W_ox, 1e-10) && check_near(a.Wg, b.Wg, 1e-10) &&
        check_near(a.p_out, b.p_out, 1e-5) && check_near(a.T_load, b.T_load, 1e-8);
}
typedef struct CountContext { int calls; SystemInput input; } CountContext;
static SystemInput counted(double time, void *context)
{
    CountContext *count = context;
    ++count->calls;
    SystemInput in = count->input;
    in.T_load += time;
    return in;
}
int check_boundary(void)
{
    CheckCount count = {0};
    SystemConfig config = system_config_defaults();
    ParamError error;
    check_assert(&count, param_file_load("config/delayed_ramp.cfg", &config, &error), "ramp sample file loads");
    BoundaryProfile profile = system_config_profile(&config);
    const double times[] = {0.0, 0.5, 1.0, 2.0, 3.0, 5.0};
    const SystemInput expected[] = {
        {2900000, 0, 0, 100000, 4}, {2900000, 0, 0, 100000, 4},
        {2900000, 1.6, 4.1, 100000, 4}, {3250000, 1.825, 4.6, 110000, 8},
        {3600000, 2.05, 5.1, 120000, 12}, {3600000, 2.05, 5.1, 120000, 12}
    };
    BoundarySnapshot snapshot = {0};
    double max_error[5] = {0};
    for (size_t i = 0; i < sizeof(times)/sizeof(times[0]); ++i) {
        const int ok = boundary_profile_sample(&profile, times[i], &snapshot);
        printf("  t=%.17g input=[%.17g,%.17g,%.17g,%.17g,%.17g]\n", times[i],
            snapshot.input.p_chamber, snapshot.input.W_ox, snapshot.input.Wg,
            snapshot.input.p_out, snapshot.input.T_load);
        check_assert(&count, ok && snapshot.sampled_at_s == times[i] &&
            same_input(snapshot.input, expected[i]), "five-channel independent expected sample");
        const double actual_values[] = {snapshot.input.p_chamber, snapshot.input.W_ox,
            snapshot.input.Wg, snapshot.input.p_out, snapshot.input.T_load};
        const double expected_values[] = {expected[i].p_chamber, expected[i].W_ox,
            expected[i].Wg, expected[i].p_out, expected[i].T_load};
        for (int j = 0; j < 5; ++j)
            max_error[j] = fmax(max_error[j], fabs(actual_values[j] - expected_values[j]));
    }
    printf("  max_abs_error [Pa,kg/s,kg/s,Pa,N m]=[%.17g,%.17g,%.17g,%.17g,%.17g]\n",
        max_error[0], max_error[1], max_error[2], max_error[3], max_error[4]);
    check_assert(&count, boundary_profile_sample(&profile, nextafter(1.0, 0.0), &snapshot) &&
        snapshot.input.W_ox == 0.0 && snapshot.input.Wg == 0.0, "immediately before delay: zero requested flow");
    config.boundary.duration_s = 0.0;
    check_assert(&count, boundary_profile_sample(&profile, 1.0, &snapshot) &&
        same_input(snapshot.input, expected[4]), "zero duration steps at delay");
    config.boundary.duration_s = -1.0;
    check_assert(&count, !boundary_profile_sample(&profile, 1.0, &snapshot),
        "direct invalid ramp duration fails instead of silently becoming a step");
    config.boundary.duration_s = 2.0;
    const SystemInput previous_start = config.boundary.start;
    config.boundary.start = config.boundary.target;
    config.boundary.target = previous_start;
    check_assert(&count, boundary_profile_sample(&profile, 2.0, &snapshot) &&
        same_input(snapshot.input, expected[3]), "descending ramps interpolate all channels");
    config.ramp_enabled = 0;
    profile = system_config_profile(&config);
    check_assert(&count, boundary_profile_sample(&profile, 123.0, &snapshot) &&
        same_input(snapshot.input, previous_start), "constant ignores time and returns all five inputs");
    config.boundary.target.T_load = -DBL_MAX;
    config.boundary.start.T_load = DBL_MAX;
    profile = boundary_profile_make_ramp(&config.boundary);
    check_assert(&count, boundary_profile_sample(&profile, 2.0, &snapshot) && snapshot.input.T_load == 0.0,
        "finite opposite-sign endpoints avoid subtraction overflow");

    CountContext context = {0, {3600000, 2.05, 5.1, 100000, 10}};
    profile = (BoundaryProfile){counted, &context};
    check_assert(&count, boundary_profile_sample(&profile, 2.0, &snapshot), "whole-step snapshot created");
    context.input.Wg = 999.0;
    int held = 1;
    const SystemInput held_expected = {3600000, 2.05, 5.1, 100000, 12};
    for (int stage = 0; stage < 5; ++stage) {
        const SystemInput input_for_stage = snapshot.input;
        held = held && same_input(input_for_stage, held_expected);
    }
    check_assert(&count, held && context.calls == 1 && snapshot.sampled_at_s == 2.0,
        "k1/k2/k3/k4/final consumer contract: one callback and immutable held copy");
    check_assert(&count, boundary_profile_sample(&profile, 2.1, &snapshot) && context.calls == 2 &&
        snapshot.input.Wg == 999.0 && check_near(snapshot.input.T_load, 12.1, 1e-8),
        "next whole step gets new sample");
    unsigned char before[sizeof(snapshot)];
    memcpy(before, &snapshot, sizeof(snapshot));
    check_assert(&count, !boundary_profile_sample(&profile, NAN, &snapshot) &&
        !boundary_profile_sample(&profile, -1, &snapshot) && context.calls == 2 &&
        !memcmp(before, &snapshot, sizeof(snapshot)), "invalid time fails before callback without modifying snapshot");
    context.input.Wg = NAN;
    check_assert(&count, !boundary_profile_sample(&profile, 3, &snapshot) &&
        !memcmp(before, &snapshot, sizeof(snapshot)), "invalid callback output rejected atomically");
    check_assert(&count, !boundary_profile_sample(NULL, 0, &snapshot) &&
        !boundary_profile_sample(&profile, 0, NULL), "null sample arguments rejected");
    puts("Whole-step hold is an interface/consumer test; actual RK4 wiring is reserved for S08.");
    return check_finish(&count, "boundary");
}
