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

/* Algebraic observations only. No ELM computation or coefficients in S02. */
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
#endif
