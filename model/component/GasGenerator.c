#include "component/GasGenerator.h"
#include "utils/numeric_utils.h"

#include <math.h>

#define GG_ROW(i, beta, bias, a0, a1, a2) {a0, a1, a2},
static const double input_weight[][3] = {
#include "gas_generator_coefficients.inc"
};
#undef GG_ROW
#define GG_ROW(i, beta, bias, a0, a1, a2) bias,
static const double hidden_bias[] = {
#include "gas_generator_coefficients.inc"
};
#undef GG_ROW
#define GG_ROW(i, beta, bias, a0, a1, a2) beta,
static const double output_weight[] = {
#include "gas_generator_coefficients.inc"
};
#undef GG_ROW
_Static_assert(sizeof input_weight / sizeof input_weight[0] == 50, "50 input rows");
_Static_assert(sizeof hidden_bias / sizeof hidden_bias[0] == 50, "50 biases");
_Static_assert(sizeof output_weight / sizeof output_weight[0] == 50, "50 output weights");

const GasGeneratorParams gas_generator_params = {
    .hidden_count = 50, .input_weight = input_weight,
    .hidden_bias = hidden_bias, .output_weight = output_weight,
    .ox_min = 1.6, .ox_max = 2.5, .fuel_min = 4.1, .fuel_max = 6.1,
    .pressure_min_mpa = 2.9, .pressure_max_mpa = 4.3, .pressure_scale_pa = 1e6,
    .temperature_min = 1021.1, .temperature_max = 1224.0,
    .fuel_floor = 1e-5, .ox_floor = 2e-5, .cp_source = 2860.0,
    .temperature_scale = 1.0, .static_temperature_ratio = 0.81,
    .legacy_fuel_calibration = 10.0, .property_mode = TURBINE_CALIBRATED
};
GasGeneratorView gas_generator_view = {0};

GasGeneratorStatus gas_generator_validate_params(const GasGeneratorParams *p)
{
    if (!p || p->hidden_count != 50 || p->input_weight != input_weight ||
        p->hidden_bias != hidden_bias || p->output_weight != output_weight ||
        p->property_mode != TURBINE_CALIBRATED)
        return GG_INVALID_PARAMS;
    /* Exact identity intentionally enforces this frozen model version. Invalid
     * scales and nonfinite values also fail these comparisons before division. */
    if (p->ox_min != 1.6 || p->ox_max != 2.5 || p->fuel_min != 4.1 || p->fuel_max != 6.1 ||
        p->pressure_min_mpa != 2.9 || p->pressure_max_mpa != 4.3 || p->pressure_scale_pa != 1e6 ||
        p->temperature_min != 1021.1 || p->temperature_max != 1224.0 ||
        p->fuel_floor != 1e-5 || p->ox_floor != 2e-5 || p->cp_source != 2860.0 ||
        p->temperature_scale != 1.0 || p->static_temperature_ratio != 0.81 ||
        !isfinite(p->legacy_fuel_calibration))
        return GG_INVALID_PARAMS;
    return GG_OK;
}

GasGeneratorEval gas_generator_evaluate(const GasGeneratorParams *p,
                                        double p_chamber, double W_ox, double Wg)
{
    GasGeneratorEval e = {0};
    e.p_chamber = p_chamber;
    e.W_ox_requested = W_ox;
    e.Wg_requested = Wg;
    e.status = gas_generator_validate_params(p);
    if (e.status != GG_OK) return e;
    e.status = GG_INVALID_INPUT;
    if (!isfinite(p_chamber) || !isfinite(W_ox) || !isfinite(Wg) || p_chamber <= 0.0)
        return e;

    e.W_ox_effective = W_ox < p->ox_floor ? p->ox_floor : W_ox;
    e.Wg_effective = Wg < p->fuel_floor ? p->fuel_floor : Wg;
    if (W_ox < p->ox_floor) e.flags |= OX_FLOORED;
    if (Wg < p->fuel_floor) e.flags |= FUEL_FLOORED;
    e.nomO2 = (e.W_ox_effective - p->ox_min) / (p->ox_max - p->ox_min);
    e.nomWf = (e.Wg_effective - p->fuel_min) / (p->fuel_max - p->fuel_min);
    e.nomConP = (p_chamber / p->pressure_scale_pa - p->pressure_min_mpa) /
                (p->pressure_max_mpa - p->pressure_min_mpa);
    if (e.nomO2 < 0.0 || e.nomO2 > 1.0 || e.nomWf < 0.0 || e.nomWf > 1.0 ||
        e.nomConP < 0.0 || e.nomConP > 1.0)
        e.flags |= INPUT_OUTSIDE_NORMALIZATION_RANGE;
    e.status = GG_INVALID_OUTPUT;
    if (!isfinite(e.nomO2) || !isfinite(e.nomWf) || !isfinite(e.nomConP)) return e;

    for (int i = 0; i < GAS_GENERATOR_HIDDEN_COUNT; ++i) {
        e.preactivation[i] = p->input_weight[i][0] * e.nomO2 +
                            p->input_weight[i][1] * e.nomWf +
                            p->input_weight[i][2] * e.nomConP + p->hidden_bias[i];
        if (!numeric_sigmoid(e.preactivation[i], &e.activation[i])) return e;
    }
    /* Ascending accumulation as in screenshot lines 240-243; no output bias. */
    for (int i = 0; i < GAS_GENERATOR_HIDDEN_COUNT; ++i)
        e.nomConT = e.nomConT + e.activation[i] * p->output_weight[i];
    e.T4_total = p->temperature_scale *
        (e.nomConT * (p->temperature_max - p->temperature_min) + p->temperature_min);
    if (e.T4_total < p->temperature_min || e.T4_total > p->temperature_max)
        e.flags |= OUTPUT_OUTSIDE_TEMPERATURE_SCALE;
    if (!isfinite(e.nomConT) || !isfinite(e.T4_total) || e.T4_total <= 0.0) return e;
    e.ox_fuel_ratio = e.W_ox_effective / e.Wg_effective;
    e.fuel_ox_ratio = e.Wg_effective / e.W_ox_effective;
    if (!isfinite(e.ox_fuel_ratio) || !isfinite(e.fuel_ox_ratio)) return e;
    Gas gas;
    if (gas_build_from_effective_flows(e.W_ox_effective, e.Wg_effective,
                                       p_chamber, e.T4_total, &gas) != GAS_OK) return e;
    e.W4 = gas.mass_flow;
    e.P4_total = gas.total_pressure;
    e.P4_static = gas.static_pressure;
    e.T4_static = gas.static_temperature;
    e.properties = gas.properties;
    if (e.properties.cp_source != e.properties.cp_for_turbine)
        e.flags |= SOURCE_CP_DIFFERS_FROM_TURBINE_CP;
    e.status = GG_OK;
    return e;
}

GasGeneratorStatus gas_generator_apply_state(GasGeneratorView *view,
                                             const GasGeneratorEval *e)
{
    if (!view || !e) return GG_INVALID_INPUT;
    if (e->status != GG_OK) return GG_INVALID_OUTPUT;
    /* Validate the snapshot, without recomputing ELM, floors or mass flow. */
    const double values[] = {e->p_chamber, e->W_ox_requested, e->Wg_requested,
        e->W_ox_effective, e->Wg_effective, e->W4, e->P4_total, e->P4_static,
        e->T4_total, e->T4_static, e->ox_fuel_ratio, e->fuel_ox_ratio,
        e->nomO2, e->nomWf, e->nomConP, e->nomConT, e->properties.cp_source,
        e->properties.gamma_for_turbine, e->properties.R_for_turbine, e->properties.cp_for_turbine};
    for (unsigned i = 0; i < sizeof values / sizeof values[0]; ++i)
        if (!isfinite(values[i])) return GG_INVALID_OUTPUT;
    if (e->T4_total <= 0.0 || e->T4_static <= 0.0 || e->P4_total <= 0.0 || e->P4_static <= 0.0 ||
        e->W_ox_effective <= 0.0 || e->Wg_effective <= 0.0 || e->W4 <= 0.0 ||
        e->properties.property_source != TURBINE_CALIBRATED)
        return GG_INVALID_OUTPUT;
    for (int i = 0; i < GAS_GENERATOR_HIDDEN_COUNT; ++i)
        if (!isfinite(e->preactivation[i]) || !isfinite(e->activation[i]) ||
            e->activation[i] < 0.0 || e->activation[i] > 1.0) return GG_INVALID_OUTPUT;
    *view = *e;
    return GG_OK;
}
