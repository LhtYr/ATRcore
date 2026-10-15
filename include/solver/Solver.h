#ifndef ATRCORE_SOLVER_H
#define ATRCORE_SOLVER_H
#include "component/GasGenerator.h"
#include "component/Turbine.h"
#include "component/Shaft.h"

typedef struct SystemState {
    double theta_mech;   /* rad */
    double omega_mech;   /* rad/s */
    double p_plenum;     /* Pa */
    double p_downstream; /* Pa */
} SystemState;
typedef struct SystemDeriv {
    double dtheta_mech;   /* rad/s */
    double domega_mech;   /* rad/s^2 */
    double dp_plenum;     /* Pa/s */
    double dp_downstream; /* Pa/s */
} SystemDeriv;
typedef struct SystemInput {
    double p_chamber; /* Pa: chamber pressure, NOT old p23_total */
    double W_ox;      /* kg/s: requested LOX flow (W23 is documentation only) */
    double Wg;        /* kg/s: requested fuel flow */
    double p_out;     /* Pa */
    double T_load;    /* N m: positive absorbs shaft energy */
} SystemInput;
typedef struct SystemAlgebraic {
    GasGeneratorEval gas_generator;
    TurbineEval turbine;
    MechanicalEval mechanical;
} SystemAlgebraic;
typedef enum EvalStatus {
    EVAL_NOT_EVALUATED = 0, EVAL_OK, EVAL_INVALID_INPUT,
    EVAL_COMPONENT_FAILED, EVAL_NOT_CONVERGED
} EvalStatus;
typedef struct EvalResult {
    SystemDeriv deriv;
    SystemAlgebraic alg;
    EvalStatus status; /* Only EVAL_OK authorizes consuming deriv. Zero is failure. */
} EvalResult;
typedef struct CoupledIterVars {
    double omega_mech, p_plenum, p_downstream; /* rad/s, Pa, Pa; local work only */
} CoupledIterVars;

_Static_assert(sizeof(SystemState) == 4 * sizeof(double), "four states only");
_Static_assert(sizeof(SystemDeriv) == 4 * sizeof(double), "four derivatives only");
_Static_assert(sizeof(SystemInput) == 5 * sizeof(double), "five external inputs");
#endif
