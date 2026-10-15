#ifndef ATRCORE_TURBINE_H
#define ATRCORE_TURBINE_H
/* Data contract; the reference two-plenum algorithm follows in S06. */
typedef struct TurbineEval {
    double dp_plenum, dp_downstream;       /* Pa/s */
    double T_turb;                        /* N m, positive driving torque */
    double m_dot_in, m_dot_turb, m_dot_out; /* kg/s */
    double p_plenum, p_downstream;         /* Pa */
    double T6_total, T6_ideal;             /* K */
    double efficiency, speed_ratio;       /* dimensionless, u/c */
    double P_turb, P_shaft;                /* W */
    unsigned flags;
} TurbineEval;
#endif
