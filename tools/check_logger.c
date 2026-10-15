#include "check_logger.h"
#include "check_s02.h"
#include "Logger.h"
#include <float.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include "logger_expected.h"

enum { EXPECTED_COUNT = sizeof(expected_columns)/sizeof(expected_columns[0]), BUFFER_CAPACITY = 16384 };
typedef struct Fixture {
    BoundarySnapshot boundary;
    SystemState state;
    EvalResult eval;
    LogSnapshot snapshot;
} Fixture;
static void fixture_init(Fixture *f)
{
    memset(f, 0, sizeof(*f));
    f->boundary = (BoundarySnapshot){{3600000, 2.05, 5.1, 100000, 7}, 2};
    f->state = (SystemState){1.2345678901234567, -123.45678901234567, -123, 19999.999};
    f->eval.status = EVAL_OK;
    f->eval.deriv = (SystemDeriv){1e-200, -1e-190, 1e200, -1e210};
    f->eval.alg.gas_generator = (GasGeneratorEval){
        .p_chamber=3600000, .W_ox_requested=0, .Wg_requested=-2,
        .W_ox_effective=2e-5, .Wg_effective=1e-5, .W4=3e-5,
        .P4_total=3500000, .T4_total=1123.456789012345, .T4_static=910, .P4_static=3400000,
        .ox_fuel_ratio=2, .fuel_ox_ratio=0.5, .nomO2=-0.25, .nomWf=0.125, .nomConP=0.375, .nomConT=1.25,
        .properties={2860,1.18,380,2491.111111111111,TURBINE_CALIBRATED},
        .flags=FUEL_FLOORED | OX_FLOORED | INPUT_OUTSIDE_NORMALIZATION_RANGE |
            OUTPUT_OUTSIDE_TEMPERATURE_SCALE | SOURCE_CP_DIFFERS_FROM_TURBINE_CP, .status=GG_OK
    };
    f->eval.alg.turbine = (TurbineEval){
        .p_plenum=20000, .p_downstream=20001, .m_dot_in=0.01, .m_dot_turb=0.02, .m_dot_out=0.03,
        .T6_total=900, .T6_ideal=850, .efficiency=0.7, .speed_ratio=0.4,
        .P_turb=12345.678901234567, .P_shaft=-9876.543210987654, .T_turb=1.23456789e-8,
        .flags=UINT_MAX
    };
    f->snapshot = (LogSnapshot){.step_start_s=2, .step_h_s=0,
        .boundary=&f->boundary, .state=&f->state, .evaluation=&f->eval};
}
static void fixture_step_end(Fixture *f)
{
    f->snapshot.step_h_s = 0.125;
    f->snapshot.iteration_enabled = 1;
    f->snapshot.stages[0] = (StageIterationLog){2,1,0,1e-6,1e-4};
    f->snapshot.stages[1] = (StageIterationLog){3,1,0,2e-6,2e-4};
    f->snapshot.stages[2] = (StageIterationLog){4,1,0,3e-6,3e-4};
    f->snapshot.stages[3] = (StageIterationLog){80,0,1,0.01,3};
    f->snapshot.stages[4] = (StageIterationLog){5,1,0,4e-6,4e-4};
    f->eval.status = EVAL_NOT_CONVERGED;
}

/* Strict reader for this numeric CSV dialect; no call to logger extractors. */
static int split(char *line, char **fields, size_t capacity)
{
    char *newline = strchr(line, '\n');
    if (!newline || newline[1] != '\0') return -1;
    *newline = '\0';
    size_t count = 0;
    char *start = line;
    for (;;) {
        if (count >= capacity || !*start) return -1;
        fields[count++] = start;
        char *comma = strchr(start, ',');
        if (!comma) break;
        *comma = '\0';
        start = comma + 1;
    }
    return (int)count;
}
static double expected_value(size_t column, int row)
{
    if (!row) return expected_columns[column].value;
    if (column == 0) return 2.125;
    if (column == 3) return 0.125;
    if (column == 53) return 4; /* EVAL_NOT_CONVERGED, independent numeric code */
    if (column == 54) return 1;
    const double stage_values[5][5] = {
        {2,1,0,1e-6,1e-4}, {3,1,0,2e-6,2e-4}, {4,1,0,3e-6,3e-4},
        {80,0,1,0.01,3}, {5,1,0,4e-6,4e-4}
    };
    if (column >= 55) return stage_values[(column-55)/5][(column-55)%5];
    return expected_columns[column].value;
}
static int readback(const char *path, double *max_error, int *values_read)
{
    FILE *file = fopen(path, "rb");
    if (!file) return 0;
    char line[BUFFER_CAPACITY], *fields[EXPECTED_COUNT];
    int ok = fgets(line, sizeof(line), file) != NULL;
    if (ok) ok = split(line, fields, EXPECTED_COUNT) == EXPECTED_COUNT;
    for (size_t i = 0; ok && i < EXPECTED_COUNT; ++i)
        ok = strcmp(fields[i], expected_columns[i].name) == 0;
    for (int row = 0; ok && row < 2; ++row) {
        ok = fgets(line, sizeof(line), file) != NULL;
        if (ok) ok = split(line, fields, EXPECTED_COUNT) == EXPECTED_COUNT;
        for (size_t i = 0; ok && i < EXPECTED_COUNT; ++i) {
            char *end;
            const double actual = strtod(fields[i], &end);
            const double expected = expected_value(i, row);
            ok = end != fields[i] && *end == '\0' && isfinite(actual) && actual == expected;
            if (ok) {
                *max_error = fmax(*max_error, fabs(actual - expected));
                ++*values_read;
            } else printf("  readback mismatch row=%d column=%zu (%s)\n", row, i, expected_columns[i].name);
        }
    }
    if (ok) ok = fgetc(file) == EOF && !ferror(file);
    if (fclose(file) != 0) ok = 0;
    return ok;
}

typedef struct TestSink {
    char bytes[BUFFER_CAPACITY];
    size_t used;
    int writes, flushes, closes;
    int fail_write, fail_flush, fail_close;
} TestSink;
static int sink_write(void *context, const char *bytes, size_t size)
{
    TestSink *sink = context;
    ++sink->writes;
    if (sink->fail_write) return 0;
    if (size >= sizeof(sink->bytes) - sink->used) return 0;
    memcpy(sink->bytes + sink->used, bytes, size);
    sink->used += size;
    sink->bytes[sink->used] = '\0';
    return 1;
}
static int sink_flush(void *context)
{
    TestSink *sink = context;
    ++sink->flushes;
    return !sink->fail_flush;
}
static int sink_close(void *context)
{
    TestSink *sink = context;
    ++sink->closes;
    return !sink->fail_close;
}
static LoggerStatus memory_init(SimulationLogger *logger, TestSink *sink)
{
    memset(sink, 0, sizeof(*sink));
    *logger = (SimulationLogger){0};
    return SimulationLogger_init_sink(logger, (LoggerSink){sink,sink_write,sink_flush,sink_close});
}
static int rejects_snapshot(LogSnapshot *snapshot, LoggerStatus expected)
{
    TestSink sink;
    SimulationLogger logger;
    int ok = memory_init(&logger, &sink) == LOGGER_OK && SimulationLogger_write_header(&logger) == LOGGER_OK;
    const size_t before = sink.used;
    if (ok) ok = SimulationLogger_log_snapshot(&logger, snapshot) == expected &&
        sink.used == before && logger.rows_written == 0;
    const LoggerStatus closed = SimulationLogger_close(&logger);
    return ok && closed == expected && sink.closes == 1;
}

int check_logger(const char *csv_path)
{
    CheckCount count = {0};
    size_t column_count;
    const LoggerColumn *columns = SimulationLogger_columns(&column_count);
    int schema_ok = column_count == EXPECTED_COUNT && column_count == 80;
    for (size_t i = 0; schema_ok && i < column_count; ++i) {
        schema_ok = !strcmp(columns[i].name, expected_columns[i].name) &&
            !strcmp(columns[i].unit, expected_columns[i].unit) && columns[i].extract;
        for (size_t j = 0; schema_ok && j < i; ++j) schema_ok = strcmp(columns[i].name, columns[j].name) != 0;
    }
    check_assert(&count, schema_ok, "80 unique columns: independent names/order/units contract");
    Fixture fixture;
    fixture_init(&fixture);
    unsigned char before[sizeof(fixture)];
    memcpy(before, &fixture, sizeof(fixture));
    SimulationLogger logger = {0};
    const int opened = SimulationLogger_init(&logger, csv_path) == LOGGER_OK;
    check_assert(&count, opened, "open requested CSV (parent must already exist)");
    if (!opened) {
        fprintf(stderr, "%s: %s\n", csv_path, SimulationLogger_status_string(logger.status));
        return check_finish(&count, "logger");
    }
    check_assert(&count, SimulationLogger_write_header(&logger) == LOGGER_OK &&
        SimulationLogger_log_snapshot(&logger, &fixture.snapshot) == LOGGER_OK,
        "write known initial snapshot with h=0");
    check_assert(&count, !memcmp(before, &fixture, sizeof(fixture)), "logging leaves inputs/state/eval/diagnostics byte-identical");
    fixture_step_end(&fixture);
    memcpy(before, &fixture, sizeof(fixture));
    check_assert(&count, SimulationLogger_log_snapshot(&logger, &fixture.snapshot) == LOGGER_OK &&
        !memcmp(before, &fixture, sizeof(fixture)), "step-end t+h and stage diagnostics serialize without mutation");
    check_assert(&count, logger.rows_written == 2 && SimulationLogger_flush(&logger) == LOGGER_OK &&
        SimulationLogger_close(&logger) == LOGGER_OK, "two rows explicitly flushed and closed successfully");
    double max_error = 0;
    int values_read = 0;
    check_assert(&count, readback(csv_path, &max_error, &values_read) && values_read == 160,
        "C readback: exact header/row width and 160 independent numeric expectations");
    printf("  CSV=%s rows=2 columns=%zu values_read=%d max_abs_error=%.17g\n",
        csv_path, column_count, values_read, max_error);
    puts("  fixture only: t=2 -> t+h=2.125; raw p=[-123,19999.999], protected=[20000,20001];\n"
         "  k4 limit_hit=1 / converged=0 / count=80; final converged=1; eval_status=4 retained.\n"
         "  These are synthetic serialization values, not an engine simulation or solver test.");

    TestSink sink;
    fixture_init(&fixture);
    (void)memory_init(&logger, &sink);
    check_assert(&count, SimulationLogger_log_snapshot(&logger, &fixture.snapshot) == LOGGER_BAD_STATE &&
        sink.used == 0, "row before header rejected");
    (void)SimulationLogger_close(&logger);
    (void)memory_init(&logger, &sink);
    (void)SimulationLogger_write_header(&logger);
    const size_t header_bytes = sink.used;
    check_assert(&count, SimulationLogger_write_header(&logger) == LOGGER_BAD_STATE && sink.used == header_bytes,
        "duplicate header rejected without additional bytes");
    (void)SimulationLogger_close(&logger);
    (void)memory_init(&logger, &sink);
    check_assert(&count, SimulationLogger_init(&logger, "build/should-not-open.csv") == LOGGER_BAD_STATE &&
        SimulationLogger_close(&logger) == LOGGER_BAD_STATE && sink.closes == 1,
        "reinitialization of active logger rejected; original sink released");
    check_assert(&count, SimulationLogger_close(&logger) == LOGGER_BAD_STATE && sink.closes == 1,
        "repeated close does not double-close");
    (void)memory_init(&logger, &sink);
    (void)SimulationLogger_close(&logger);
    check_assert(&count, SimulationLogger_write_header(&logger) == LOGGER_BAD_STATE, "write after close rejected");

    (void)memory_init(&logger, &sink);
    sink.fail_write = 1;
    check_assert(&count, SimulationLogger_write_header(&logger) == LOGGER_IO_ERROR && !logger.header_written,
        "injected header write failure reported");
    sink.fail_write = 0;
    check_assert(&count, SimulationLogger_write_header(&logger) == LOGGER_IO_ERROR && sink.writes == 1 &&
        SimulationLogger_close(&logger) == LOGGER_IO_ERROR && sink.closes == 1,
        "I/O error remains sticky; retries suppressed; resource closes");
    (void)memory_init(&logger, &sink);
    (void)SimulationLogger_write_header(&logger);
    sink.fail_write = 1;
    check_assert(&count, SimulationLogger_log_snapshot(&logger, &fixture.snapshot) == LOGGER_IO_ERROR &&
        logger.rows_written == 0, "injected data write failure not counted as successful row");
    (void)SimulationLogger_close(&logger);
    (void)memory_init(&logger, &sink);
    sink.fail_flush = 1;
    check_assert(&count, SimulationLogger_flush(&logger) == LOGGER_IO_ERROR &&
        SimulationLogger_close(&logger) == LOGGER_IO_ERROR && sink.closes == 1,
        "injected explicit flush failure returned and closed");
    (void)memory_init(&logger, &sink);
    sink.fail_flush = 1;
    check_assert(&count, SimulationLogger_close(&logger) == LOGGER_IO_ERROR && sink.closes == 1,
        "buffered failure during final flush returned; close still executed");
    (void)memory_init(&logger, &sink);
    sink.fail_close = 1;
    check_assert(&count, SimulationLogger_close(&logger) == LOGGER_IO_ERROR && sink.closes == 1,
        "injected close failure returned");
    logger = (SimulationLogger){0};
    check_assert(&count, SimulationLogger_init(&logger, "build/no-such-s03-directory/output.csv") == LOGGER_IO_ERROR &&
        SimulationLogger_close(&logger) == LOGGER_IO_ERROR, "real fopen failure: no platform mkdir fallback");
    check_assert(&count, SimulationLogger_init(NULL, csv_path) == LOGGER_BAD_ARGUMENT &&
        SimulationLogger_init(&logger, NULL) == LOGGER_BAD_ARGUMENT &&
        SimulationLogger_init_sink(&logger, (LoggerSink){0}) == LOGGER_BAD_ARGUMENT &&
        SimulationLogger_flush(NULL) == LOGGER_BAD_ARGUMENT &&
        SimulationLogger_close(NULL) == LOGGER_BAD_ARGUMENT, "null/invalid lifecycle arguments reported");

    check_assert(&count, rejects_snapshot(NULL, LOGGER_BAD_ARGUMENT), "null snapshot rejected before write");
    fixture.snapshot.state = NULL;
    check_assert(&count, rejects_snapshot(&fixture.snapshot, LOGGER_BAD_ARGUMENT), "missing observation pointer rejected");
    fixture_init(&fixture);
    fixture.boundary.sampled_at_s = 1;
    check_assert(&count, rejects_snapshot(&fixture.snapshot, LOGGER_BAD_TIME), "boundary time cannot be mistaken for final time");
    fixture_init(&fixture);
    fixture.snapshot.step_h_s = -1;
    check_assert(&count, rejects_snapshot(&fixture.snapshot, LOGGER_BAD_TIME), "negative step duration rejected");
    fixture.snapshot.step_h_s = NAN;
    check_assert(&count, rejects_snapshot(&fixture.snapshot, LOGGER_BAD_TIME), "nonfinite time rejected");
    fixture.snapshot.step_start_s = DBL_MAX;
    fixture.snapshot.step_h_s = DBL_MAX;
    check_assert(&count, rejects_snapshot(&fixture.snapshot, LOGGER_BAD_TIME), "overflowing final time rejected");
    fixture_init(&fixture);
    fixture.snapshot.step_h_s = DBL_TRUE_MIN;
    check_assert(&count, rejects_snapshot(&fixture.snapshot, LOGGER_BAD_TIME), "positive h lost to time rounding rejected");
    fixture_init(&fixture);
    fixture.state.theta_mech = NAN;
    check_assert(&count, rejects_snapshot(&fixture.snapshot, LOGGER_NONFINITE), "NaN value rejected without partial row");
    fixture_init(&fixture);
    fixture.eval.alg.gas_generator.T4_total = INFINITY;
    check_assert(&count, rejects_snapshot(&fixture.snapshot, LOGGER_NONFINITE), "infinite observation rejected without partial row");
    fixture_init(&fixture);
    fixture.snapshot.stages[0].converged = 1;
    check_assert(&count, rejects_snapshot(&fixture.snapshot, LOGGER_BAD_DIAGNOSTIC), "disabled iteration cannot claim convergence");
    fixture.snapshot.iteration_enabled = 1;
    fixture.snapshot.stages[0].limit_hit = 1;
    check_assert(&count, rejects_snapshot(&fixture.snapshot, LOGGER_BAD_DIAGNOSTIC), "limit hit and convergence cannot both be true");
    fixture_init(&fixture);
    fixture.snapshot.iteration_enabled = 2;
    check_assert(&count, rejects_snapshot(&fixture.snapshot, LOGGER_BAD_DIAGNOSTIC), "nonboolean iteration flag rejected");
    fixture_init(&fixture);
    fixture.snapshot.iteration_enabled = 1;
    fixture.snapshot.stages[0].iterations = -1;
    check_assert(&count, rejects_snapshot(&fixture.snapshot, LOGGER_BAD_DIAGNOSTIC), "negative iteration count rejected");
    fixture.snapshot.stages[0].iterations = 1;
    fixture.snapshot.stages[0].delta_pressure = -1;
    check_assert(&count, rejects_snapshot(&fixture.snapshot, LOGGER_BAD_DIAGNOSTIC), "negative increment magnitude rejected");

    /* Extreme finite values and signed zero: one column exercises the common
     * formatter, with C strtod and signbit as independent readback. */
    fixture_init(&fixture);
    const double extremes[] = {DBL_MAX, DBL_MIN, DBL_TRUE_MIN, -DBL_MAX, -0.0, nextafter(1.0, 2.0)};
    int extreme_ok = 1;
    for (size_t i = 0; i < sizeof(extremes)/sizeof(extremes[0]); ++i) {
        (void)memory_init(&logger, &sink);
        (void)SimulationLogger_write_header(&logger);
        fixture.state.theta_mech = extremes[i];
        extreme_ok = extreme_ok && SimulationLogger_log_snapshot(&logger, &fixture.snapshot) == LOGGER_OK;
        char *fields[EXPECTED_COUNT];
        char *line = strchr(sink.bytes, '\n');
        if (!line || split(line + 1, fields, EXPECTED_COUNT) != EXPECTED_COUNT) extreme_ok = 0;
        else {
            char *end;
            const double value = strtod(fields[9], &end);
            extreme_ok = extreme_ok && *end == '\0' && value == extremes[i] &&
                (!!signbit(value) == !!signbit(extremes[i]));
        }
        if (SimulationLogger_close(&logger) != LOGGER_OK) extreme_ok = 0;
    }
    check_assert(&count, extreme_ok, "17-digit formatter round-trips finite extremes, adjacent double and signed zero");
    return check_finish(&count, "logger");
}
