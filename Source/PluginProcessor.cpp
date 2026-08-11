#include <cmath>

#include <EdgeVersion.h>

#include "PluginProcessor.h"
#include "PluginEditor.h"

#include "Core/Presets.h"
#include "Core/StateMigration.h"

EdgeAudioProcessor::EdgeAudioProcessor()
    : juce::AudioProcessor (BusesProperties()
          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "EDGE", edge::createParameterLayout())
{
    apvts.state.setProperty ("stateVersion", edge::kStateVersion, nullptr);
}

bool EdgeAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();

    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;

    return layouts.getMainInputChannelSet() == out;
}

//  What a support e-mail needs and never contains: which build, in which host,
//  at which rate and block size. Written once per prepareToPlay - the message
//  thread, never the audio thread - and only when it CHANGES, so a host that
//  re-prepares every few seconds cannot fill a disk.
void EdgeAudioProcessor::logEnvironment (double sampleRate, int samplesPerBlock)
{
    const auto line = juce::String ("EDGE ") + edge::kVersion
                        + " (" + edge::kGitDescribe + ", built " + edge::kBuildDate + ")"
                        + "  host: " + juce::PluginHostType().getHostDescription()
                        + "  " + juce::String (sampleRate, 0) + " Hz"
                        + "  block " + juce::String (samplesPerBlock)
                        + "  " + juce::String (getTotalNumInputChannels()) + " in / "
                        + juce::String (getTotalNumOutputChannels()) + " out";

    if (line == lastLoggedEnvironment)
        return;

    lastLoggedEnvironment = line;

    //  One file, appended, next to the other plug-in logs. Nothing is sent
    //  anywhere: it exists so the user can paste it, not so anyone can collect
    //  it.
    const auto file = juce::FileLogger::getSystemLogFileFolder()
                          .getChildFile ("EDGE").getChildFile ("EDGE.log");

    if (logger == nullptr)
        logger = std::make_unique<juce::FileLogger> (file, "EDGE session log", 256 * 1024);

    logger->logMessage (juce::Time::getCurrentTime().toISO8601 (true) + "  " + line);
}

void EdgeAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    const int chans = juce::jmax (1, getTotalNumOutputChannels());

    engine.prepare (sampleRate, juce::jmax (1, samplesPerBlock), chans);
    setLatencySamples ((int) std::lround (engine.getLatencySamples()));

    engine.snapToSettings (edge::readSettings (apvts));

    logEnvironment (sampleRate, samplesPerBlock);
}

void EdgeAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    for (int c = getTotalNumInputChannels(); c < getTotalNumOutputChannels(); ++c)
        buffer.clear (c, 0, buffer.getNumSamples());

    engine.setSettings (edge::readSettings (apvts));
    engine.process (buffer);
}

int EdgeAudioProcessor::getNumPrograms()
{
    return edge::kNumPresets;
}

const juce::String EdgeAudioProcessor::getProgramName (int index)
{
    if (juce::isPositiveAndBelow (index, edge::kNumPresets))
        return edge::kPresets[index].name;

    return {};
}

void EdgeAudioProcessor::setCurrentProgram (int index)
{
    if (! juce::isPositiveAndBelow (index, edge::kNumPresets))
        return;

    currentProgram = index;

    //  Written through the parameters, so the host's automation lanes, the
    //  editor and the saved state all see the same change. NOT snapped: the
    //  smoothers glide, which is what stops auditioning presets from clicking.
    edge::applyPreset (apvts, index);
}

juce::AudioProcessorEditor* EdgeAudioProcessor::createEditor()
{
    return new EdgeAudioProcessorEditor (*this);
}

void EdgeAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto tree = apvts.copyState();
    tree.setProperty ("stateVersion", edge::kStateVersion, nullptr);
    tree.setProperty ("editorWidth",  editorWidth.load(),  nullptr);
    tree.setProperty ("editorHeight", editorHeight.load(), nullptr);
    tree.setProperty ("shapeOpen",    shapeOpen.load(),    nullptr);
    tree.setProperty ("program",      currentProgram,      nullptr);

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

    //  A state written by a build that does not exist yet. replaceState would
    //  hand APVTS a tree with parameters it does not recognise and none of the
    //  ones it does, which resets a mixed session to defaults - the loudest
    //  possible way to lose someone's work. Take across whatever is recognised
    //  and leave everything else exactly as it is.
    if ((int) tree.getProperty ("stateVersion", 1) > edge::kStateVersion)
    {
        for (int i = 0; i < tree.getNumChildren(); ++i)
        {
            const auto child = tree.getChild (i);
            if (! child.hasProperty ("id") || ! child.hasProperty ("value"))
                continue;

            if (auto* p = apvts.getParameter (child.getProperty ("id").toString()))
                p->setValueNotifyingHost (
                    p->convertTo0to1 ((float) child.getProperty ("value")));
        }

        loadedLegacyState.store (false);
        return;
    }

    //  A project saved with v0.1 has the old parameter IDs in it. Loading it
    //  unchanged would reset a mixed session to defaults, so it is rewritten
    //  into a v2 tree first - see Core/StateMigration.h for what maps to what
    //  and what is deliberately dropped.
    const bool migrated = edge::migrateToCurrent (tree);

    editorWidth.store  ((int) tree.getProperty ("editorWidth",  edge::ui::metric::defaultWidth));
    editorHeight.store ((int) tree.getProperty ("editorHeight", edge::ui::metric::defaultHeight));
    shapeOpen.store    ((bool) tree.getProperty ("shapeOpen", false));
    currentProgram = juce::jlimit (0, edge::kNumPresets - 1,
                                   (int) tree.getProperty ("program", 0));
    loadedLegacyState.store (migrated);

    apvts.replaceState (tree);

    //  Deliberately NOT snapped. A recall that lands instantly steps every
    //  shelf gain, and a step in G is a step in the output - a click. The
    //  20 ms smoothers glide instead, which is fast enough to feel like a
    //  recall. A project LOAD snaps, because prepareToPlay runs first and
    //  there is no audio to click.
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new EdgeAudioProcessor();
}
