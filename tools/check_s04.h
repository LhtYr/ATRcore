#ifndef ATRCORE_CHECK_S04_H
#define ATRCORE_CHECK_S04_H
int check_gas(void);
int check_numeric(const char *csv_path);
int check_numeric_csv_pair(const char *first, const char *second);
/* Test-only transcription of EngineSim C_exp; argument must be finite. */
double reference_exp(double a);
#endif
