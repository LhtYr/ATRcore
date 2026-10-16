#include "char_gas_generator.h"
#include "component/GasGenerator.h"

#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int parse_number(const char *text, double *value)
{
    char *end;
    errno = 0;
    *value = strtod(text, &end);
    return end != text && *end == '\0' && errno != ERANGE && isfinite(*value);
}

static int write_header(FILE *f)
{
    return fprintf(f, "p_chamber_Pa,W_ox_requested_kg_s,Wg_requested_kg_s,W_ox_effective_kg_s,Wg_effective_kg_s,"
        "W4_kg_s,delta_W_floor_kg_s,P4_total_Pa,T4_total_K,T4_static_K,P4_static_Pa,"
        "ox_fuel_ratio,fuel_ox_ratio,nomO2,nomWf,nomConP,nomConT,cp_source_J_kgK,"
        "gamma_turbine,R_turbine_J_kgK,cp_turbine_J_kgK,property_source,flags,status\n") >= 0;
}

static int write_row(FILE *f, const GasGeneratorEval *e)
{
    const double floor_added = (e->W_ox_effective - e->W_ox_requested) +
                               (e->Wg_effective - e->Wg_requested);
    if (e->status != GG_OK || !isfinite(floor_added)) return 0;
    return fprintf(f,
        "%.17g,%.17g,%.17g,%.17g,%.17g,%.17g,%.17g,%.17g,%.17g,%.17g,%.17g,"
        "%.17g,%.17g,%.17g,%.17g,%.17g,%.17g,%.17g,%.17g,%.17g,%.17g,%d,%u,%d\n",
        e->p_chamber, e->W_ox_requested, e->Wg_requested, e->W_ox_effective, e->Wg_effective,
        e->W4, floor_added, e->P4_total, e->T4_total, e->T4_static, e->P4_static,
        e->ox_fuel_ratio, e->fuel_ox_ratio, e->nomO2, e->nomWf, e->nomConP, e->nomConT,
        e->properties.cp_source, e->properties.gamma_for_turbine, e->properties.R_for_turbine,
        e->properties.cp_for_turbine, (int)e->properties.property_source, e->flags, (int)e->status) >= 0;
}

int char_gas_generator(int argc, char **argv)
{
    const int point = argc == 6 && !strcmp(argv[1], "gg-point");
    const int scan = argc == 3 && !strcmp(argv[1], "gg-scan");
    if (!point && !scan) {
        fputs("Usage: char gg-point P_PA W_OX WG CSV | gg-scan CSV\n", stderr);
        return 1;
    }
    GasGeneratorEval e = {0};
    if (point) {
        double p, ox, fuel;
        if (!parse_number(argv[2], &p) || !parse_number(argv[3], &ox) || !parse_number(argv[4], &fuel)) {
            fputs("Invalid finite numeric input.\n", stderr);
            return 1;
        }
        e = gas_generator_evaluate(&gas_generator_params, p, ox, fuel);
        if (e.status != GG_OK) {
            fprintf(stderr, "GasGenerator failed: status=%d flags=%u\n", (int)e.status, e.flags);
            return 1;
        }
    }
    const char *path = argv[point ? 5 : 2];
    FILE *f = fopen(path, "w");
    if (!f) { perror(path); return 1; }
    int ok = write_header(f);
    int rows = 0;
    double tmin = INFINITY, tmax = -INFINITY;
    if (point) {
        ok &= write_row(f, &e);
        rows = 1;
        tmin = tmax = e.T4_total;
        printf("GG point: status=%d flags=%u W4=%.17g T4=%.17g nomT=%.17g\n",
               (int)e.status, e.flags, e.W4, e.T4_total, e.nomConT);
    } else {
        /* 3 pressures x 21 oxygen x 21 fuel. Frozen normalization box only;
         * this is a mathematical characteristic scan, not a training envelope. */
        for (int ip = 0; ip < 3 && ok; ++ip)
            for (int io = 0; io <= 20 && ok; ++io)
                for (int iw = 0; iw <= 20 && ok; ++iw) {
                    const double p = (2900000.0 + ip * 700000.0);
                    const double ox = (1600.0 + io * 45.0) / 1000.0;
                    const double fuel = (4100.0 + iw * 100.0) / 1000.0;
                    e = gas_generator_evaluate(&gas_generator_params, p, ox, fuel);
                    ok &= write_row(f, &e);
                    tmin = fmin(tmin, e.T4_total);
                    tmax = fmax(tmax, e.T4_total);
                    ++rows;
                }
    }
    if (fclose(f) != 0) ok = 0;
    printf("%s GG CSV %s: rows=%d Tmin=%.17g Tmax=%.17g K\n", ok ? "PASS" : "FAIL", path, rows, tmin, tmax);
    return ok ? 0 : 1;
}
