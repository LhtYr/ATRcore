#include "check_s04.h"
#include "check_s02.h"
#include "component/Gas.h"

#include <float.h>
#include <string.h>

int check_gas(void)
{
    CheckCount count = {0};
    Gas gas = {0};
    GasProperties props = {0};
    check_assert(&count, GAS_NOT_EVALUATED != GAS_OK, "Gas zero status is not success");
    check_assert(&count, gas_properties_turbine_calibrated(NULL) == GAS_INVALID_ARGUMENT,
                 "property adapter rejects null");
    check_assert(&count, gas_properties_turbine_calibrated(&props) == GAS_OK,
                 "fixed property adapter succeeds");
    check_assert(&count, props.cp_source == 2860.0, "screenshot Cp = 2860 J/(kg K)");
    check_assert(&count, props.gamma_for_turbine == 1.18 && props.R_for_turbine == 380.0,
                 "turbine gamma/R = 1.18/380");
    check_assert(&count, fabs(props.cp_for_turbine - 22420.0 / 9.0) <= 1e-9,
                 "turbine Cp independently equals 22420/9");
    check_assert(&count, fabs(props.cp_source - props.cp_for_turbine - 3320.0 / 9.0) <= 1e-9,
                 "two Cp values remain distinct (difference 3320/9)");
    check_assert(&count, props.property_source == TURBINE_CALIBRATED,
                 "explicit turbine calibration source");
    check_assert(&count, gas_build_from_effective_flows(2.05, 5.1, 3.6e6, 1100.0, &gas) == GAS_OK,
                 "Gas accepts mathematical midpoint flow fixture");
    check_assert(&count, fabs(gas.mass_flow - 7.15) <= 1e-12 &&
                 gas.ox_flow == 2.05 && gas.fuel_flow == 5.1,
                 "source flows summed once without unit conversion");
    check_assert(&count, gas.total_temperature == 1100.0 && fabs(gas.static_temperature - 891.0) <= 1e-12,
                 "total temperature preserved; static accounting 0.81 times total");
    check_assert(&count, gas.total_pressure == 3.6e6 && gas.static_pressure == 3.6e6,
                 "total/static pressure accounting is direct Pa pass-through");
    check_assert(&count, gas.properties.cp_source == props.cp_source &&
                 gas.properties.cp_for_turbine == props.cp_for_turbine &&
                 gas.properties.gamma_for_turbine == props.gamma_for_turbine &&
                 gas.properties.R_for_turbine == props.R_for_turbine &&
                 gas.properties.property_source == props.property_source,
                 "Gas carries both property contracts");
    check_assert(&count, gas_build_from_effective_flows(2.05, 5.1, 3.6e6, 1100.0, &gas) == GAS_OK &&
                 fabs(gas.mass_flow - 7.15) <= 1e-12, "repeated evaluation does not accumulate fuel");
    check_assert(&count, gas_build_from_effective_flows(0.0, 0.0, 101325.0, 300.0, &gas) == GAS_OK &&
                 gas.mass_flow == 0.0, "explicit zero flow allowed for isolated blowdown");
    check_assert(&count, gas_build_from_effective_flows(1e-8, 2e-9, 1.0, 100.0, &gas) == GAS_OK &&
                 fabs(gas.mass_flow - 1.2e-8) <= 1e-23 && gas.total_temperature == 100.0 &&
                 gas.total_pressure == 1.0, "Gas applies no requested-flow or turbine temperature/pressure floor");
    check_assert(&count, gas_build_from_effective_flows(0.0, 2.0, 1.0, 1.0, &gas) == GAS_OK &&
                 gas.mass_flow == 2.0, "one source alone is counted once");
    check_assert(&count, gas_build_from_effective_flows(1.0, 1.0, 1.0, 1.0, NULL) == GAS_INVALID_ARGUMENT,
                 "Gas rejects null output");

    /* Every input slot is checked independently, including nonfinite values. */
    const double invalid[] = {-1.0, NAN, INFINITY, -INFINITY};
    for (int slot = 0; slot < 4; ++slot) {
        for (size_t i = 0; i < sizeof invalid / sizeof invalid[0]; ++i) {
            double args[] = {2.05, 5.1, 3.6e6, 1100.0};
            unsigned char before[sizeof gas];
            char label[100];
            memcpy(before, &gas, sizeof gas);
            args[slot] = invalid[i];
            const GasStatus status = gas_build_from_effective_flows(args[0], args[1], args[2], args[3], &gas);
            snprintf(label, sizeof label, "invalid slot=%d case=%zu rejected; output unchanged", slot, i);
            check_assert(&count, status == GAS_INVALID_INPUT && !memcmp(before, &gas, sizeof gas), label);
        }
    }
    unsigned char before[sizeof gas];
    memcpy(before, &gas, sizeof gas);
    check_assert(&count, gas_build_from_effective_flows(1.0, 1.0, 0.0, 1.0, &gas) == GAS_INVALID_INPUT &&
                 !memcmp(before, &gas, sizeof gas), "zero pressure rejected atomically");
    check_assert(&count, gas_build_from_effective_flows(1.0, 1.0, 1.0, 0.0, &gas) == GAS_INVALID_INPUT &&
                 !memcmp(before, &gas, sizeof gas), "zero total temperature rejected atomically");
    check_assert(&count, gas_build_from_effective_flows(DBL_MAX, DBL_MAX, 1.0, 1.0, &gas) == GAS_INVALID_OUTPUT &&
                 !memcmp(before, &gas, sizeof gas), "overflowing mass sum rejected atomically");
    printf("Gas evidence: Cp_source=%.17g Cp_turbine=%.17g delta_Cp=%.17g\n",
           props.cp_source, props.cp_for_turbine, props.cp_source - props.cp_for_turbine);
    return check_finish(&count, "gas");
}
