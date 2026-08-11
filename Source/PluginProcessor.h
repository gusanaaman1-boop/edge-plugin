#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "Core/Parameters.h"
#include "Dsp/EdgeEngine.h"
#include "Ui/Theme.h"

class EdgeAudioProcessor : public juce::AudioProcessor
{
public:
    EdgeAudioProcessor();
    ~EdgeAudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    //  JucePlugin_Name only exists inside a plug-in wrapper build. The tools
    //  link the same processor without one.
   #if defined (JucePlugin_Name)
    const juce::String getName() const override { return JucePlugin_Name; }
   #else
    const juce::String getName() const override { return "EDGE"; }
   #endif
    bool acceptsMidi() const override  { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return "Default"; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    juce::AudioProcessorValueTreeState& getState() noexcept { return apvts; }
    edge::EdgeEngine& getEngine() noexcept { return engine; }

    //  Editor-side state, kept in the plug-in state so a resized window and an
    //  open SHAPE panel come back the way they were left.
    std::atomic<int> editorWidth  { edge::ui::metric::defaultWidth };
    std::atomic<int> editorHeight { edge::ui::metric::defaultHeight };
    std::atomic<bool> shapeOpen { false };

    //  Set when the last loaded state had to be migrated from v0.1. The editor
    //  shows a one-line notice, because a session silently changing colour law
    //  is worse than a session that says it did.
    std::atomic<bool> loadedLegacyState { false };

private:
    juce::AudioProcessorValueTreeState apvts;
    edge::EdgeEngine engine;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EdgeAudioProcessor)
};
