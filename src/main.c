#include "build_info.h"
#include "utils/param_file.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv)
{
    if (argc == 2 &&
        (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0)) {
        puts("Usage: main [--config FILE] [--set key=value ...] [--sample SECONDS]\n"
             "ATRcore " ATRCORE_BUILD_STAGE ": inspect boundary input; no integration.\n"
             "Precedence: defaults < file < CLI. See docs/stages/S02/README.md for keys/units.");
        return EXIT_SUCCESS;
    }
    SystemConfig config;
    double time;
    ParamError error;
    if (!config_from_cli(argc, argv, &config, &time, &error)) {
        fprintf(stderr, "Configuration error (line %zu): %s\n", error.line, error.message);
        return EXIT_FAILURE;
    }
    const BoundaryProfile profile = system_config_profile(&config);
    BoundarySnapshot snapshot;
    if (!boundary_profile_sample(&profile, time, &snapshot)) {
        fputs("Boundary sample failed.\n", stderr);
        return EXIT_FAILURE;
    }
    printf("ATRcore " ATRCORE_BUILD_STAGE " boundary sample; no simulation performed.\n"
           "sampled_at_s=%.17g p_chamber_Pa=%.17g W_ox_kg_s=%.17g "
           "Wg_kg_s=%.17g p_out_Pa=%.17g T_load_N_m=%.17g\n",
           snapshot.sampled_at_s, snapshot.input.p_chamber, snapshot.input.W_ox,
           snapshot.input.Wg, snapshot.input.p_out, snapshot.input.T_load);
    return EXIT_SUCCESS;
}
