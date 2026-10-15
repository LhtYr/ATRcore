#ifndef ATRCORE_GAS_H
#define ATRCORE_GAS_H

/* Screenshot Cp and calibrated turbine properties are distinct contracts. */
typedef enum GasPropertySource {
    GAS_PROPERTY_UNSET = 0,
    TURBINE_CALIBRATED = 1
} GasPropertySource;

typedef struct GasProperties {
    double cp_source;          /* J/(kg K), screenshot value, separate from turbine */
    double gamma_for_turbine;  /* dimensionless */
    double R_for_turbine;      /* J/(kg K) */
    double cp_for_turbine;     /* J/(kg K) */
    GasPropertySource property_source;
} GasProperties;

typedef struct Gas {
    double ox_flow, fuel_flow, mass_flow;         /* kg/s, source accounting */
    double total_temperature, static_temperature; /* K */
    double total_pressure, static_pressure;       /* Pa */
    GasProperties properties;
} Gas;

typedef enum GasStatus {
    GAS_NOT_EVALUATED = 0,
    GAS_OK,
    GAS_INVALID_ARGUMENT,
    GAS_INVALID_INPUT,
    GAS_INVALID_OUTPUT
} GasStatus;

/* Pure fixed adapter. A null output is rejected. */
GasStatus gas_properties_turbine_calibrated(GasProperties *out);

/* SI units: kg/s, Pa, K. Takes EFFECTIVE source flows, sums them once.
 * Zero flow is allowed for downstream isolated blowdown checks. Requested
 * flow floors belong to GasGenerator, not Gas. No pressure/temperature floor
 * or normalization clamp is applied here. Static fields are legacy accounting,
 * not a compressible-flow total/static conversion. Failure leaves out intact.
 */
GasStatus gas_build_from_effective_flows(double ox_flow, double fuel_flow,
                                        double total_pressure,
                                        double total_temperature, Gas *out);
#endif
