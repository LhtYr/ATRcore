#ifndef ATRCORE_GAS_H
#define ATRCORE_GAS_H

/* Data contract only; the property adapter is implemented in S04. */
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
#endif
