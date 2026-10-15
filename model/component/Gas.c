#include "component/Gas.h"

#include <math.h>

GasStatus gas_properties_turbine_calibrated(GasProperties *out)
{
    const double gamma = 1.18;
    const double gas_R = 380.0;
    if (!out) return GAS_INVALID_ARGUMENT;
    const GasProperties properties = {
        2860.0, gamma, gas_R, gamma * gas_R / (gamma - 1.0),
        TURBINE_CALIBRATED
    };
    *out = properties;
    return GAS_OK;
}

GasStatus gas_build_from_effective_flows(double ox_flow, double fuel_flow,
                                        double total_pressure,
                                        double total_temperature, Gas *out)
{
    if (!out) return GAS_INVALID_ARGUMENT;
    if (!isfinite(ox_flow) || !isfinite(fuel_flow) ||
        !isfinite(total_pressure) || !isfinite(total_temperature) ||
        ox_flow < 0.0 || fuel_flow < 0.0 ||
        total_pressure <= 0.0 || total_temperature <= 0.0)
        return GAS_INVALID_INPUT;

    Gas next = {0};
    next.ox_flow = ox_flow;
    next.fuel_flow = fuel_flow;
    next.mass_flow = ox_flow + fuel_flow;
    next.total_pressure = total_pressure;
    next.static_pressure = total_pressure;
    next.total_temperature = total_temperature;
    next.static_temperature = 0.81 * total_temperature;
    if (!isfinite(next.mass_flow) || !isfinite(next.static_temperature) ||
        next.static_temperature <= 0.0)
        return GAS_INVALID_OUTPUT;
    (void)gas_properties_turbine_calibrated(&next.properties);
    *out = next;
    return GAS_OK;
}
