#include "check_s02.h"
#include "utils/param_file.h"
#include <stddef.h>

#define DOUBLE_FIELD(type, field) _Static_assert(_Generic(((type *)0)->field, double: 1, default: 0), #type "." #field " must be double")
DOUBLE_FIELD(SystemState, theta_mech);
DOUBLE_FIELD(SystemState, omega_mech);
DOUBLE_FIELD(SystemState, p_plenum);
DOUBLE_FIELD(SystemState, p_downstream);
DOUBLE_FIELD(SystemDeriv, dtheta_mech);
DOUBLE_FIELD(SystemDeriv, domega_mech);
DOUBLE_FIELD(SystemDeriv, dp_plenum);
DOUBLE_FIELD(SystemDeriv, dp_downstream);
DOUBLE_FIELD(SystemInput, p_chamber);
DOUBLE_FIELD(SystemInput, W_ox);
DOUBLE_FIELD(SystemInput, Wg);
DOUBLE_FIELD(SystemInput, p_out);
DOUBLE_FIELD(SystemInput, T_load);
#undef DOUBLE_FIELD

int check_contracts(void)
{
    CheckCount count = {0};
    const SystemConfig config = system_config_defaults();
    check_assert(&count, sizeof(SystemState) == 4 * sizeof(double) &&
        sizeof(SystemDeriv) == 4 * sizeof(double) && sizeof(SystemInput) == 5 * sizeof(double),
        "exact 4/4/5 dimensions; all 13 fields compile-time double");
    check_assert(&count, offsetof(SystemState, p_downstream) == 3 * sizeof(double) &&
        offsetof(SystemInput, T_load) == 4 * sizeof(double), "contract field order");
    check_assert(&count, config.boundary.target.p_chamber == 101325.0 &&
        config.boundary.target.W_ox == 0.0001 && config.boundary.target.Wg == 0.0004 &&
        config.boundary.target.p_out == 100000.0 && config.boundary.target.T_load == 0.0,
        "default inputs: Pa, kg/s LOX, kg/s fuel, Pa, N m");
    check_assert(&count, config.initial.theta_mech == 0.0 && config.initial.omega_mech == 0.0 &&
        config.initial.p_plenum == 400000.0 && config.initial.p_downstream == 400000.0,
        "initial units: rad, rad/s, Pa, Pa; no conversion or projection");
    check_assert(&count, check_near(config.shaft.J_sum, 0.000774, 1e-15) &&
        check_near(config.shaft.b_sum, 0.0048, 1e-15), "reference software J_sum/b_sum");
    const EvalResult result = {0};
    check_assert(&count, result.status != EVAL_OK && result.alg.gas_generator.status != GG_OK,
        "zero initialized evaluation is not success");
    check_assert(&count, sizeof(result.alg.gas_generator.preactivation) == 50 * sizeof(double) &&
        sizeof(result.alg.gas_generator.activation) == 50 * sizeof(double) &&
        sizeof(CoupledIterVars) == 3 * sizeof(double), "50-node diagnostics; 3 local iteration values");
    GasProperties properties = {2860.0, 1.18, 380.0, 2491.111111111111, TURBINE_CALIBRATED};
    check_assert(&count, properties.cp_source != properties.cp_for_turbine &&
        properties.property_source == TURBINE_CALIBRATED, "Cp source and turbine Cp remain separate data");
    puts("Units are explicit header/config contracts, not runtime dimensional types.\n"
         "No physical evaluator, torque calculation or RK4 integration tested in S02.");
    return check_finish(&count, "contracts");
}
