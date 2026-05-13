#pragma once

/**
 * @file
 * @brief Non-real-time feature capture writer for Phase 34 debug sessions.
 */

#include "analysis/FeatureVector.h"
#include "readerwriterqueue.h"
#include <juce_core/juce_core.h>
#include <atomic>
#include <cstdint>
#include <fstream>
#include <string>
#include <thread>

struct FeatureCaptureRow
{
    float bpm = 120.0f;
    float rmsEnergy = 0.0f;
    float spectralCentroid = 0.0f;
    float highFreqFlux = 0.0f;
    StructureState state = StructureState::SILENT;
    int64_t sampleTimestamp = 0;
    float pitchRootMidi = 40.0f;
    float pitchConfidence = 0.0f;
    float policyIntensity = 0.5f;
    float rmsDelta = 0.0f;

    double elapsedSeconds = 0.0;
    int rulePatternIndex = 0;
    int activePatternIndex = 0;
    int onnxPatternIndex = -1;
    bool onnxAvailable = false;
    std::string rulePatternName;
    std::string activePatternName;
    std::string onnxPatternName;
    std::string modelMode;
};

class FeatureCapture final
{
public:
    static constexpr int kMaxRows = 30000;
    static constexpr double kMaxCaptureSeconds = 600.0;

    FeatureCapture();
    ~FeatureCapture();

    void prepare(double sampleRate, int blockSize, std::string modelMode);
    bool start();
    void stop();

    bool isCapturing() const noexcept { return capturing.load(std::memory_order_acquire); }
    std::string getCapturePath() const;
    uint64_t getDroppedRows() const noexcept { return droppedRows.load(std::memory_order_relaxed); }

    bool tryPush(const FeatureCaptureRow& row);

private:
    void writerLoop();
    void writeHeader(std::ofstream& out) const;
    void writeRow(std::ofstream& out, const FeatureCaptureRow& row) const;

    double sampleRate = 44100.0;
    int blockSize = 0;
    std::string preparedModelMode = "None";
    juce::File captureFile;

    moodycamel::ReaderWriterQueue<FeatureCaptureRow> queue{ 4096 };
    std::atomic<bool> capturing{ false };
    std::atomic<bool> stopRequested{ false };
    std::atomic<uint64_t> droppedRows{ 0 };
    std::atomic<int> rowsWritten{ 0 };
    std::thread writerThread;
};
