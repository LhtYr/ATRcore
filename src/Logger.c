#include "Logger.h"
#include <float.h>
#include <locale.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

_Static_assert(DBL_DECIMAL_DIG <= 17, "CSV precision requires at most 17 decimal digits");
#define C(name, unit, expr) static double col_##name(const LogSnapshot *s) { return (double)(expr); }
#include "logger_columns.def"
#undef C
static const LoggerColumn columns[] = {
#define C(name, unit, expr) {#name, unit, col_##name},
#include "logger_columns.def"
#undef C
};
enum { COLUMN_COUNT = sizeof(columns) / sizeof(columns[0]), ROW_CAPACITY = 16384 };

static LoggerStatus fail(SimulationLogger *logger, LoggerStatus status)
{
    if (!logger) return LOGGER_BAD_ARGUMENT;
    if (logger->status == LOGGER_OK) logger->status = status;
    return logger->status;
}
static int file_write(void *context, const char *bytes, size_t size)
{
    return fwrite(bytes, 1, size, context) == size;
}
static int file_flush(void *context) { return fflush(context) == 0; }
static int file_close(void *context) { return fclose(context) == 0; }
LoggerStatus SimulationLogger_init_sink(SimulationLogger *logger, LoggerSink sink)
{
    if (!logger) return LOGGER_BAD_ARGUMENT;
    if (logger->active) return fail(logger, LOGGER_BAD_STATE);
    *logger = (SimulationLogger){0};
    if (!sink.context || !sink.write || !sink.flush || !sink.close)
        return fail(logger, LOGGER_BAD_ARGUMENT);
    logger->sink = sink;
    logger->active = 1;
    return LOGGER_OK;
}
LoggerStatus SimulationLogger_init(SimulationLogger *logger, const char *path)
{
    if (!logger) return LOGGER_BAD_ARGUMENT;
    if (logger->active) return fail(logger, LOGGER_BAD_STATE);
    *logger = (SimulationLogger){0};
    if (!path || !*path) return fail(logger, LOGGER_BAD_ARGUMENT);
    FILE *file = fopen(path, "wb"); /* Fixed LF; directory creation belongs to caller/Make. */
    if (!file) return fail(logger, LOGGER_IO_ERROR);
    const LoggerSink sink = {file, file_write, file_flush, file_close};
    return SimulationLogger_init_sink(logger, sink);
}
const LoggerColumn *SimulationLogger_columns(size_t *count)
{
    if (count) *count = COLUMN_COUNT;
    return columns;
}
const char *SimulationLogger_status_string(LoggerStatus status)
{
    switch (status) {
    case LOGGER_OK: return "ok";
    case LOGGER_BAD_ARGUMENT: return "invalid argument";
    case LOGGER_BAD_STATE: return "invalid logger lifecycle";
    case LOGGER_BAD_TIME: return "invalid step or boundary time";
    case LOGGER_NONFINITE: return "nonfinite CSV value";
    case LOGGER_BAD_DIAGNOSTIC: return "invalid iteration diagnostic";
    case LOGGER_BAD_LOCALE: return "CSV requires a dot decimal separator";
    case LOGGER_IO_ERROR: return "CSV I/O failed; output may be incomplete";
    case LOGGER_FORMAT_ERROR: return "CSV row formatting failed";
    }
    return "unknown logger status";
}
static LoggerStatus ready(SimulationLogger *logger)
{
    if (!logger) return LOGGER_BAD_ARGUMENT;
    if (logger->status != LOGGER_OK) return logger->status;
    if (!logger->active) return fail(logger, LOGGER_BAD_STATE);
    if (strcmp(localeconv()->decimal_point, ".") != 0) return fail(logger, LOGGER_BAD_LOCALE);
    return LOGGER_OK;
}
static LoggerStatus write_row(SimulationLogger *logger, const char *row, size_t length)
{
    if (!logger->sink.write(logger->sink.context, row, length)) return fail(logger, LOGGER_IO_ERROR);
    return LOGGER_OK;
}
LoggerStatus SimulationLogger_write_header(SimulationLogger *logger)
{
    LoggerStatus status = ready(logger);
    if (status != LOGGER_OK) return status;
    if (logger->header_written) return fail(logger, LOGGER_BAD_STATE);
    char row[ROW_CAPACITY];
    size_t used = 0;
    for (size_t i = 0; i < COLUMN_COUNT; ++i) {
        const int n = snprintf(row + used, sizeof(row) - used, "%s%s", i ? "," : "", columns[i].name);
        if (n < 0 || (size_t)n >= sizeof(row) - used) return fail(logger, LOGGER_FORMAT_ERROR);
        used += (size_t)n;
    }
    row[used++] = '\n';
    status = write_row(logger, row, used);
    if (status == LOGGER_OK) logger->header_written = 1;
    return status;
}
static LoggerStatus validate(const LogSnapshot *s)
{
    if (!s || !s->boundary || !s->state || !s->evaluation) return LOGGER_BAD_ARGUMENT;
    const double end = s->step_start_s + s->step_h_s;
    if (!isfinite(s->step_start_s) || s->step_start_s < 0.0 ||
        !isfinite(s->step_h_s) || s->step_h_s < 0.0 || !isfinite(end) ||
        (s->step_h_s > 0.0 && end <= s->step_start_s) ||
        s->boundary->sampled_at_s != s->step_start_s) return LOGGER_BAD_TIME;
    if (s->iteration_enabled != 0 && s->iteration_enabled != 1) return LOGGER_BAD_DIAGNOSTIC;
    for (int i = 0; i < LOGGER_STAGE_COUNT; ++i) {
        const StageIterationLog *stage = &s->stages[i];
        if (stage->iterations < 0 || (stage->converged != 0 && stage->converged != 1) ||
            (stage->limit_hit != 0 && stage->limit_hit != 1) ||
            (stage->limit_hit && stage->converged) ||
            stage->delta_omega < 0.0 || stage->delta_pressure < 0.0 ||
            (!s->iteration_enabled && (stage->iterations || stage->converged || stage->limit_hit ||
                stage->delta_omega != 0.0 || stage->delta_pressure != 0.0))) return LOGGER_BAD_DIAGNOSTIC;
    }
    return LOGGER_OK;
}
LoggerStatus SimulationLogger_log_snapshot(SimulationLogger *logger, const LogSnapshot *snapshot)
{
    LoggerStatus status = ready(logger);
    if (status != LOGGER_OK) return status;
    if (!logger->header_written) return fail(logger, LOGGER_BAD_STATE);
    status = validate(snapshot);
    if (status != LOGGER_OK) return fail(logger, status);
    char row[ROW_CAPACITY];
    size_t used = 0;
    /* Build/validate complete row before writing, so invalid values cannot
     * leave a partial row. An actual I/O failure may still leave partial bytes. */
    for (size_t i = 0; i < COLUMN_COUNT; ++i) {
        const double value = columns[i].extract(snapshot);
        if (!isfinite(value)) return fail(logger, LOGGER_NONFINITE);
        const int n = snprintf(row + used, sizeof(row) - used, "%s%.17g", i ? "," : "", value);
        if (n < 0 || (size_t)n >= sizeof(row) - used) return fail(logger, LOGGER_FORMAT_ERROR);
        used += (size_t)n;
    }
    row[used++] = '\n';
    status = write_row(logger, row, used);
    if (status == LOGGER_OK) ++logger->rows_written;
    return status;
}
LoggerStatus SimulationLogger_flush(SimulationLogger *logger)
{
    LoggerStatus status = ready(logger);
    if (status != LOGGER_OK) return status;
    if (!logger->sink.flush(logger->sink.context)) return fail(logger, LOGGER_IO_ERROR);
    return LOGGER_OK;
}
LoggerStatus SimulationLogger_close(SimulationLogger *logger)
{
    if (!logger) return LOGGER_BAD_ARGUMENT;
    if (!logger->active) return logger->status;
    /* Do not short-circuit: resources must close after write/flush failure. */
    if (!logger->sink.flush(logger->sink.context)) (void)fail(logger, LOGGER_IO_ERROR);
    if (!logger->sink.close(logger->sink.context)) (void)fail(logger, LOGGER_IO_ERROR);
    logger->active = 0;
    logger->sink = (LoggerSink){0};
    return logger->status;
}
