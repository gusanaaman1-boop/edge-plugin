#include <vector>

#include "ColorStage.h"

namespace edge
{
    namespace
    {
        fourcolor::ColorType engineTypeFor (int character) noexcept
        {
            return character == (int) Character::iron ? fourcolor::ColorType::iron
                                                      : fourcolor::ColorType::warm;
        }
    }

    void ColorStage::prepare (double sampleRate, int maxBlockSize, int numChannels)
    {
        baseRate = sampleRate;
        channels = juce::jlimit (1, maxChannels, numChannels);

        //  BOTH characters are built and prepared here. Switching between them
        //  on the audio thread must not allocate, and an engine that has been
        //  running all along has warm state to cross into.
        for (int m = 0; m < numCharacters; ++m)
            for (int c = 0; c < maxChannels; ++c)
                engines[m][c] = fourcolor::createColorEngine (engineTypeFor (m));

        if (osFactor == 2)
        {
            //  Polyphase IIR half-band: a few samples of latency instead of the
            //  ~65 an equiripple FIR costs, at the price of non-linear phase in
            //  the top octave. EDGE is minimum-phase anyway.
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

        for (int m = 0; m < numCharacters; ++m)
            for (int c = 0; c < maxChannels; ++c)
                engines[m][c]->prepare (baseRate * (double) osFactor, 1);

        for (int m = 0; m < numCharacters; ++m)
            buildTrimTable (m);

        //  20 ms of equal-gain crossfade, at the OVERSAMPLED rate because that
        //  is where the fade is applied.
        fadeLength = juce::jmax (1, (int) (baseRate * (double) osFactor * 0.020));
        fadeScratch.setSize (maxChannels, juce::jmax (16, maxBlockSize * osFactor));

        setDrive (drivePercent);
        reset();
    }

    //  Measure, do not assume. A separate probe engine is driven with a sine at
    //  the working level at each of kTrimPoints drive settings and its RMS gain
    //  recorded. Nothing here touches the live engines, and nothing here runs on
    //  the audio thread.
    void ColorStage::buildTrimTable (int character)
    {
        const double probeRate = baseRate * (double) osFactor;
        auto probe = fourcolor::createColorEngine (engineTypeFor (character));
        probe->prepare (probeRate, 1);

        constexpr int settle = 4096;      // covers WARM's ~120 ms sag envelope
        constexpr int measure = 4096;

        //  -18 dBFS, the working level the work order names. Calibrating at a
        //  small-signal level instead would make the plug-in about 1 dB LOUDER
        //  on normal material every time the colour engaged.
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
            trimTable[(size_t) character][(size_t) p] =
                (p == 0 || ! std::isfinite (gain) || gain <= 1.0e-4) ? 1.0f : (float) gain;
        }
    }

    void ColorStage::reset() noexcept
    {
        if (oversampler != nullptr)
            oversampler->reset();

        for (int m = 0; m < numCharacters; ++m)
            for (int c = 0; c < maxChannels; ++c)
                if (engines[m][c] != nullptr)
                    engines[m][c]->reset();

        fadeLeft = 0;
    }

    void ColorStage::setDrive (float newDrivePercent) noexcept
    {
        drivePercent = juce::jlimit (0.0f, 100.0f, newDrivePercent);

        //  Every engine gets the drive, including the ones not currently
        //  audible: an engine that has been tracking is one that can be
        //  crossfaded into without a jump.
        for (int m = 0; m < numCharacters; ++m)
            for (int c = 0; c < maxChannels; ++c)
                if (engines[m][c] != nullptr)
                    engines[m][c]->setDrive (drivePercent);

        //  Linear interpolation into the ACTIVE character's measured table.
        const float pos = drivePercent * 0.01f * (float) (kTrimPoints - 1);
        const int i0 = juce::jlimit (0, kTrimPoints - 1, (int) pos);
        const int i1 = juce::jlimit (0, kTrimPoints - 1, i0 + 1);
        const float frac = pos - (float) i0;

        const auto& table = trimTable[(size_t) activeCharacter];
        const float gain = table[(size_t) i0]
                         + frac * (table[(size_t) i1] - table[(size_t) i0]);

        measuredGain = gain;
        levelTrim = gain > 1.0e-4f ? 1.0f / gain : 1.0f;
    }

    void ColorStage::setCharacter (int character) noexcept
    {
        const int wanted = juce::jlimit (0, numCharacters - 1, character);

        if (wanted == activeCharacter)
            return;

        //  If a fade is already running, the thing we are leaving is whatever
        //  is currently audible - not the character we started from.
        fadingFrom = fadeLeft > 0 ? fadingFrom : activeCharacter;
        activeCharacter = wanted;
        fadeLeft = fadeLength;

        //  The trim belongs to the character now in charge.
        setDrive (drivePercent);
    }

    void ColorStage::setSpectrum (float lowHz, float highHz) noexcept
    {
        if (engines[0][0] == nullptr)
            return;

        fourcolor::ColorContext ctx;
        ctx.bandIndex       = 0;
        ctx.oversampledRate = baseRate * (double) osFactor;
        ctx.bandLowHz       = lowHz;
        ctx.bandHighHz      = highHz;
        ctx.centreHz        = std::sqrt (juce::jmax (1.0f, lowHz) * juce::jmax (1.0f, highHz));

        for (int m = 0; m < numCharacters; ++m)
            for (int c = 0; c < maxChannels; ++c)
                engines[m][c]->setContext (ctx);
    }

    float ColorStage::getEngageFactor() const noexcept
    {
        return engines[(size_t) activeCharacter][0] != nullptr
                 ? engines[(size_t) activeCharacter][0]->getEngageTarget() : 0.0f;
    }

    void ColorStage::runEngines (int character, float* const* data, int numChannels, int n) noexcept
    {
        //  Channel 0 of each per-channel engine: see the note in the header.
        for (int c = 0; c < numChannels; ++c)
            engines[(size_t) character][(size_t) c]->processBlock (data[c], n, 0);
    }

    void ColorStage::process (juce::AudioBuffer<float>& buffer, int n) noexcept
    {
        if (engines[0][0] == nullptr || n == 0)
            return;

        const int chans = juce::jmin (channels, buffer.getNumChannels());

        auto processAt = [&] (float* const* data, int samples)
        {
            if (fadeLeft <= 0)
            {
                runEngines (activeCharacter, data, chans, samples);
                return;
            }

            //  Both characters run; the outgoing one on a copy. Equal gain,
            //  because the two are highly correlated at these drives - an
            //  equal-power fade would bulge in the middle.
            for (int c = 0; c < chans; ++c)
                fadeScratch.copyFrom (c, 0, data[c], samples);

            float* outgoing[maxChannels] = { fadeScratch.getWritePointer (0),
                                             chans > 1 ? fadeScratch.getWritePointer (1) : nullptr };

            runEngines (activeCharacter, data, chans, samples);
            runEngines (fadingFrom, outgoing, chans, samples);

            const float inv = 1.0f / (float) fadeLength;
            int left = fadeLeft;

            for (int c = 0; c < chans; ++c)
            {
                left = fadeLeft;
                auto* d = data[c];
                const auto* o = outgoing[c];

                for (int i = 0; i < samples; ++i)
                {
                    const float w = left > 0 ? (float) left * inv : 0.0f;
                    d[i] += w * (o[i] - d[i]);
                    if (left > 0) --left;
                }
            }

            fadeLeft = left;
        };

        if (oversampler == nullptr)
        {
            float* ptrs[maxChannels] = { buffer.getWritePointer (0),
                                         chans > 1 ? buffer.getWritePointer (1) : nullptr };
            processAt (ptrs, n);
            return;
        }

        juce::dsp::AudioBlock<float> baseBlock (buffer.getArrayOfWritePointers(),
                                                (size_t) chans, (size_t) n);
        auto osBlock = oversampler->processSamplesUp (baseBlock);
        const int osSamples = (int) osBlock.getNumSamples();

        float* osPtrs[maxChannels] = { osBlock.getChannelPointer (0),
                                       chans > 1 ? osBlock.getChannelPointer (1) : nullptr };
        processAt (osPtrs, osSamples);

        oversampler->processSamplesDown (baseBlock);
    }
}
