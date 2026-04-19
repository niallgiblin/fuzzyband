#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <vector>
#include "analysis/EnergyAnalyser.h"

namespace {

std::vector<float> makeSine(int numSamples, double freq, double sampleRate, double amplitude = 1.0)
{
    std::vector<float> buf(static_cast<size_t>(numSamples));
    for (int i = 0; i < numSamples; ++i)
        buf[static_cast<size_t>(i)] = static_cast<float>(
            amplitude * std::sin(2.0 * M_PI * freq * i / sampleRate));
    return buf;
}

std::vector<float> makeSilence(int numSamples)
{
    return std::vector<float>(static_cast<size_t>(numSamples), 0.0f);
}

} // namespace

// ─── RMS ─────────────────────────────────────────────────────────────────────

TEST_CASE("EnergyAnalyser: RMS is near zero for silence", "[energy]")
{
    EnergyAnalyser analyser;
    analyser.prepare(44100.0, 512);

    // Feed 5000 samples (> 0.1 s RMS window at 44100 Hz) of silence
    auto silent = makeSilence(5000);
    analyser.process(silent.data(), 5000);

    REQUIRE(analyser.getRmsEnergy() < 0.01f);
}

TEST_CASE("EnergyAnalyser: RMS rises for non-trivial amplitude signal", "[energy]")
{
    EnergyAnalyser analyser;
    analyser.prepare(44100.0, 512);

    // Amplitude 0.25: RMS ≈ 0.177, * 4.0 ≈ 0.707 → well above silent threshold
    // Feed 5120 samples to fill the 0.1 s rolling RMS window (4410 samples)
    const int n = 5120;
    auto sig = makeSine(n, 440.0, 44100.0, 0.25);
    analyser.process(sig.data(), n);

    REQUIRE(analyser.getRmsEnergy() > 0.3f);
}

TEST_CASE("EnergyAnalyser: RMS is clamped at 1.0 for full-scale signal", "[energy]")
{
    EnergyAnalyser analyser;
    analyser.prepare(44100.0, 512);

    // Amplitude 1.0: RMS ≈ 0.707, * 4.0 = 2.83, clamped to 1.0
    const int n = 5120;
    auto sig = makeSine(n, 440.0, 44100.0, 1.0);
    analyser.process(sig.data(), n);

    REQUIRE(analyser.getRmsEnergy() <= 1.0f);
    REQUIRE(analyser.getRmsEnergy() > 0.9f);
}

// ─── Spectral Centroid ────────────────────────────────────────────────────────

TEST_CASE("EnergyAnalyser: spectral centroid is low for a low-frequency tone", "[energy]")
{
    EnergyAnalyser analyser;
    analyser.prepare(44100.0, 512);

    // 500 Hz sine — centroid should cluster well below 1500 Hz
    // Feed 4096 samples (multiple FFT hops: hopSize=256, fftSize=1024)
    const int n = 4096;
    auto sig = makeSine(n, 500.0, 44100.0, 0.5);
    analyser.process(sig.data(), n);

    REQUIRE(analyser.getSpectralCentroid() < 1500.0f);
    REQUIRE(analyser.getSpectralCentroid() > 0.0f);
}

TEST_CASE("EnergyAnalyser: spectral centroid is high for a high-frequency tone", "[energy]")
{
    EnergyAnalyser analyser;
    analyser.prepare(44100.0, 512);

    // 4000 Hz sine — centroid should be clearly above 2000 Hz
    const int n = 4096;
    auto sig = makeSine(n, 4000.0, 44100.0, 0.5);
    analyser.process(sig.data(), n);

    REQUIRE(analyser.getSpectralCentroid() > 2000.0f);
}

TEST_CASE("EnergyAnalyser: centroid ordering — low freq < high freq", "[energy]")
{
    EnergyAnalyser low, high;
    low.prepare(44100.0, 512);
    high.prepare(44100.0, 512);

    const int n = 4096;
    auto sigLow  = makeSine(n, 500.0,  44100.0, 0.5);
    auto sigHigh = makeSine(n, 4000.0, 44100.0, 0.5);
    low.process(sigLow.data(), n);
    high.process(sigHigh.data(), n);

    REQUIRE(low.getSpectralCentroid() < high.getSpectralCentroid());
}

// ─── High-Frequency Flux ─────────────────────────────────────────────────────

TEST_CASE("EnergyAnalyser: HF flux is low for a sustained low-frequency tone", "[energy]")
{
    EnergyAnalyser analyser;
    analyser.prepare(44100.0, 512);

    // 440 Hz — energy sits well below the 2000 Hz HF boundary (bin ~46).
    // After several hops the previous-magnitude buffer stabilises; flux ≈ 0.
    const int n = 8192;
    auto sig = makeSine(n, 440.0, 44100.0, 0.5);
    analyser.process(sig.data(), n);

    REQUIRE(analyser.getHighFreqFlux() < 1.0f);
}

TEST_CASE("EnergyAnalyser: HF flux is positive after a broadband impulse", "[energy]")
{
    EnergyAnalyser analyser;
    analyser.prepare(44100.0, 512);

    // Prime prevHighMagnitudes with silence so the baseline is zero
    auto silence = makeSilence(1024);
    analyser.process(silence.data(), 1024);

    // Broadband impulse excites all frequency bins including the HF band
    std::vector<float> impulse(512, 0.0f);
    impulse[0] = 1.0f;
    analyser.process(impulse.data(), 512);

    REQUIRE(analyser.getHighFreqFlux() > 0.0f);
}

// ─── Peak RMS Envelope ───────────────────────────────────────────────────────

TEST_CASE("EnergyAnalyser: peak RMS envelope tracks signal and does not drop instantly", "[energy]")
{
    EnergyAnalyser analyser;
    analyser.prepare(44100.0, 512);

    // Feed enough signal to raise the peak envelope
    const int sigN = 5120;
    auto sig = makeSine(sigN, 440.0, 44100.0, 0.25);
    analyser.process(sig.data(), sigN);
    const float peakAfterSignal = analyser.getPeakRms();
    REQUIRE(peakAfterSignal > 0.1f);

    // Feed 512 silence samples — peak should barely decay (kRelease ≈ 0.9995 per call)
    auto silent = makeSilence(512);
    analyser.process(silent.data(), 512);
    const float peakAfterSilence = analyser.getPeakRms();

    // Must still be clearly above floor after one silent call
    REQUIRE(peakAfterSilence > 0.05f);
    // Must not increase
    REQUIRE(peakAfterSilence <= peakAfterSignal + 0.01f);
}

TEST_CASE("EnergyAnalyser: peak RMS is never below kSilentRmsFloor (0.01)", "[energy]")
{
    EnergyAnalyser analyser;
    analyser.prepare(44100.0, 512);

    // Even after prolonged silence the floor is clamped to kSilentRmsFloor
    const int n = 44100; // 1 second of silence
    auto silent = makeSilence(n);
    analyser.process(silent.data(), n);

    REQUIRE(analyser.getPeakRms() >= 0.01f);
}
