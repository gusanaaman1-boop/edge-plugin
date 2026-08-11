#include "PluginProcessor.h"
#include "PluginEditor.h"

EdgeAudioProcessor::EdgeAudioProcessor()
    : juce::AudioProcessor (BusesProperties()
          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "EDGE", edge::createParameterLayout())
{
}

bool EdgeAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();

    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;

    return layouts.getMainInputChannelSet() == out;
}

void EdgeAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    const int chans = juce::jmax (1, getTotalNumOutputChannels());

    engine.prepare (sampleRate, juce::jmax (1, samplesPerBlock), chans);
    setLatencySamples ((int) std::lround (engine.getLatencySamples()));

    engine.snapToSettings (edge::readSettings (apvts));
}

void EdgeAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    for (int c = getTotalNumInputChannels(); c < getTotalNumOutputChannels(); ++c)
        buffer.clear (c, 0, buffer.getNumSamples());

    engine.setSettings (edge::readSettings (apvts));
    engine.process (buffer);
}

juce::AudioProcessorEditor* EdgeAudioProcessor::createEditor()
{
    return new EdgeAudioProcessorEditor (*this);
}

void EdgeAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto tree = apvts.copyState();
    tree.setProperty ("editorWidth",  editorWidth.load(),  nullptr);
    tree.setProperty ("editorHeight", editorHeight.load(), nullptr);

    if (auto xml = tree.createXml())
        copyXmlToBinary (*xml, destData);
}

void EdgeAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType()))
        return;

    auto tree = juce::ValueTree::fromXml (*xml);
    if (! tree.isValid())
        return;

    editorWidth.store  ((int) tree.getProperty ("editorWidth",  760));
    editorHeight.store ((int) tree.getProperty ("editorHeight", 460));

    apvts.replaceState (tree);

    //  Deliberately NOT snapped. A recall that lands instantly steps every
    //  shelf gain, and a step in G is a step in the output - a click. The
    //  20 ms smoothers glide instead, which is fast enough to feel like a
    //  recall and slow enough that test 15 (preset change during playback)
    //  passes. A project LOAD snaps, because prepareToPlay runs first and
    //  there is no audio to click.
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new EdgeAudioProcessor();
}
