#include "utils/param_file.h"
#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { LINE_CAPACITY = 1024 };
typedef struct KeyBinding {
    const char *key;
    size_t offset;
    int integer;
    int required; /* 1: every file; 2: required if ramp enabled */
} KeyBinding;
#define KEY(name, member, needed) {name, offsetof(SystemConfig, member), 0, needed}
static const KeyBinding bindings[] = {
    KEY("input.p_chamber", boundary.target.p_chamber, 1),
    KEY("input.W_ox", boundary.target.W_ox, 1),
    KEY("input.Wg", boundary.target.Wg, 1),
    KEY("input.p_out", boundary.target.p_out, 1),
    KEY("input.T_load", boundary.target.T_load, 1),
    KEY("initial.theta_mech", initial.theta_mech, 0),
    KEY("initial.omega_mech", initial.omega_mech, 0),
    KEY("initial.p_plenum", initial.p_plenum, 0),
    KEY("initial.p_downstream", initial.p_downstream, 0),
    KEY("shaft.J_sum", shaft.J_sum, 0),
    KEY("shaft.b_sum", shaft.b_sum, 0),
    {"boundary.ramp_enabled", offsetof(SystemConfig, ramp_enabled), 1, 0},
    KEY("boundary.delay_s", boundary.delay_s, 2),
    KEY("boundary.duration_s", boundary.duration_s, 2),
    KEY("boundary.start.p_chamber", boundary.start.p_chamber, 2),
    KEY("boundary.start.W_ox", boundary.start.W_ox, 2),
    KEY("boundary.start.Wg", boundary.start.Wg, 2),
    KEY("boundary.start.p_out", boundary.start.p_out, 2),
    KEY("boundary.start.T_load", boundary.start.T_load, 2)
};
#undef KEY
enum { KEY_COUNT = sizeof(bindings) / sizeof(bindings[0]) };

static int fail(ParamError *error, size_t line, const char *message, const char *key)
{
    if (error) {
        error->line = line;
        (void)snprintf(error->message, sizeof(error->message), "%s%s%s",
                       message, key ? ": " : "", key ? key : "");
    }
    return 0;
}
static char *trim(char *text)
{
    while (isspace((unsigned char)*text)) ++text;
    char *end = text + strlen(text);
    while (end > text && isspace((unsigned char)end[-1])) --end;
    *end = '\0';
    return text;
}
static int number(const char *text, double *value)
{
    char *end;
    errno = 0;
    const double parsed = strtod(text, &end);
    if (end == text || errno == ERANGE || !isfinite(parsed)) return 0;
    while (isspace((unsigned char)*end)) ++end;
    if (*end) return 0;
    *value = parsed;
    return 1;
}
static int assignment(char *line, SystemConfig *config, unsigned char *seen,
                       int reject_duplicate, size_t line_number, ParamError *error)
{
    char *equal = strchr(line, '=');
    if (!equal) return fail(error, line_number, "expected key=value", NULL);
    *equal = '\0';
    const char *key = trim(line);
    const char *value_text = trim(equal + 1);
    for (size_t i = 0; i < KEY_COUNT; ++i) {
        const KeyBinding *binding = &bindings[i];
        if (strcmp(binding->key, key) != 0) continue;
        if (reject_duplicate && seen[i]) return fail(error, line_number, "duplicate key", key);
        double value;
        if (!number(value_text, &value)) return fail(error, line_number, "invalid finite number", key);
        unsigned char *destination = (unsigned char *)config + binding->offset;
        if (binding->integer) {
            if (value != 0.0 && value != 1.0) return fail(error, line_number, "expected 0 or 1", key);
            *(int *)destination = (int)value;
        } else {
            *(double *)destination = value;
        }
        seen[i] = 1;
        return 1;
    }
    return fail(error, line_number, "unsupported key", key);
}

int system_config_validate(const SystemConfig *config, ParamError *error)
{
    if (!config) return fail(error, 0, "null configuration", NULL);
    if (!system_input_valid(&config->boundary.target) ||
        !system_input_valid(&config->boundary.start))
        return fail(error, 0, "inputs require finite values and positive pressures", NULL);
    if (!isfinite(config->initial.theta_mech) || !isfinite(config->initial.omega_mech) ||
        !isfinite(config->initial.p_plenum) || config->initial.p_plenum <= 0.0 ||
        !isfinite(config->initial.p_downstream) || config->initial.p_downstream <= 0.0)
        return fail(error, 0, "initial states require finite values and positive pressures", NULL);
    if (!isfinite(config->shaft.J_sum) || config->shaft.J_sum <= 0.0 ||
        !isfinite(config->shaft.b_sum) || config->shaft.b_sum < 0.0)
        return fail(error, 0, "shaft requires J_sum>0 and b_sum>=0", NULL);
    if ((config->ramp_enabled != 0 && config->ramp_enabled != 1) ||
        !isfinite(config->boundary.delay_s) || config->boundary.delay_s < 0.0 ||
        !isfinite(config->boundary.duration_s) || config->boundary.duration_s < 0.0)
        return fail(error, 0, "ramp requires mode 0/1 and finite nonnegative times", NULL);
    return 1;
}
static int required_keys(const SystemConfig *config, const unsigned char *seen,
                         int require_inputs, ParamError *error)
{
    for (size_t i = 0; i < KEY_COUNT; ++i) {
        if (!seen[i] && ((require_inputs && bindings[i].required == 1) ||
            (config->ramp_enabled && bindings[i].required == 2)))
            return fail(error, 0, "missing required key", bindings[i].key);
    }
    return 1;
}

static int load_file(const char *path, SystemConfig *config, unsigned char *seen,
                     ParamError *error)
{
    FILE *file = fopen(path, "rb");
    if (!file) return fail(error, 0, "cannot open configuration", path);
    char line[LINE_CAPACITY];
    size_t length = 0, line_number = 1;
    int ok = 1, ch;
    while ((ch = fgetc(file)) != EOF) {
        if (ch == 0 || (ch != '\n' && length >= sizeof(line) - 1)) {
            ok = fail(error, line_number, ch == 0 ? "NUL byte in file" : "line too long", NULL);
            break;
        }
        if (ch != '\n') { line[length++] = (char)ch; continue; }
        line[length] = '\0';
        char *comment = strchr(line, '#');
        if (comment) *comment = '\0';
        char *entry = trim(line);
        if (*entry && !assignment(entry, config, seen, 1, line_number, error)) { ok = 0; break; }
        length = 0;
        ++line_number;
    }
    if (ok && ferror(file)) ok = fail(error, line_number, "file read failed", path);
    if (ok && length) {
        line[length] = '\0';
        char *comment = strchr(line, '#');
        if (comment) *comment = '\0';
        char *entry = trim(line);
        if (*entry) ok = assignment(entry, config, seen, 1, line_number, error);
    }
    if (fclose(file) != 0 && ok) ok = fail(error, line_number, "file close failed", path);
    if (ok) ok = required_keys(config, seen, 1, error) && system_config_validate(config, error);
    return ok;
}
int param_file_load(const char *path, SystemConfig *config, ParamError *error)
{
    if (error) *error = (ParamError){0};
    if (!path || !config) return fail(error, 0, "null file/configuration", NULL);
    SystemConfig candidate = *config;
    unsigned char seen[KEY_COUNT] = {0};
    if (!load_file(path, &candidate, seen, error)) return 0;
    *config = candidate;
    return 1;
}

int config_from_cli(int argc, char *const argv[], SystemConfig *out,
                    double *sample_time, ParamError *error)
{
    if (error) *error = (ParamError){0};
    if (!out || !sample_time || !argv || argc < 1) return fail(error, 0, "invalid CLI arguments", NULL);
    SystemConfig candidate = system_config_defaults();
    unsigned char seen[KEY_COUNT] = {0};
    const char *path = NULL;
    double time = 0.0;
    int time_seen = 0;
    for (int i = 1; i < argc; i += 2) {
        if (i + 1 >= argc) return fail(error, 0, "missing option value", argv[i]);
        if (strcmp(argv[i], "--config") == 0) {
            if (path) return fail(error, 0, "multiple --config options", NULL);
            path = argv[i + 1];
        } else if (strcmp(argv[i], "--sample") == 0) {
            if (time_seen++ || !number(argv[i + 1], &time) || time < 0.0)
                return fail(error, 0, "invalid/repeated --sample", NULL);
        } else if (strcmp(argv[i], "--set") != 0) {
            return fail(error, 0, "unsupported option", argv[i]);
        }
    }
    if (path && !load_file(path, &candidate, seen, error)) return 0;
    for (int i = 1; i < argc; i += 2) {
        if (strcmp(argv[i], "--set") != 0) continue;
        char entry[LINE_CAPACITY];
        if (strlen(argv[i + 1]) >= sizeof(entry)) return fail(error, 0, "assignment too long", NULL);
        (void)strcpy(entry, argv[i + 1]);
        if (!assignment(entry, &candidate, seen, 0, 0, error)) return 0;
    }
    if (!required_keys(&candidate, seen, 0, error) || !system_config_validate(&candidate, error)) return 0;
    *out = candidate;
    *sample_time = time;
    return 1;
}
