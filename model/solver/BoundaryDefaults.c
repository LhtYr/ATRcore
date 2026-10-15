#include "utils/param_file.h"

SystemConfig system_config_defaults(void)
{
    const SystemInput input = {101325.0, 0.0001, 0.0004, 100000.0, 0.0};
    const SystemConfig config = {
        .initial = {0.0, 0.0, 400000.0, 400000.0},
        .shaft = {0.000774, 0.0048},
        .boundary = {.start = input, .target = input, .delay_s = 0.0, .duration_s = 0.0},
        .ramp_enabled = 0
    };
    return config;
}

BoundaryProfile system_config_profile(const SystemConfig *config)
{
    return config->ramp_enabled ? boundary_profile_make_ramp(&config->boundary)
                               : boundary_profile_make_constant(&config->boundary.target);
}
