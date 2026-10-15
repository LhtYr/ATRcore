#include "check_s02.h"
#include "utils/param_file.h"
#include <string.h>

static const char *fixture = "build/s02_params_fixture.cfg";
static const char *inputs[] = {
    "input.p_chamber = 3600000\n", "input.W_ox = 2.05\n", "input.Wg = 5.1\n",
    "input.p_out = 100000\n", "input.T_load = 12\n"
};
static int write_fixture(int omitted, const char *prefix, const char *suffix)
{
    FILE *file = fopen(fixture, "wb");
    if (!file) return 0;
    int ok = fputs(prefix, file) >= 0;
    for (int i = 0; i < 5; ++i) if (i != omitted && fputs(inputs[i], file) < 0) ok = 0;
    if (fputs(suffix, file) < 0) ok = 0;
    if (fclose(file) != 0) ok = 0;
    return ok;
}
static int rejects_file_unchanged(const char *path, const char *diagnostic)
{
    SystemConfig config = system_config_defaults();
    config.initial.omega_mech = 123.0;
    unsigned char before[sizeof(config)];
    memcpy(before, &config, sizeof(config));
    ParamError error;
    const int ok = !param_file_load(path, &config, &error) &&
        !memcmp(before, &config, sizeof(config)) && strstr(error.message, diagnostic);
    printf("  rejection line=%zu message=%s\n", error.line, error.message);
    return ok;
}

int check_params(void)
{
    CheckCount count = {0};
    ParamError error;
    SystemConfig config = system_config_defaults();
    check_assert(&count, param_file_load("config/in_range.cfg", &config, &error) &&
        config.boundary.target.p_chamber == 3600000.0 && config.boundary.target.W_ox == 2.05 &&
        config.boundary.target.Wg == 5.1 && config.boundary.target.p_out == 100000.0 &&
        config.boundary.target.T_load == 0.0, "five file inputs load in declared SI units");
    const char *optional = "initial.theta_mech=1.25\ninitial.omega_mech=-30\n"
        "initial.p_plenum=123\ninitial.p_downstream=456\nshaft.J_sum=0.01\nshaft.b_sum=0\n";
    check_assert(&count, write_fixture(-1, " # comment\r\n\t\n", optional) &&
        param_file_load(fixture, &config, &error) && config.initial.theta_mech == 1.25 &&
        config.initial.omega_mech == -30.0 && config.initial.p_plenum == 123.0 &&
        config.initial.p_downstream == 456.0 && config.shaft.J_sum == 0.01 && config.shaft.b_sum == 0.0,
        "all state/shaft bindings, whitespace/CRLF/comments; low positive pressures not clamped");
    check_assert(&count, write_fixture(4, "", "input.T_load = -7 # final line without newline") &&
        param_file_load(fixture, &config, &error) && config.boundary.target.T_load == -7.0,
        "signed load is retained; EOF without newline");
    for (int omitted = 0; omitted < 5; ++omitted) {
        check_assert(&count, write_fixture(omitted, "", "") &&
            rejects_file_unchanged(fixture, "missing required key"), inputs[omitted]);
    }
    const char *invalid_lines[] = {"input.p_chamber=nan\n", "input.Wg=inf\n",
        "input.W_ox=1e9999\n", "input.Wg=1e-9999\n", "input.Wg=1junk\n",
        "input.Wg=\n", "input.Wg 5\n", "unknown=1\n"};
    for (size_t i = 0; i < sizeof(invalid_lines)/sizeof(invalid_lines[0]); ++i)
        check_assert(&count, write_fixture(-1, invalid_lines[i], "") &&
            rejects_file_unchanged(fixture, ""), invalid_lines[i]);
    check_assert(&count, write_fixture(-1, "", "input.Wg=6\n") &&
        rejects_file_unchanged(fixture, "duplicate key"), "duplicate file keys rejected atomically");
    check_assert(&count, write_fixture(-1, "", "boundary.ramp_enabled=1\n") &&
        rejects_file_unchanged(fixture, "missing required key"), "ramp requires explicit times and five starts");
    check_assert(&count, rejects_file_unchanged("build/no-such-s02-directory/input.cfg", "cannot open"),
        "missing file rejected atomically");
    char long_line[1100];
    memset(long_line, 'a', sizeof(long_line) - 1);
    long_line[sizeof(long_line) - 1] = '\0';
    check_assert(&count, write_fixture(-1, long_line, "") &&
        rejects_file_unchanged(fixture, "line too long"), "oversize line rejected");
    FILE *binary = fopen(fixture, "wb");
    int binary_ok = 0;
    if (binary) {
        binary_ok = fwrite("#x\0hidden", 1, 9, binary) == 9;
        if (fclose(binary) != 0) binary_ok = 0;
    }
    check_assert(&count, binary_ok && rejects_file_unchanged(fixture, "NUL byte"), "embedded NUL rejected");

    char *precedence[] = {"main", "--set", "input.Wg=5.5", "--config", "config/in_range.cfg",
        "--set", "input.Wg=5.7", "--sample", "2"};
    double time = -1.0;
    check_assert(&count, config_from_cli(9, precedence, &config, &time, &error) &&
        config.boundary.target.Wg == 5.7 && config.boundary.target.W_ox == 2.05 && time == 2.0,
        "defaults < file < CLI regardless of argument order; last CLI set wins");
    char *raw[] = {"main", "--set", "input.Wg=-2", "--set", "input.W_ox=0", "--set", "input.T_load=7"};
    check_assert(&count, config_from_cli(7, raw, &config, &time, &error) &&
        config.boundary.target.Wg == -2.0 && config.boundary.target.W_ox == 0.0 &&
        config.boundary.target.T_load == 7.0, "requested flows retained; positive resisting load retained");
    const char *bad_sets[] = {"input.p_chamber=0", "input.p_out=-1", "initial.p_plenum=0",
        "initial.p_downstream=-2", "initial.theta_mech=nan", "initial.omega_mech=inf",
        "shaft.J_sum=0", "shaft.b_sum=-1", "boundary.delay_s=-1", "boundary.duration_s=-1",
        "boundary.ramp_enabled=0.5", "boundary.ramp_enabled=2", "input.Wg=nan",
        "p23_total=1", "T23_total=1", "W23=1", "motor.Rs=1", "pump.rho=1",
        "shaft.J_motor=1", "shaft.J_pump=1", "inverter.f_sw=1",
        "control_dt=1", "control_mode=1", "enable_controller=1", "enable_inverter=1"};
    for (size_t i = 0; i < sizeof(bad_sets)/sizeof(bad_sets[0]); ++i) {
        char *args[] = {"main", "--set", (char *)bad_sets[i]};
        unsigned char before[sizeof(config)];
        memcpy(before, &config, sizeof(config));
        time = 987.0;
        check_assert(&count, !config_from_cli(3, args, &config, &time, &error) &&
            !memcmp(before, &config, sizeof(config)) && time == 987.0, bad_sets[i]);
    }
    char *missing[] = {"main", "--set"};
    char *duplicate[] = {"main", "--config", "config/constant.cfg", "--config", "config/in_range.cfg"};
    char *bad_time[] = {"main", "--sample", "nan"};
    char *negative_time[] = {"main", "--sample", "-1"};
    char *bad_option[] = {"main", "--unknown", "0"};
    check_assert(&count, !config_from_cli(2, missing, &config, &time, &error) &&
        !config_from_cli(5, duplicate, &config, &time, &error) &&
        !config_from_cli(3, bad_time, &config, &time, &error) &&
        !config_from_cli(3, negative_time, &config, &time, &error) &&
        !config_from_cli(3, bad_option, &config, &time, &error), "malformed CLI rejected");
    char *cannot_repair[] = {"main", "--config", (char *)fixture, "--set", "input.Wg=5.1"};
    check_assert(&count, write_fixture(2, "", "") &&
        !config_from_cli(5, cannot_repair, &config, &time, &error), "CLI cannot mask incomplete file");
    check_assert(&count, write_fixture(0, "input.p_chamber=-1\n", "") &&
        rejects_file_unchanged(fixture, "positive pressures"), "file semantic validation is transactional");
    const char *ramp_lines[] = {"boundary.delay_s=1\n", "boundary.duration_s=2\n",
        "boundary.start.p_chamber=2900000\n", "boundary.start.W_ox=1.6\n",
        "boundary.start.Wg=4.1\n", "boundary.start.p_out=100000\n", "boundary.start.T_load=4\n"};
    for (int omitted = 0; omitted < 7; ++omitted) {
        char suffix[512] = "boundary.ramp_enabled=1\n";
        for (int i = 0; i < 7; ++i) if (i != omitted) (void)strcat(suffix, ramp_lines[i]);
        check_assert(&count, write_fixture(-1, "", suffix) &&
            rejects_file_unchanged(fixture, "missing required key"), ramp_lines[omitted]);
    }
    char *cli_ramp[] = {"main", "--set", "boundary.ramp_enabled=1",
        "--set", "boundary.delay_s=1", "--set", "boundary.duration_s=2",
        "--set", "boundary.start.p_chamber=2900000", "--set", "boundary.start.W_ox=1.6",
        "--set", "boundary.start.Wg=4.1", "--set", "boundary.start.p_out=100000",
        "--set", "boundary.start.T_load=4"};
    check_assert(&count, config_from_cli(17, cli_ramp, &config, &time, &error) &&
        config.ramp_enabled == 1 && config.boundary.delay_s == 1 && config.boundary.duration_s == 2 &&
        config.boundary.start.p_chamber == 2900000 && config.boundary.start.W_ox == 1.6 &&
        config.boundary.start.Wg == 4.1 && config.boundary.start.p_out == 100000 &&
        config.boundary.start.T_load == 4, "all ramp bindings via CLI without file");
    check_assert(&count, remove(fixture) == 0, "temporary C fixture removed");
    return check_finish(&count, "params");
}
