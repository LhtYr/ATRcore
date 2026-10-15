#ifndef ATRCORE_SHAFT_H
#define ATRCORE_SHAFT_H
typedef struct MechanicalEval {
    double domega_mech; /* rad/s^2 */
    double dtheta_mech; /* rad/s */
} MechanicalEval;
typedef struct ShaftParams {
    double J_sum; /* kg m^2, >0 */
    double b_sum; /* N m s/rad, >=0 */
} ShaftParams;
#endif
