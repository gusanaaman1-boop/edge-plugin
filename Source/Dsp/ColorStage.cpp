#include <vector>

#include "ColorStage.h"

namespace edge
{
    void ColorStage::prepare (double sampleRate, int maxBlockSize, int numChannels)
    {
        baseRate = sampleRate;
        channels = juce::jlimit (1, 2, numChannels);

        for (auto& e : engines)
            e = fourcolor::createColorEngine (fourcolor::ColorType::warm);

        if (osFactor == 2)
        {
            //  Polyphase IIR half-band: a few samples of latency instead of the
            //  ~65 an equiripple FIR costs, at the price of non-linear phase in
            //  the top octave. EDGE is minimum-phase anyway, so that costs it
            //  nothing it was promising.
            oversampler = std::make_unique<juce::dsp::Oversampling<float>> (
                (size_t) channels, 1,
                juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR,
                true /* isMaxQuality */, false /* useIntegerLatency */);
            oversampler->initProcessing ((size_t) maxBlockSize);
            latency = (float) oversampler->getLatencyInSamples();
        }
        else
        {
            oversampler.reset();
            latency = 0.0f;
        }

        for (auto& e : engines)
            e->prepare (baseRate * (double) osFactor, 1);

        buildTrimTable();
        setDrive (drivePercent);
        reset();
    }

    //  Measure, do not assume. A separate probe engine is driven with a quiet
    //  sine at each of kTrimPoints drive settings and its RMS gain recorded.
    //  Nothing here touches the live engine, and nothing here runs on the audio
    //  thread.
    void ColorStage::buildTrimTable()
    {
        const double probeRate = baseRate * (double) osFactor;
        auto probe = fourcolor::createColorEngine (fourcolor::ColorType::warm);
        probe->prepare (probeRate, 1);

        constexpr int settle = 4096;      // covers WARM's ~120 ms sag envelope
        constexpr int measure = 4096;
        //  -18 dBFS, the working level the work order names. Calibrating at a
        //  small-signal level instead would make the plug-in about 1 dB LOUDER
        //  on normal material every time Depth was engaged; calibrating here
        //  puts the error where it belongs, on material 40 dB quieter than
        //  anything anyone mixes at.
        constexpr float amplitude = 0.125f;

        std::vector<float> buf ((size_t) (settle + measure));

        for (int p = 0; p < kTrimPoints; ++p)
        {
            const float drive = 100.0f * (float) p / (float) (kTrimPoints - 1);
            probe->setDrive (drive);
            probe->reset();

            const double w = juce::MathConstants<double>::twoPi * 1000.0 / probeRate;
            for (size_t i = 0; i < buf.size(); ++i)
                buf[i] = amplitude * (float) std::sin (w * (double) i);

            probe->processBlock (buf.data(), (int) buf.size(), 0);

            double sumSq = 0.0;
            for (int i = settle; i < settle + measure; ++i)
                sumSq += (double) buf[(size_t) i] * buf[(size_t) i];

            const double outRms = std::sqrt (sumSq / measure);
            const double inRms  = (double) amplitude * juce::MathConstants<double>::sqrt2 * 0.5;

            const double gain = inRms > 0.0 ? outRms / inRms : 1.0;

            //  Drive 0 must come out as exactly 1.0 so the neutral path stays
            //  bit-exact; the engine guarantees a bit-exact pass-through there,
            //  so anything else would be a measurement artefact.
            trimTable[(size_t) p] = (p == 0 || ! std::isfinite (gain) || gain <= 1.0e-4)
                                        ? 1.0f
                                        : (float) gain;
        }
    }

    void ColorStage::reset() noexcept
    {
        if (oversampler != nullptr)
            oversampler->reset();

        for (auto& e : engines)
            if (e != nullptr)
                e->reset();
    }

    void ColorStage::setDrive (float newDrivePercent) noexcept
    {
        drivePercent = juce::jlimit (0.0f, 100.0f, newDrivePercent);

        for (auto& e : engines)
            if (e != nullptr)
                e->setDrive (drivePercent);

        //  Linear interpolation into the measured table.
        const float pos = drivePercent * 0.01f * (float) (kTrimPoints - 1);
        const int i0 = juce::jlimit (0, kTrimPoints - 1, (int) pos);
        const int i1 = juce::jlimit (0, kTrimPoints - 1, i0 + 1);
        const float frac = pos - (float) i0;

        const float gain = trimTable[(size_t) i0]
                         + frac * (trimTable[(size_t) i1] - trimTable[(size_t) i0]);

        measuredGain = gain;
        levelTrim = gain > 1.0e-4f ? 1.0f / gain : 1.0f;
    }

    void ColorStage::setSpectrum (float lowHz, float highHz) noexcept
    {
        if (engines[0] == nullptr)
            return;

        fourcolor::ColorContext ctx;
        ctx.bandIndex       = 0;
        ctx.oversampledRate = baseRate * (double) osFactor;
        ctx.bandLowHz       = lowHz;
        ctx.bandHighHz      = highHz;
        ctx.centreHz        = std::sqrt (juce::jmax (1.0f, lowHz) * juce::jmax (1.0f, highHz));

        for (auto& e : engines)
            e->setContext (ctx);
    }

    void ColorStage::process (juce::AudioBuffer<float>& buffer, int n) noexcept
    {
        if (engines[0] == nullptr || n == 0)
            return;

        const int chans = juce::jmin (channels, buffer.getNumChannels());

        if (oversampler == nullptr)
        {
            //  Channel 0 of each per-channel engine: see ColorStage.h.
            for (int c = 0; c < chans; ++c)
                engines[c]->processBlock (buffer.getWritePointer (c), n, 0);

            return;
        }

        juce::dsp::AudioBlock<float> baseBlock (buffer.getArrayOfWritePointers(),
                                                (size_t) chans, (size_t) n);
        auto osBlock = oversampler->processSamplesUp (baseBlock);
        const int osSamples = (int) osBlock.getNumSamples();

        for (int c = 0; c < chans; ++c)
            engines[c]->processBlock (osBlock.getChannelPointer ((size_t) c), osSamples, 0);

        oversampler->processSamplesDown (baseBlock);
    }
}
