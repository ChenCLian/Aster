#ifndef ASTER_ERROR_INFO_H
#define ASTER_ERROR_INFO_H
#include <QJsonObject>
#include <QStringList>

namespace aster
{
// Stable serialized names, independent of enum order. Extensions may also use
// their own namespaced string code without changing this built-in list.
#define ASTER_ERROR_CODES(X) \
    X(Unknown, "UNCLASSIFIED", "Task failed") \
    X(InputType, "INPUT_TYPE", "Expected a compatible input type") \
    X(InputInvalid, "INPUT_INVALID", "Input data is invalid") \
    X(InputUnavailable, "INPUT_PORT_UNAVAILABLE", "The selected parent output was not produced") \
    X(TooFewSamples, "INPUT_TOO_FEW_SAMPLES", "Too few input samples") \
    X(XDuplicate, "INPUT_X_DUPLICATE", "Duplicate X encountered") \
    X(XDescending, "INPUT_X_DESCENDING", "X must be strictly increasing") \
    X(XNonUniform, "INPUT_X_NONUNIFORM", "Sampling is not uniform") \
    X(InvalidSpacing, "INPUT_SPACING_INVALID", "Invalid sampling interval") \
    X(TooFewDistinctX, "INPUT_TOO_FEW_DISTINCT_X", "At least two distinct X values required") \
    X(ResampleGrid, "RESAMPLE_GRID_INVALID", "Resampling grid must contain 2..1000000 samples") \
    X(ResampleResolution, "RESAMPLE_COORDINATE_RESOLUTION", "Resampling coordinates cannot be represented as strictly increasing doubles") \
    X(InterpolationSamples, "INTERPOLATION_TOO_FEW_SAMPLES", "Too few samples for selected interpolation") \
    X(InterpolationFailure, "INTERPOLATION_FAILED", "Interpolation failed") \
    X(DegenerateX, "LINEAR_DETREND_X_DEGENERATE", "Degenerate or numerically unrepresentable x range") \
    X(NumericRange, "NUMERIC_RANGE", "Calculation exceeds the representable numeric range") \
    X(LowessFailure, "LOWESS_FAILED", "LOWESS failed") \
    X(MtmNW, "MTM_NW_INVALID", "NW must be less than sample count / 2") \
    X(MtmFailure, "MTM_FAILED", "MTM calculation failed") \
    X(MtmSimulationFailure, "MTM_SIMULATION_FAILED", "MTM simulation test failed") \
    X(Ar1GlobalFailure, "AR1_GLOBAL_FAILED", "AR1 global test failed") \
    X(WaveletSamplesLimit, "CWT_SAMPLE_LIMIT", "Wavelet sample limit exceeded") \
    X(WaveletMorlet, "CWT_MORLET_PARAMETER", "This uncorrected Morlet implementation requires parameter >= 5") \
    X(WaveletOrder, "CWT_ORDER_INVALID", "Paul requires integer order; DOG requires even integer order") \
    X(WaveletGridLimit, "CWT_GRID_LIMIT", "Wavelet grid limit exceeded") \
    X(AllocationFailure, "MEMORY_ALLOCATION_FAILED", "Memory allocation failed") \
    X(FileOptions, "FILE_OPTIONS_INVALID", "Invalid file import options") \
    X(FileOpen, "FILE_OPEN_FAILED", "Could not open the input file") \
    X(FileLineLimit, "FILE_LINE_LIMIT", "File line exceeds the supported length") \
    X(FileColumns, "FILE_COLUMNS_MISSING", "Header does not contain the selected columns") \
    X(FileNumeric, "FILE_NUMERIC_INVALID", "Selected columns contain an invalid numeric row") \
    X(FileRowLimit, "FILE_ROW_LIMIT", "File row limit exceeded") \
    X(FileRead, "FILE_READ_FAILED", "Text decoding/read failed") \
    X(FileRows, "FILE_TOO_FEW_ROWS", "At least two valid rows are required") \
    X(SourceMissing, "SOURCE_MISSING", "Input source is missing") \
    X(DatabaseReference, "DATABASE_REFERENCE_INVALID", "Choose a valid database output as source") \
    X(DatabaseOpen, "DATABASE_OPEN_FAILED", "Could not open the source database") \
    X(DatabaseRead, "DATABASE_OUTPUT_READ_FAILED", "Could not read the referenced database output") \
    X(DatabaseType, "DATABASE_OUTPUT_TYPE", "Database source type mismatch") \
    X(ParameterInvalid, "TASK_PARAMETERS_INVALID", "Task parameters are invalid") \
    X(OutputInvalid, "TASK_OUTPUT_INVALID", "Task output failed validation") \
    X(TaskException, "TASK_EXCEPTION", "Calculation raised an exception") \
    X(TaskContract, "TASK_CONTRACT_INVALID", "Task returned an invalid execution state") \
    X(UpstreamFailed, "UPSTREAM_UNAVAILABLE", "Parent did not succeed") \
    X(Cancelled, "CANCELLED", "Cancellation requested")

enum class ErrorCode
{
#define ASTER_DECLARE_ERROR(name, key, fallback) name,
    ASTER_ERROR_CODES(ASTER_DECLARE_ERROR)
#undef ASTER_DECLARE_ERROR
};
struct ErrorInfo
{
    QString code;
    QJsonObject context; // Small scalar metadata only; never the numerical payload.
    QString technical;
    bool isEmpty() const { return code.isEmpty(); }
    QJsonObject toJson() const;
    static ErrorInfo fromJson(const QJsonObject &object);
};
QString errorCodeName(ErrorCode code);
QStringList builtinErrorCodes();
QString errorFallback(const ErrorInfo &error);
ErrorInfo makeError(ErrorCode code, QJsonObject context = {}, QString technical = {});
} // namespace aster
#endif
