#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "Core/Parameters.h"
#include "Dsp/EdgeEngine.h"

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

    //  Editor-side sizing, kept in the plug-in state so a resized window comes
    //  back the size it was left.
    std::atomic<int> editorWidth { 760 };
    std::atomic<int> editorHeight { 460 };

private:
    juce::AudioProcessorValueTreeState apvts;
    edge::EdgeEngine engine;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EdgeAudioProcessor)
};
