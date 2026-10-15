#ifndef ATRCORE_PARAM_FILE_H
#define ATRCORE_PARAM_FILE_H
#include <stddef.h>
#include "solver/BoundaryProfile.h"

typedef struct SystemConfig {
    SystemState initial;
    ShaftParams shaft;
    BoundaryRampConfig boundary; /* target is also the constant input */
    int ramp_enabled;            /* 0 constant, 1 delayed ramp */
} SystemConfig;
typedef struct ParamError {
    size_t line; /* 1-based file line; 0 for CLI/validation */
    char message[192];
} ParamError;

SystemConfig system_config_defaults(void);
int system_config_validate(const SystemConfig *config, ParamError *error);
/* Explicit files require the five input.* keys. All changes are transactional. */
int param_file_load(const char *path, SystemConfig *config, ParamError *error);
/* defaults -> one optional file -> --set in order, independent of --config position.
 * --sample accepts finite t>=0. Neither output is changed on error. */
int config_from_cli(int argc, char *const argv[], SystemConfig *out,
                    double *sample_time, ParamError *error);
BoundaryProfile system_config_profile(const SystemConfig *config);
#endif
