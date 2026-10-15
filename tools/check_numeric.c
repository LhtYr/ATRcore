#include "check_s04.h"
#include "check_s02.h"
#include "utils/numeric_utils.h"

#include <float.h>
#include <string.h>

typedef struct Sweep {
    double max_legacy_abs, max_libm_abs, max_exp_rel, previous;
    int finite_range, monotone, accurate, io_ok;
    size_t rows;
} Sweep;

/* Independent long-double oracle; no production function or legacy constants. */
static double oracle(double z)
{
    const long double e = expl(-fabsl((long double)z));
    return (double)(z < 0.0 ? e / (1.0L + e) : 1.0L / (1.0L + e));
}

static void sample(Sweep *s, FILE *file, const char *region, double z, int compare_legacy)
{
    double actual = NAN;
    const int ok = numeric_sigmoid(z, &actual);
    const double old = 1.0 / (1.0 + reference_exp(-z));
    const double delta = fabs(actual - old);
    const double expected = oracle(z);
    const double libm_delta = fabs(actual - expected);
    s->finite_range &= ok && isfinite(actual) && actual >= 0.0 && actual <= 1.0 &&
                       isfinite(old) && old >= 0.0 && old <= 1.0;
    s->monotone &= actual >= s->previous;
    s->previous = actual;
    s->accurate &= isfinite(libm_delta) && libm_delta <= 8.0 * DBL_EPSILON;
    /* Absolute error alone would incorrectly accept loss of a small tail. */
    if (expected >= DBL_MIN)
        s->accurate &= libm_delta / expected <= 8.0 * DBL_EPSILON;
    else
        s->accurate &= libm_delta <= 2.0 * DBL_TRUE_MIN;
    s->max_libm_abs = fmax(s->max_libm_abs, libm_delta);
    s->max_legacy_abs = fmax(s->max_legacy_abs, delta);
    if (compare_legacy) {
        const double expected_exp = exp(-z); /* |z| <= 500, no overflow */
        const double relative = fabs(reference_exp(-z) / expected_exp - 1.0);
        s->accurate &= isfinite(delta) && delta <= 4e-15 &&
                      isfinite(relative) && relative <= 1e-13;
        s->max_exp_rel = fmax(s->max_exp_rel, relative);
    }
    s->io_ok &= fprintf(file, "%s,%.17g,%.17g,%.17g,%.17g\n", region, z, actual, old, delta) >= 0;
    ++s->rows;
}

static void finish_sweep(CheckCount *count, const Sweep *s, const char *name)
{
    char label[100];
    snprintf(label, sizeof label, "%s finite/range, monotonicity, independent accuracy and CSV", name);
    check_assert(count, s->finite_range && s->monotone && s->accurate && s->io_ok, label);
    printf("%s rows=%zu max_sigmoid_legacy_abs=%.17g max_oracle_abs=%.17g max_exp_rel=%.17g\n",
           name, s->rows, s->max_legacy_abs, s->max_libm_abs, s->max_exp_rel);
}

int check_numeric(const char *csv_path)
{
    CheckCount count = {0};
    double result = -7.0;
    check_assert(&count, numeric_sigmoid(0.0, &result) && result == 0.5, "sigmoid(0) = 0.5");
    check_assert(&count, numeric_sigmoid(-0.0, &result) && result == 0.5, "sigmoid(-0) = 0.5");
    check_assert(&count, !numeric_sigmoid(0.0, NULL), "sigmoid rejects null output");
    const double invalid[] = {NAN, INFINITY, -INFINITY};
    for (size_t i = 0; i < sizeof invalid / sizeof invalid[0]; ++i) {
        result = -7.0;
        check_assert(&count, !numeric_sigmoid(invalid[i], &result) && result == -7.0,
                     "sigmoid rejects nonfinite input without changing output");
    }
    check_assert(&count, numeric_sigmoid(-DBL_MAX, &result) && result == 0.0,
                 "sigmoid(-DBL_MAX) finite saturation = 0");
    check_assert(&count, numeric_sigmoid(DBL_MAX, &result) && result == 1.0,
                 "sigmoid(DBL_MAX) finite saturation = 1");
    FILE *file = csv_path ? fopen(csv_path, "w") : NULL;
    check_assert(&count, file != NULL, "open numeric evidence CSV");
    if (!file) return check_finish(&count, "numeric");
    check_assert(&count, fprintf(file, "region,z,sigmoid,legacy_sigmoid,abs_difference\n") >= 0,
                 "write numeric evidence header");
    Sweep normal = {0.0, 0.0, 0.0, -1.0, 1, 1, 1, 1, 0};
    for (int i = 0; i <= 8000; ++i)
        sample(&normal, file, "normal", (i - 4000) / 1000.0, 1);
    finish_sweep(&count, &normal, "normal [-4,4]");
    Sweep wide = {0.0, 0.0, 0.0, -1.0, 1, 1, 1, 1, 0};
    for (int i = 0; i <= 2000; ++i)
        sample(&wide, file, "wide", -500.0 + i * 0.5, 1);
    finish_sweep(&count, &wide, "uncompressed [-500,500]");
    /* Probe both sides of original C_exp polynomial segment boundaries. */
    Sweep edges = {0.0, 0.0, 0.0, -1.0, 1, 1, 1, 1, 0};
    for (int i = -20; i <= 20; ++i) {
        const double z = i * 0.2;
        sample(&edges, file, "edge", nextafter(z, -INFINITY), 1);
        sample(&edges, file, "edge", z, 1);
        sample(&edges, file, "edge", nextafter(z, INFINITY), 1);
    }
    finish_sweep(&count, &edges, "polynomial edges");
    Sweep extreme = {0.0, 0.0, 0.0, -1.0, 1, 1, 1, 1, 0};
    const double extremes[] = {-DBL_MAX, -1000.0, -745.0, -710.0, -600.0,
                               -500.000001, 500.000001, 600.0, 710.0, 745.0, 1000.0, DBL_MAX};
    for (size_t i = 0; i < sizeof extremes / sizeof extremes[0]; ++i)
        sample(&extreme, file, "soft_limit_difference", extremes[i], 0);
    finish_sweep(&count, &extreme, "extremes (legacy difference informational)");
    const double old600 = 1.0 / (1.0 + reference_exp(600.0));
    (void)numeric_sigmoid(-600.0, &result);
    const double compressed = 500.0 + 100.0 / 1.5;
    check_assert(&count, fabs(reference_exp(600.0) / exp(compressed) - 1.0) <= 1e-13 &&
                 old600 / result > 1e14, "legacy soft-limit difference independently identified at z=-600");
    printf("soft-limit evidence z=-600: new=%.17g legacy=%.17g legacy/new=%.17g compressed_abs_z=%.17g\n",
           result, old600, old600 / result, compressed);
    int symmetric = 1;
    for (int i = 0; i <= 100; ++i) {
        double positive = NAN, negative = NAN;
        symmetric &= numeric_sigmoid(i * 0.1, &positive) && numeric_sigmoid(-i * 0.1, &negative);
        symmetric &= fabs(positive + negative - 1.0) <= 2.0 * DBL_EPSILON;
    }
    check_assert(&count, symmetric, "sigmoid complement symmetry at 101 pairs");
    check_assert(&count, fclose(file) == 0, "close numeric evidence CSV");
    return check_finish(&count, "numeric");
}

int check_numeric_csv_pair(const char *first, const char *second)
{
    CheckCount count = {0};
    FILE *a = fopen(first, "r"), *b = fopen(second, "r");
    check_assert(&count, a && b, "open both numeric CSV files");
    if (!a || !b) {
        if (a) fclose(a);
        if (b) fclose(b);
        return check_finish(&count, "numeric-compare");
    }
    char line_a[512], line_b[512];
    const char *header = "region,z,sigmoid,legacy_sigmoid,abs_difference\n";
    int structure = fgets(line_a, sizeof line_a, a) && fgets(line_b, sizeof line_b, b) &&
                    !strcmp(line_a, header) && !strcmp(line_b, header);
    int values = 1;
    size_t rows = 0;
    double max_abs[3] = {0}, max_tail_rel = 0.0;
    while (structure) {
        char *ra = fgets(line_a, sizeof line_a, a), *rb = fgets(line_b, sizeof line_b, b);
        if (!ra || !rb) { structure &= !ra && !rb; break; }
        char region_a[32], region_b[32];
        double va[4], vb[4];
        int end_a = 0, end_b = 0;
        const char *format = "%31[^,],%lf,%lf,%lf,%lf%n";
        structure &= sscanf(line_a, format, region_a, &va[0], &va[1], &va[2], &va[3], &end_a) == 5 &&
                     sscanf(line_b, format, region_b, &vb[0], &vb[1], &vb[2], &vb[3], &end_b) == 5;
        if (!structure) break;
        structure &= !strcmp(line_a + end_a, "\n") && !strcmp(line_b + end_b, "\n") &&
                     !strcmp(region_a, region_b) && isfinite(va[0]) && va[0] == vb[0];
        for (int col = 1; col < 4; ++col) {
            const double delta = fabs(va[col] - vb[col]);
            values &= isfinite(va[col]) && isfinite(vb[col]) && delta <= 4e-15;
            max_abs[col - 1] = fmax(max_abs[col - 1], delta);
            if (col <= 2) {
                const double scale = fmax(fabs(va[col]), fabs(vb[col]));
                values &= va[col] >= 0.0 && va[col] <= 1.0 && vb[col] >= 0.0 && vb[col] <= 1.0;
                if (scale >= DBL_MIN) {
                    const double relative = delta / scale;
                    values &= relative <= 1e-13;
                    max_tail_rel = fmax(max_tail_rel, relative);
                } else values &= delta <= 2.0 * DBL_TRUE_MIN;
            }
        }
        ++rows;
    }
    structure &= !ferror(a) && !ferror(b);
    const int close_a = fclose(a), close_b = fclose(b);
    check_assert(&count, structure && rows == 10137, "same header, regions, exact z grid, 10137 rows, complete parse");
    check_assert(&count, values, "release/debug values satisfy absolute and small-tail relative tolerances");
    check_assert(&count, close_a == 0 && close_b == 0, "close both numeric CSV files");
    printf("CSV comparison: rows=%zu max_sigmoid_abs=%.17g max_legacy_abs=%.17g max_delta_abs=%.17g max_tail_rel=%.17g\n",
           rows, max_abs[0], max_abs[1], max_abs[2], max_tail_rel);
    return check_finish(&count, "numeric-compare");
}
