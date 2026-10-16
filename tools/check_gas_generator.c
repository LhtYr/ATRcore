#include "char_gas_generator.h"
#include "check_s02.h"
#include "check_s04.h"
#include "component/GasGenerator.h"

#include <float.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

/* Reference rows are parsed separately from a blueprint-derived CSV; never
 * include the production coefficient table or call the production sigmoid. */
typedef struct RefRow { double beta, bias, a[3]; } RefRow;
typedef struct Reference {
    long double n[3], z[50], h[50], sum, temperature;
    double old_h[50], old_sum, old_temperature;
} Reference;
typedef struct Evidence {
    FILE *cases, *nodes;
    int io_ok, samples, invalid_temperature;
    double max_n, max_z, max_h, max_sum, max_T, max_mass;
    double max_legacy_h, max_legacy_T;
} Evidence;

static int read_reference(const char *path, RefRow rows[50])
{
    FILE *f = fopen(path, "r");
    if (!f) return 0;
    char line[512];
    int ok = fgets(line, sizeof line, f) && !strcmp(line, "i,beta,bias,A0_ox,A1_fuel,A2_pressure\n");
    for (int i = 0; i < 50 && ok; ++i) {
        int index = -1, end = 0;
        ok = fgets(line, sizeof line, f) &&
            sscanf(line, "%d,%lf,%lf,%lf,%lf,%lf%n", &index, &rows[i].beta, &rows[i].bias,
                   &rows[i].a[0], &rows[i].a[1], &rows[i].a[2], &end) == 6;
        if (ok) ok = index == i && !strcmp(line + end, "\n") &&
            isfinite(rows[i].beta) && isfinite(rows[i].bias) &&
            isfinite(rows[i].a[0]) && isfinite(rows[i].a[1]) && isfinite(rows[i].a[2]);
    }
    if (ok) ok = !fgets(line, sizeof line, f) && !ferror(f);
    if (fclose(f) != 0) ok = 0;
    return ok;
}

static Reference reference_eval(const RefRow rows[50], double pressure, double ox, double fuel)
{
    Reference r = {0};
    /* Decimal long-double constants independently implement the frozen math. */
    const long double wo = ox < 2e-5 ? 2e-5L : (long double)ox;
    const long double wf = fuel < 1e-5 ? 1e-5L : (long double)fuel;
    r.n[0] = (wo - 1.6L) / 0.9L;
    r.n[1] = (wf - 4.1L) / 2.0L;
    r.n[2] = ((long double)pressure / 1000000.0L - 2.9L) / 1.4L;
    for (int j = 0; j < 50; ++j) {
        r.z[j] = (long double)rows[j].bias;
        for (int k = 0; k < 3; ++k) r.z[j] += (long double)rows[j].a[k] * r.n[k];
        const long double t = expl(-fabsl(r.z[j]));
        r.h[j] = r.z[j] < 0 ? t / (1.0L + t) : 1.0L / (1.0L + t);
        r.sum += (long double)rows[j].beta * r.h[j];
        r.old_h[j] = 1.0 / (1.0 + reference_exp(-(double)r.z[j]));
        r.old_sum += rows[j].beta * r.old_h[j];
    }
    r.temperature = 1021.1L + 202.9L * r.sum;
    r.old_temperature = 1021.1 + (1224.0 - 1021.1) * r.old_sum;
    return r;
}

static int near_ld(double value, long double reference, double atol)
{
    return isfinite(value) && isfinite(reference) &&
           fabsl((long double)value - reference) <= atol + 1e-9L * fabsl(reference);
}

static void check_case(CheckCount *count, Evidence *log, const RefRow rows[50],
                       const char *name, double pressure, double ox, double fuel)
{
    const Reference r = reference_eval(rows, pressure, ox, fuel);
    const GasGeneratorParams params_before = gas_generator_params;
    unsigned char view_before[sizeof gas_generator_view];
    memcpy(view_before, &gas_generator_view, sizeof view_before);
    const GasGeneratorEval e = gas_generator_evaluate(&gas_generator_params, pressure, ox, fuel);
    const GasGeneratorEval again = gas_generator_evaluate(&gas_generator_params, pressure, ox, fuel);
    char label[160];
    snprintf(label, sizeof label, "%s pure evaluation (params/View unchanged)", name);
    check_assert(count, !memcmp(&params_before, &gas_generator_params, sizeof params_before) &&
                 !memcmp(view_before, &gas_generator_view, sizeof view_before), label);
    const int expected_ok = isfinite(r.temperature) && r.temperature > 0.0L;
    snprintf(label, sizeof label, "%s explicit status (positive finite T only)", name);
    check_assert(count, e.status == (expected_ok ? GG_OK : GG_INVALID_OUTPUT), label);
    ++log->samples;
    if (!expected_ok) ++log->invalid_temperature;
    int n_ok = near_ld(e.nomO2, r.n[0], 1e-12) && near_ld(e.nomWf, r.n[1], 1e-12) &&
               near_ld(e.nomConP, r.n[2], 1e-12);
    log->max_n = fmax(log->max_n, (double)fabsl(e.nomO2 - r.n[0]));
    log->max_n = fmax(log->max_n, (double)fabsl(e.nomWf - r.n[1]));
    log->max_n = fmax(log->max_n, (double)fabsl(e.nomConP - r.n[2]));
    int nodes_ok = 1, repeated = e.status == again.status && e.flags == again.flags;
    long double cumulative = 0.0L;
    double actual_cumulative = 0.0, max_z = 0.0, max_h = 0.0, max_old = 0.0;
    for (int i = 0; i < 50; ++i) {
        nodes_ok &= near_ld(e.preactivation[i], r.z[i], 1e-12) && near_ld(e.activation[i], r.h[i], 1e-12);
        repeated &= e.preactivation[i] == again.preactivation[i] && e.activation[i] == again.activation[i];
        max_z = fmax(max_z, (double)fabsl(e.preactivation[i] - r.z[i]));
        max_h = fmax(max_h, (double)fabsl(e.activation[i] - r.h[i]));
        max_old = fmax(max_old, fabs(e.activation[i] - r.old_h[i]));
        cumulative += (long double)rows[i].beta * r.h[i];
        actual_cumulative += rows[i].beta * e.activation[i];
        log->io_ok &= fprintf(log->nodes,
            "%s,%d,%.17g,%.17g,%.17g,%.17g,%.17g,%.17g,%.17g,%.17g\n",
            name, i, e.preactivation[i], (double)r.z[i], e.activation[i], (double)r.h[i], r.old_h[i],
            rows[i].beta * e.activation[i], actual_cumulative, (double)cumulative) >= 0;
    }
    log->max_z = fmax(log->max_z, max_z);
    log->max_h = fmax(log->max_h, max_h);
    log->max_sum = fmax(log->max_sum, (double)fabsl(e.nomConT - r.sum));
    log->max_T = fmax(log->max_T, (double)fabsl(e.T4_total - r.temperature));
    snprintf(label, sizeof label, "%s normalization + all 50 z/H + sum + temperature", name);
    check_assert(count, n_ok && nodes_ok && near_ld(e.nomConT, r.sum, 1e-12) &&
                 near_ld(e.T4_total, r.temperature, 1e-7), label);
    repeated &= e.nomConT == again.nomConT && e.T4_total == again.T4_total &&
                e.W4 == again.W4 && e.P4_total == again.P4_total;
    snprintf(label, sizeof label, "%s repeat is deterministic, no accumulated fuel", name);
    check_assert(count, repeated, label);
    const double wo = ox < 2e-5 ? 2e-5 : ox;
    const double wf = fuel < 1e-5 ? 1e-5 : fuel;
    unsigned flags = (ox < 2e-5 ? OX_FLOORED : 0u) | (fuel < 1e-5 ? FUEL_FLOORED : 0u);
    /* Test range on the effective dimensional inputs, independent of production normalization. */
    const int outside = wo < 1.6 || wo > 2.5 || wf < 4.1 || wf > 6.1 || pressure < 2.9e6 || pressure > 4.3e6;
    if (outside) flags |= INPUT_OUTSIDE_NORMALIZATION_RANGE;
    if (r.temperature < 1021.1L || r.temperature > 1224.0L) flags |= OUTPUT_OUTSIDE_TEMPERATURE_SCALE;
    if (expected_ok) {
        flags |= SOURCE_CP_DIFFERS_FROM_TURBINE_CP;
    }
    snprintf(label, sizeof label, "%s requested/effective and protection/extrapolation flags", name);
    check_assert(count, e.p_chamber == pressure && e.W_ox_requested == ox && e.Wg_requested == fuel &&
                 e.W_ox_effective == wo && e.Wg_effective == wf && e.flags == flags, label);
    if (expected_ok) {
        const double mass_error = fabs(e.W4 - (wo + wf)) / fmax(1.0, fabs(wo + wf));
        log->max_mass = fmax(log->max_mass, mass_error);
        snprintf(label, sizeof label, "%s outlet mass/pressure/static-temperature/ratios and dual Cp", name);
        check_assert(count, mass_error <= 1e-10 && e.P4_total == pressure && e.P4_static == pressure &&
            near_ld(e.T4_static, 0.81L * r.temperature, 1e-7) &&
            near_ld(e.ox_fuel_ratio, (long double)wo / wf, 1e-12) &&
            near_ld(e.fuel_ox_ratio, (long double)wf / wo, 1e-12) &&
            e.properties.cp_source == 2860.0 && e.properties.gamma_for_turbine == 1.18 &&
            e.properties.R_for_turbine == 380.0 && near_ld(e.properties.cp_for_turbine, 22420.0L / 9.0L, 1e-9) &&
            e.properties.property_source == TURBINE_CALIBRATED, label);
    }
    const double old_T_error = fabs(e.T4_total - r.old_temperature);
    if (!outside) {
        double weight_abs = 0.0;
        for (int i = 0; i < 50; ++i) weight_abs += fabs(rows[i].beta);
        const double bound = 202.9 * weight_abs * 4e-15 + 1e-9;
        snprintf(label, sizeof label, "%s C_exp difference within separately derived budget", name);
        check_assert(count, max_old <= 4e-15 && old_T_error <= bound, label);
        log->max_legacy_h = fmax(log->max_legacy_h, max_old);
        log->max_legacy_T = fmax(log->max_legacy_T, old_T_error);
    }
    const double floor_added = (wo - ox) + (wf - fuel);
    log->io_ok &= fprintf(log->cases,
        "%s,%.17g,%.17g,%.17g,%d,%u,%.17g,%.17g,%.17g,%.17g,%.17g,%.17g,%.17g,%.17g,%.17g,%.17g,%.17g,%.17g,%.17g\n",
        name, pressure, ox, fuel, (int)e.status, e.flags, wo, wf, e.W4, floor_added,
        e.nomConT, (double)r.sum, e.T4_total, (double)r.temperature, r.old_temperature, max_z, max_h, max_old, old_T_error) >= 0;
}

static void check_failures(CheckCount *count)
{
    const double bad[] = {NAN, INFINITY, -INFINITY};
    for (int slot = 0; slot < 3; ++slot) for (int i = 0; i < 3; ++i) {
        double input[] = {3.6e6, 2.05, 5.1};
        input[slot] = bad[i];
        check_assert(count, gas_generator_evaluate(&gas_generator_params, input[0], input[1], input[2]).status == GG_INVALID_INPUT,
                     "nonfinite input rejected before floors");
    }
    check_assert(count, gas_generator_evaluate(&gas_generator_params, 0.0, 2.05, 5.1).status == GG_INVALID_INPUT,
                 "zero pressure rejected");
    check_assert(count, gas_generator_evaluate(&gas_generator_params, -1.0, 2.05, 5.1).status == GG_INVALID_INPUT,
                 "negative pressure rejected");
    check_assert(count, gas_generator_evaluate(NULL, 3.6e6, 2.05, 5.1).status == GG_INVALID_PARAMS,
                 "null params rejected");
    check_assert(count, gas_generator_evaluate(&gas_generator_params, 3.6e6, DBL_MAX, DBL_MAX).status == GG_INVALID_OUTPUT,
                 "finite inputs with overflowing normalized output fail explicitly");
    const size_t offsets[] = {
        offsetof(GasGeneratorParams, ox_min), offsetof(GasGeneratorParams, ox_max),
        offsetof(GasGeneratorParams, fuel_min), offsetof(GasGeneratorParams, fuel_max),
        offsetof(GasGeneratorParams, pressure_min_mpa), offsetof(GasGeneratorParams, pressure_max_mpa),
        offsetof(GasGeneratorParams, pressure_scale_pa), offsetof(GasGeneratorParams, temperature_min),
        offsetof(GasGeneratorParams, temperature_max), offsetof(GasGeneratorParams, fuel_floor),
        offsetof(GasGeneratorParams, ox_floor), offsetof(GasGeneratorParams, cp_source),
        offsetof(GasGeneratorParams, temperature_scale), offsetof(GasGeneratorParams, static_temperature_ratio),
        offsetof(GasGeneratorParams, legacy_fuel_calibration)
    };
    for (size_t i = 0; i < sizeof offsets / sizeof offsets[0]; ++i) {
        GasGeneratorParams p = gas_generator_params;
        const double invalid = NAN;
        memcpy((unsigned char *)&p + offsets[i], &invalid, sizeof invalid);
        check_assert(count, gas_generator_evaluate(&p, 3.6e6, 2.05, 5.1).status == GG_INVALID_PARAMS,
                     "every scalar parameter rejects NaN");
    }
    GasGeneratorParams p = gas_generator_params;
    p.ox_max = p.ox_min;
    check_assert(count, gas_generator_validate_params(&p) == GG_INVALID_PARAMS, "zero normalization span rejected");
    p = gas_generator_params; p.pressure_scale_pa = 0.0;
    check_assert(count, gas_generator_validate_params(&p) == GG_INVALID_PARAMS, "zero pressure divisor rejected");
    p = gas_generator_params; p.hidden_count = 49;
    check_assert(count, gas_generator_validate_params(&p) == GG_INVALID_PARAMS, "hidden count must be 50");
    p = gas_generator_params; p.input_weight = NULL;
    check_assert(count, gas_generator_validate_params(&p) == GG_INVALID_PARAMS, "input weights cannot be replaced");
    p = gas_generator_params; p.hidden_bias = NULL;
    check_assert(count, gas_generator_validate_params(&p) == GG_INVALID_PARAMS, "hidden biases cannot be replaced");
    p = gas_generator_params; p.output_weight = NULL;
    check_assert(count, gas_generator_validate_params(&p) == GG_INVALID_PARAMS, "output weights cannot be replaced");
    p = gas_generator_params; p.property_mode = GAS_PROPERTY_UNSET;
    check_assert(count, gas_generator_validate_params(&p) == GG_INVALID_PARAMS, "property mode must be calibrated");
    p = gas_generator_params; p.temperature_scale = 0.866;
    check_assert(count, gas_generator_validate_params(&p) == GG_INVALID_PARAMS, "commented 0.866 multiplier cannot be enabled");
    p = gas_generator_params; p.cp_source = 2516.0;
    check_assert(count, gas_generator_validate_params(&p) == GG_INVALID_PARAMS, "commented Cp 2516 cannot be enabled");
    p = gas_generator_params; p.legacy_fuel_calibration = 0.01;
    const GasGeneratorEval e = gas_generator_evaluate(&gas_generator_params, 3.6e6, 2.05, 5.1);
    const GasGeneratorEval metadata = gas_generator_evaluate(&p, 3.6e6, 2.05, 5.1);
    check_assert(count, metadata.status == GG_OK && metadata.T4_total == e.T4_total && metadata.W4 == e.W4,
                 "legacy calibration metadata never limits fuel or changes temperature");

    GasGeneratorView v = {0};
    check_assert(count, gas_generator_apply_state(&v, &e) == GG_OK && v.T4_total == e.T4_total &&
                 v.W4 == e.W4 && v.nomConT == e.nomConT && v.flags == e.flags &&
                 !memcmp(v.activation, e.activation, sizeof e.activation), "apply copies completed final snapshot");
    unsigned char before[sizeof v];
    memcpy(before, &v, sizeof v);
    const GasGeneratorEval stage = gas_generator_evaluate(&gas_generator_params, 2.9e6, 1.6, 4.1);
    check_assert(count, stage.status == GG_OK && !memcmp(before, &v, sizeof v), "intermediate evaluation cannot update View");
    check_assert(count, gas_generator_apply_state(&v, &e) == GG_OK && v.W4 == e.W4, "repeated apply does not add fuel");
    GasGeneratorEval invalid = e; invalid.status = GG_INVALID_OUTPUT;
    check_assert(count, gas_generator_apply_state(&v, &invalid) == GG_INVALID_OUTPUT && !memcmp(before, &v, sizeof v),
                 "failed Eval cannot overwrite View");
    invalid = e; invalid.T4_total = NAN;
    check_assert(count, gas_generator_apply_state(&v, &invalid) == GG_INVALID_OUTPUT && !memcmp(before, &v, sizeof v),
                 "nonfinite forged success cannot overwrite View");
    check_assert(count, gas_generator_apply_state(NULL, &e) == GG_INVALID_INPUT &&
                 gas_generator_apply_state(&v, NULL) == GG_INVALID_INPUT, "apply null arguments rejected");
}

/* Parse the emitted CSV back in C, including the independently computed oracle
 * columns. Explicit double serialization avoids Windows CRT %Lg differences. */
static int check_report_readback(const char *path, const char *node_path, const RefRow rows[50], int expected_rows)
{
    FILE *f = fopen(path, "r");
    FILE *nodes = fopen(node_path, "r");
    if (!f || !nodes) {
        if (f) fclose(f);
        if (nodes) fclose(nodes);
        return 0;
    }
    char line[2048];
    char node_line[2048];
    int ok = fgets(line, sizeof line, f) && fgets(node_line, sizeof node_line, nodes), count = 0;
    while (ok && fgets(line, sizeof line, f)) {
        char *cursor = strchr(line, ',');
        double v[18];
        if (!cursor) { ok = 0; break; }
        ++cursor;
        for (int j = 0; j < 18 && ok; ++j) {
            char *end;
            v[j] = strtod(cursor, &end);
            ok = end != cursor && isfinite(v[j]) &&
                 (j == 17 ? !strcmp(end, "\n") : *end == ',');
            cursor = end + 1;
        }
        if (!ok) break;
        const Reference ref = reference_eval(rows, v[0], v[1], v[2]);
        ok = near_ld(v[10], ref.sum, 1e-12) && near_ld(v[12], ref.temperature, 1e-7) &&
             fabs(v[13] - ref.old_temperature) <= 1e-7 &&
             near_ld(v[9], ref.sum, 1e-12) && near_ld(v[11], ref.temperature, 1e-7);
        const size_t name_length = (size_t)(strchr(line, ',') - line);
        long double sum = 0.0L;
        for (int i = 0; i < 50 && ok; ++i) {
            ok = fgets(node_line, sizeof node_line, nodes) &&
                 !strncmp(line, node_line, name_length) && node_line[name_length] == ',';
            if (!ok) break;
            char *part = node_line + name_length + 1;
            double n[9];
            for (int k = 0; k < 9 && ok; ++k) {
                char *end;
                n[k] = strtod(part, &end);
                ok = end != part && isfinite(n[k]) && (k == 8 ? !strcmp(end, "\n") : *end == ',');
                part = end + 1;
            }
            if (!ok) break;
            sum += rows[i].beta * ref.h[i];
            ok = n[0] == i && near_ld(n[1], ref.z[i], 1e-12) && near_ld(n[2], ref.z[i], 1e-12) &&
                 near_ld(n[3], ref.h[i], 1e-12) && near_ld(n[4], ref.h[i], 1e-12) &&
                 near_ld(n[5], ref.old_h[i], 1e-12) && near_ld(n[6], rows[i].beta * ref.h[i], 1e-12) &&
                 near_ld(n[7], sum, 1e-12) && near_ld(n[8], sum, 1e-12);
        }
        ++count;
    }
    ok &= !ferror(f) && count == expected_rows;
    ok &= !fgets(node_line, sizeof node_line, nodes) && !ferror(nodes);
    if (fclose(f) != 0) ok = 0;
    if (fclose(nodes) != 0) ok = 0;
    return ok;
}

int check_gas_generator(const char *coefficient_csv, const char *report)
{
    CheckCount count = {0};
    RefRow rows[50];
    const int loaded = read_reference(coefficient_csv, rows);
    check_assert(&count, loaded, "parse exactly 50 independent Appendix A rows (250 finite coefficients)");
    if (!loaded) return check_finish(&count, "gas-generator");
    int equal = gas_generator_params.hidden_count == 50;
    double sum_abs_beta = 0.0;
    for (int i = 0; i < 50; ++i) {
        equal &= rows[i].beta == gas_generator_params.output_weight[i] && rows[i].bias == gas_generator_params.hidden_bias[i];
        for (int j = 0; j < 3; ++j) equal &= rows[i].a[j] == gas_generator_params.input_weight[i][j];
        sum_abs_beta += fabs(rows[i].beta);
    }
    check_assert(&count, equal, "all 250 values, signs, row order and LOX/fuel/pressure columns match exactly");
    check_assert(&count, gas_generator_validate_params(&gas_generator_params) == GG_OK, "frozen default parameter contract valid");
    printf("C_exp comparison budget: sum_abs_beta=%.17g H_atol=4e-15 T_atol=%.17g K\n",
           sum_abs_beta, 202.9 * sum_abs_beta * 4e-15 + 1e-9);
    char node_path[1024];
    const int length = snprintf(node_path, sizeof node_path, "%s.nodes.csv", report);
    if (length < 0 || (size_t)length >= sizeof node_path) return 1;
    Evidence log = {0};
    log.cases = fopen(report, "w");
    log.nodes = fopen(node_path, "w");
    check_assert(&count, log.cases && log.nodes, "open case and per-node evidence CSV");
    if (!log.cases || !log.nodes) {
        if (log.cases) fclose(log.cases);
        if (log.nodes) fclose(log.nodes);
        return check_finish(&count, "gas-generator");
    }
    log.io_ok = fprintf(log.cases, "case,p_Pa,ox_requested,fuel_requested,status,flags,ox_effective,fuel_effective,W4,delta_W_floor,"
        "nomT,nomT_reference,T4_K,T4_reference_K,T4_C_exp_K,max_z_error,max_H_error,max_H_C_exp_error,T4_C_exp_error_K\n") >= 0;
    log.io_ok &= fprintf(log.nodes, "case,node,z,z_reference,H,H_reference,H_C_exp,weighted_H,cumulative,cumulative_reference\n") >= 0;
    check_case(&count, &log, rows, "midpoint", 3.6e6, 2.05, 5.1);
    for (int ip = 0; ip < 2; ++ip) for (int io = 0; io < 2; ++io) for (int iw = 0; iw < 2; ++iw) {
        char name[40]; snprintf(name, sizeof name, "corner_%d%d%d", ip, io, iw);
        check_case(&count, &log, rows, name, ip ? 4.3e6 : 2.9e6, io ? 2.5 : 1.6, iw ? 6.1 : 4.1);
    }
    for (int ip = 0; ip < 5; ++ip) for (int io = 0; io < 5; ++io) for (int iw = 0; iw < 5; ++iw) {
        char name[40]; snprintf(name, sizeof name, "grid_%d%d%d", ip, io, iw);
        check_case(&count, &log, rows, name, 2.9e6 + ip * 350000.0,
                   (1600.0 + io * 225.0) / 1000.0, (4100.0 + iw * 500.0) / 1000.0);
    }
    const double ox_edges[] = {nextafter(2e-5, 0.0), 2e-5, nextafter(2e-5, INFINITY)};
    const double fuel_edges[] = {nextafter(1e-5, 0.0), 1e-5, nextafter(1e-5, INFINITY)};
    for (int io = 0; io < 3; ++io) for (int iw = 0; iw < 3; ++iw) {
        char name[40]; snprintf(name, sizeof name, "floor_%d%d", io, iw);
        check_case(&count, &log, rows, name, 3.6e6, ox_edges[io], fuel_edges[iw]);
    }
    check_case(&count, &log, rows, "zero_requests", 3.6e6, 0.0, 0.0);
    check_case(&count, &log, rows, "negative_requests", 3.6e6, -1.0, -2.0);
    check_case(&count, &log, rows, "zero_ox_only", 3.6e6, 0.0, 5.1);
    check_case(&count, &log, rows, "zero_fuel_only", 3.6e6, 2.05, 0.0);
    check_case(&count, &log, rows, "source_initialization", 101325.0, 0.0001, 0.0004);
    check_case(&count, &log, rows, "extrap_pressure_low", 1e5, 2.05, 5.1);
    check_case(&count, &log, rows, "extrap_pressure_high", 1e12, 2.05, 5.1);
    check_case(&count, &log, rows, "fuel_above_10", 3.6e6, 2.05, 11.0);
    check_case(&count, &log, rows, "same_ratio_scaled", 3.6e6, 4.1, 10.2);
    check_case(&count, &log, rows, "extrap_ox_high", 3.6e6, 100.0, 5.1);
    check_case(&count, &log, rows, "extrap_fuel_high", 3.6e6, 2.05, 100.0);
    check_failures(&count);
    const int close_cases = fclose(log.cases), close_nodes = fclose(log.nodes);
    check_assert(&count, log.io_ok && !close_cases && !close_nodes, "all evidence writes and closes succeeded");
    check_assert(&count, check_report_readback(report, node_path, rows, log.samples), "C CSV readback reproduces all actual/reference layers, sums and temperatures");
    printf("GG evidence: cases=%d nonpositive_T_cases=%d nodes=%d max_normalization=%.17g max_z=%.17g max_H=%.17g max_sum=%.17g max_T_K=%.17g mass_scaled=%.17g\n",
        log.samples, log.invalid_temperature, 50 * log.samples, log.max_n, log.max_z, log.max_h, log.max_sum, log.max_T, log.max_mass);
    printf("GG in-range C_exp difference: max_H=%.17g max_T_K=%.17g\n", log.max_legacy_h, log.max_legacy_T);
    return check_finish(&count, "gas-generator");
}

int compare_gas_generator_csv(const char *first, const char *second)
{
    FILE *a = fopen(first, "r"), *b = fopen(second, "r");
    if (!a || !b) {
        if (a) fclose(a);
        if (b) fclose(b);
        return 1;
    }
    char la[4096], lb[4096];
    int ok = fgets(la, sizeof la, a) && fgets(lb, sizeof lb, b) && !strcmp(la, lb);
    const int has_label = ok && !strncmp(la, "case,", 5);
    int columns = 1;
    for (const char *p = la; ok && *p; ++p) if (*p == ',') ++columns;
    ok &= columns == 24 || columns == 19 || columns == 10;
    int row_count = 0;
    double max_abs = 0.0;
    while (ok) {
        char *ra = fgets(la, sizeof la, a), *rb = fgets(lb, sizeof lb, b);
        if (!ra || !rb) { ok = !ra && !rb; break; }
        char *pa = la, *pb = lb;
        if (has_label) {
            char *ca = strchr(pa, ','), *cb = strchr(pb, ',');
            if (!ca || !cb || ca - pa != cb - pb || strncmp(pa, pb, (size_t)(ca - pa))) { ok = 0; break; }
            pa = ca + 1; pb = cb + 1;
        }
        for (int col = has_label; col < columns && ok; ++col) {
            char *ea, *eb;
            const double va = strtod(pa, &ea), vb = strtod(pb, &eb);
            const double delta = fabs(va - vb);
            const int discrete = (columns == 24 && col >= 21) ||
                (columns == 19 && (col == 4 || col == 5)) || (columns == 10 && col == 1);
            ok = ea != pa && eb != pb && isfinite(va) && isfinite(vb) &&
                 (discrete ? va == vb : delta <= 1e-10 + 1e-9 * fabs(vb));
            ok &= col == columns - 1 ? (!strcmp(ea, "\n") && !strcmp(eb, "\n")) : (*ea == ',' && *eb == ',');
            max_abs = fmax(max_abs, delta);
            pa = ea + 1; pb = eb + 1;
        }
        ++row_count;
    }
    ok &= row_count > 0 && !ferror(a) && !ferror(b);
    const int ca = fclose(a), cb = fclose(b);
    ok &= ca == 0 && cb == 0;
    printf("%s GG CSV comparison rows=%d columns=%d max_abs=%.17g\n", ok ? "PASS" : "FAIL", row_count, columns, max_abs);
    return ok ? 0 : 1;
}
