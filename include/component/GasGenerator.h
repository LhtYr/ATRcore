#ifndef ATRCORE_GAS_GENERATOR_H
#define ATRCORE_GAS_GENERATOR_H
#include "component/Gas.h"

enum { GAS_GENERATOR_HIDDEN_COUNT = 50 };
typedef enum GasGeneratorStatus {
    GG_NOT_EVALUATED = 0, GG_OK, GG_INVALID_INPUT, GG_INVALID_PARAMS,
    GG_INVALID_OUTPUT
} GasGeneratorStatus;
enum GasGeneratorFlags {
    FUEL_FLOORED = 1u << 0,
    OX_FLOORED = 1u << 1,
    INPUT_OUTSIDE_NORMALIZATION_RANGE = 1u << 2,
    OUTPUT_OUTSIDE_TEMPERATURE_SCALE = 1u << 3,
    SOURCE_CP_DIFFERS_FROM_TURBINE_CP = 1u << 4
};

/* Fixed model data: the coefficient pointers refer to immutable Appendix A
 * arrays. This version accepts only the frozen scalar/model contract; it has
 * no runtime coefficient/scalar override or training interface. */
typedef struct GasGeneratorParams {
    int hidden_count;
    const double (*input_weight)[3]; /* columns: normalized LOX, fuel, pressure */
    const double *hidden_bias, *output_weight;
    double ox_min, ox_max, fuel_min, fuel_max;
    double pressure_min_mpa, pressure_max_mpa, pressure_scale_pa;
    double temperature_min, temperature_max;
    double fuel_floor, ox_floor;
    double cp_source, temperature_scale, static_temperature_ratio;
    double legacy_fuel_calibration; /* metadata ONLY, not a fuel ceiling */
    GasPropertySource property_mode;
} GasGeneratorParams;

/* Pure algebraic observations; failed evaluations must never drive a solver. */
typedef struct GasGeneratorEval {
    double p_chamber;                            /* Pa */
    double W_ox_requested, Wg_requested;           /* kg/s */
    double W_ox_effective, Wg_effective, W4;        /* kg/s */
    double P4_total, T4_total, T4_static, P4_static; /* Pa, K, K, Pa */
    double ox_fuel_ratio, fuel_ox_ratio;           /* O/F, F/O */
    GasProperties properties;
    double nomO2, nomWf, nomConP, nomConT;
    double preactivation[GAS_GENERATOR_HIDDEN_COUNT];
    double activation[GAS_GENERATOR_HIDDEN_COUNT];
    unsigned flags;
    GasGeneratorStatus status;
} GasGeneratorEval;

/* Keep the complete final snapshot, including hidden-layer diagnostics. */
typedef GasGeneratorEval GasGeneratorView;
extern const GasGeneratorParams gas_generator_params;
extern GasGeneratorView gas_generator_view;

GasGeneratorStatus gas_generator_validate_params(const GasGeneratorParams *params);
GasGeneratorEval gas_generator_evaluate(const GasGeneratorParams *params,
                                        double p_chamber, double W_ox, double Wg);
/* Copy only a completed successful evaluation; never re-evaluate or add flows.
 * The caller chooses which evaluation is final. Failure leaves view unchanged. */
GasGeneratorStatus gas_generator_apply_state(GasGeneratorView *view,
                                             const GasGeneratorEval *eval);
#endif
