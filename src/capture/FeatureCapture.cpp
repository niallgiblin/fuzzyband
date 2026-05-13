#include "capture/FeatureCapture.h"
#include <ctime>
#include <iomanip>
#include <sstream>

#ifndef MA_PLUGIN_VERSION
#define MA_PLUGIN_VERSION "0.0.0"
#endif

namespace
{
const char* stateName(StructureState state) noexcept
{
    switch (state)
    {
        case StructureState::SILENT: return "SILENT";
        case StructureState::SOFT: return "SOFT";
        case StructureState::LOUD: return "LOUD";
    }
    return "UNKNOWN";
}

std::string jsonEscape(const std::string& value)
{
    std::string escaped;
    escaped.reserve(value.size() + 8);
    for (char c : value)
    {
        switch (c)
        {
            case '\\': escaped += "\\\\"; break;
            case '"': escaped += "\\\""; break;
            case '\n': escaped += "\\n"; break;
            case '\r': escaped += "\\r"; break;
            case '\t': escaped += "\\t"; break;
            default: escaped += c; break;
        }
    }
    return escaped;
}

std::string quoted(const std::string& value)
{
    return "\"" + jsonEscape(value) + "\"";
}

std::string utcTimestampForFile()
{
    const std::time_t now = std::time(nullptr);
    std::tm utc{};
#if defined(_WIN32)
    gmtime_s(&utc, &now);
#else
    gmtime_r(&now, &utc);
#endif
    std::ostringstream out;
    out << std::put_time(&utc, "%Y%m%dT%H%M%SZ");
    return out.str();
}

std::string utcTimestampIso()
{
    const std::time_t now = std::time(nullptr);
    std::tm utc{};
#if defined(_WIN32)
    gmtime_s(&utc, &now);
#else
    gmtime_r(&now, &utc);
#endif
    std::ostringstream out;
    out << std::put_time(&utc, "%Y-%m-%dT%H:%M:%SZ");
    return out.str();
}

std::string patternNameOrNull(const std::string& name)
{
    return name.empty() ? "null" : quoted(name);
}

juce::File resolveCaptureDirectory()
{
    const juce::File roots[] = {
        juce::File::getSpecialLocation(juce::File::userDocumentsDirectory),
        juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory),
        juce::File::getSpecialLocation(juce::File::tempDirectory),
        juce::File("/private/tmp"),
        juce::File("/tmp")
    };

    for (const auto& root : roots)
    {
        auto dir = root.getChildFile("MetalAccompaniment").getChildFile("captures");
        const auto result = dir.createDirectory();
        if (result.wasOk() || dir.isDirectory())
            return dir;
    }

    return {};
}

} // namespace

FeatureCapture::FeatureCapture() = default;

FeatureCapture::~FeatureCapture()
{
    stop();
}

void FeatureCapture::prepare(double sr, int bs, std::string modelMode)
{
    sampleRate = sr > 0.0 ? sr : 44100.0;
    blockSize = juce::jmax(0, bs);
    preparedModelMode = std::move(modelMode);
}

bool FeatureCapture::start()
{
    if (capturing.load(std::memory_order_acquire))
        return true;

    stop();

    rowsWritten.store(0, std::memory_order_relaxed);
    droppedRows.store(0, std::memory_order_relaxed);
    stopRequested.store(false, std::memory_order_release);

    auto dir = resolveCaptureDirectory();
    if (!dir.exists())
        return false;

    captureFile = dir.getChildFile("feature_capture_" + utcTimestampForFile() + ".jsonl");
    capturing.store(true, std::memory_order_release);
    writerThread = std::thread([this] { writerLoop(); });
    return true;
}

void FeatureCapture::stop()
{
    stopRequested.store(true, std::memory_order_release);
    capturing.store(false, std::memory_order_release);
    if (writerThread.joinable())
        writerThread.join();

    FeatureCaptureRow discarded{};
    while (queue.try_dequeue(discarded)) {}
}

std::string FeatureCapture::getCapturePath() const
{
    return captureFile.getFullPathName().toStdString();
}

bool FeatureCapture::tryPush(const FeatureCaptureRow& row)
{
    if (!capturing.load(std::memory_order_acquire))
        return false;

    if (row.elapsedSeconds >= kMaxCaptureSeconds || rowsWritten.load(std::memory_order_relaxed) >= kMaxRows)
    {
        capturing.store(false, std::memory_order_release);
        stopRequested.store(true, std::memory_order_release);
        return false;
    }

    if (!queue.try_enqueue(row))
    {
        droppedRows.fetch_add(1, std::memory_order_relaxed);
        return false;
    }

    return true;
}

void FeatureCapture::writerLoop()
{
    std::ofstream out(captureFile.getFullPathName().toStdString(), std::ios::out | std::ios::trunc);
    if (!out.is_open())
    {
        capturing.store(false, std::memory_order_release);
        return;
    }

    writeHeader(out);

    while (!stopRequested.load(std::memory_order_acquire))
    {
        FeatureCaptureRow row{};
        bool wroteAny = false;
        while (queue.try_dequeue(row))
        {
            writeRow(out, row);
            wroteAny = true;
            const int total = rowsWritten.fetch_add(1, std::memory_order_relaxed) + 1;
            if (total >= kMaxRows || row.elapsedSeconds >= kMaxCaptureSeconds)
            {
                capturing.store(false, std::memory_order_release);
                stopRequested.store(true, std::memory_order_release);
                break;
            }
        }

        if (wroteAny)
            out.flush();
        juce::Thread::sleep(20);
    }

    FeatureCaptureRow row{};
    while (queue.try_dequeue(row))
    {
        writeRow(out, row);
        rowsWritten.fetch_add(1, std::memory_order_relaxed);
    }
    out.flush();
}

void FeatureCapture::writeHeader(std::ofstream& out) const
{
    out << "{\"type\":\"header\","
        << "\"schema_version\":\"feature_capture.v1\","
        << "\"plugin_version\":" << quoted(MA_PLUGIN_VERSION) << ","
        << "\"sample_rate\":" << sampleRate << ","
        << "\"block_size\":" << blockSize << ","
        << "\"capture_start_utc\":" << quoted(utcTimestampIso()) << ","
        << "\"model_mode\":" << quoted(preparedModelMode) << ","
        << "\"max_rows\":" << kMaxRows << ","
        << "\"max_capture_seconds\":" << kMaxCaptureSeconds << ","
        << "\"columns\":["
        << "\"sample_timestamp\",\"elapsed_seconds\",\"bpm\",\"rms_energy\","
        << "\"spectral_centroid\",\"high_freq_flux\",\"state\",\"state_name\","
        << "\"pitch_root_midi\",\"pitch_confidence\",\"policy_intensity\",\"rms_delta\","
        << "\"rule_pattern_index\",\"rule_pattern_name\",\"active_pattern_index\","
        << "\"active_pattern_name\",\"onnx_pattern_index\",\"onnx_pattern_name\","
        << "\"onnx_available\",\"model_mode\"]}\n";
}

void FeatureCapture::writeRow(std::ofstream& out, const FeatureCaptureRow& row) const
{
    out << std::setprecision(9)
        << "{\"type\":\"feature\","
        << "\"schema_version\":\"feature_capture.v1\","
        << "\"sample_timestamp\":" << row.sampleTimestamp << ","
        << "\"elapsed_seconds\":" << row.elapsedSeconds << ","
        << "\"bpm\":" << row.bpm << ","
        << "\"rms_energy\":" << row.rmsEnergy << ","
        << "\"spectral_centroid\":" << row.spectralCentroid << ","
        << "\"high_freq_flux\":" << row.highFreqFlux << ","
        << "\"state\":" << static_cast<int>(row.state) << ","
        << "\"state_name\":" << quoted(stateName(row.state)) << ","
        << "\"pitch_root_midi\":" << row.pitchRootMidi << ","
        << "\"pitch_confidence\":" << row.pitchConfidence << ","
        << "\"policy_intensity\":" << row.policyIntensity << ","
        << "\"rms_delta\":" << row.rmsDelta << ","
        << "\"rule_pattern_index\":" << row.rulePatternIndex << ","
        << "\"rule_pattern_name\":" << quoted(row.rulePatternName) << ","
        << "\"active_pattern_index\":" << row.activePatternIndex << ","
        << "\"active_pattern_name\":" << quoted(row.activePatternName) << ","
        << "\"onnx_pattern_index\":";
    if (row.onnxAvailable)
        out << row.onnxPatternIndex;
    else
        out << "null";
    out << ",\"onnx_pattern_name\":" << patternNameOrNull(row.onnxPatternName) << ","
        << "\"onnx_available\":" << (row.onnxAvailable ? "true" : "false") << ","
        << "\"model_mode\":" << quoted(row.modelMode.empty() ? preparedModelMode : row.modelMode)
        << "}\n";
}
