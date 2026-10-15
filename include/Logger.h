#ifndef ATRCORE_LOGGER_H
#define ATRCORE_LOGGER_H
#include <stddef.h>
#include "solver/BoundaryProfile.h"

enum { LOGGER_STAGE_COUNT = 5 }; /* k1, k2, k3, k4, final; diagnostics only */
typedef struct StageIterationLog {
    int iterations, converged, limit_hit;
    double delta_omega;    /* rad/s, magnitude of last damped increment */
    double delta_pressure; /* Pa, max magnitude of last two pressure increments */
} StageIterationLog;

/* Borrowed, read-only observations at step_start_s + step_h_s.
 * h=0 is an initial/diagnostic snapshot. No evaluator is called by Logger.
 * boundary.sampled_at_s must equal step_start_s (whole-step input hold). */
typedef struct LogSnapshot {
    double step_start_s, step_h_s;
    const BoundarySnapshot *boundary;
    const SystemState *state; /* raw integrated state, including raw pressures */
    const EvalResult *evaluation; /* final observations, protected turbine pressures */
    int iteration_enabled;
    StageIterationLog stages[LOGGER_STAGE_COUNT];
} LogSnapshot;

typedef struct LoggerColumn {
    const char *name; /* ASCII identifier; units included in dimensional names */
    const char *unit; /* SI units, "1", "bool", "count", "enum", or "bitmask" */
    double (*extract)(const LogSnapshot *snapshot);
} LoggerColumn;

typedef enum LoggerStatus {
    LOGGER_OK = 0, LOGGER_BAD_ARGUMENT, LOGGER_BAD_STATE,
    LOGGER_BAD_TIME, LOGGER_NONFINITE, LOGGER_BAD_DIAGNOSTIC,
    LOGGER_BAD_LOCALE, LOGGER_IO_ERROR, LOGGER_FORMAT_ERROR
} LoggerStatus;

/* C11 sink seam for deterministic I/O failure tests and caller-owned streams.
 * Each callback returns 1 on complete success, 0 on failure (no partial retry).
 * close is invoked exactly once by logger_close, even after a prior error. */
typedef struct LoggerSink {
    void *context;
    int (*write)(void *context, const char *bytes, size_t size);
    int (*flush)(void *context);
    int (*close)(void *context);
} LoggerSink;
typedef struct SimulationLogger {
    LoggerSink sink;
    LoggerStatus status; /* First error is sticky until closed and initialized again. */
    int active, header_written;
    size_t rows_written; /* Rows accepted by sink; check flush/close for buffered failures. */
} SimulationLogger;

/* Initialize logger with {0} before first use. init truncates path on success;
 * parent directory must exist. init refuses an already active logger. */
LoggerStatus SimulationLogger_init(SimulationLogger *logger, const char *path);
LoggerStatus SimulationLogger_init_sink(SimulationLogger *logger, LoggerSink sink);
LoggerStatus SimulationLogger_write_header(SimulationLogger *logger);
LoggerStatus SimulationLogger_log_snapshot(SimulationLogger *logger, const LogSnapshot *snapshot);
LoggerStatus SimulationLogger_flush(SimulationLogger *logger);
LoggerStatus SimulationLogger_close(SimulationLogger *logger);
const LoggerColumn *SimulationLogger_columns(size_t *count);
const char *SimulationLogger_status_string(LoggerStatus status);
#endif
