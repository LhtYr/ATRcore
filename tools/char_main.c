#include "build_info.h"
#include "check_s02.h"
#include "check_logger.h"
#include "check_s04.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int check_build(void)
{
    /* Volatile prevents folding away the libm call in release builds. */
    volatile double zero = 0.0;
    const double value = exp(zero);
    /* Equality to finite 1.0 also rejects NaN and both infinities. */
    const int ok = value == 1.0;
    printf("%s " ATRCORE_BUILD_STAGE
           " build smoke: C standard=%ld; exp(0)=%.17g; expected=1; error=%.17g\n",
           ok ? "PASS" : "FAIL", (long)__STDC_VERSION__, value, fabs(value - 1.0));
    return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}

int main(int argc, char **argv)
{
    if (argc == 2 &&
        (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0)) {
        puts("Usage: char check | contracts-check | params-check | boundary-check | logger-check [CSV] | gas-check | numeric-check [CSV] | numeric-compare CSV CSV | --help\n"
             "C checks for contracts, configuration, boundaries and CSV.\n"
             "logger-check default: results/logger_fixture.csv (synthetic snapshots, not simulation).");
        return EXIT_SUCCESS;
    }
    if (argc == 2 && strcmp(argv[1], "check") == 0) {
        int failed = check_build();
        failed |= check_contracts();
        failed |= check_params();
        failed |= check_boundary();
        failed |= check_logger("results/logger_fixture.csv");
        failed |= check_gas();
        failed |= check_numeric("results/numeric_fixture.csv");
        return failed ? EXIT_FAILURE : EXIT_SUCCESS;
    }
    if (argc == 2 && !strcmp(argv[1], "contracts-check")) return check_contracts();
    if (argc == 2 && !strcmp(argv[1], "params-check")) return check_params();
    if (argc == 2 && !strcmp(argv[1], "boundary-check")) return check_boundary();
    if (argc == 2 && !strcmp(argv[1], "gas-check")) return check_gas();
    if (argc == 4 && !strcmp(argv[1], "numeric-compare"))
        return check_numeric_csv_pair(argv[2], argv[3]);
    if ((argc == 2 || argc == 3) && !strcmp(argv[1], "numeric-check"))
        return check_numeric(argc == 3 ? argv[2] : "results/numeric_fixture.csv");
    if ((argc == 2 || argc == 3) && !strcmp(argv[1], "logger-check"))
        return check_logger(argc == 3 ? argv[2] : "results/logger_fixture.csv");
    fputs("Error: missing or unsupported command; use --help.\n", stderr);
    return EXIT_FAILURE;
}
