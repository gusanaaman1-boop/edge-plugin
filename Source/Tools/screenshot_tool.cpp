// Deterministic UI renderer.
//
//     build/EdgeShot_artefacts/<config>/EdgeShot ui-shots
//
// Builds the real editor over the real processor, sets parameters through the
// APVTS exactly as a host would, and renders the component to a PNG. No screen
// capture, no window server, no timing luck.

#include <cstdio>
#include <memory>
#include <utility>
#include <vector>

#include <juce_gui_extra/juce_gui_extra.h>

#include "../Core/ParameterIds.h"
#include "../Core/Presets.h"
#include "../PluginEditor.h"
#include "../PluginProcessor.h"

namespace
{
    struct Shot
    {
        const char* name;
        int inspector;      // -1 none, else (int) edge::ui::SelectedControl
        int width, height;
        std::vector<std::pair<const char*, float>> values;
        int audio = 1;      // 0 silence, 1 pink, 2 techno kick pattern
    };

    void setParam (EdgeAudioProcessor& p, const char* id, float value)
    {
        if (auto* param = p.getState().getParameter (id))
            param->setValueNotifyingHost (param->convertTo0to1 (value));
    }

    int render (const juce::File& outputDir)
    {
        const int W = edge::ui::metric::defaultWidth, H = edge::ui::metric::defaultHeight;

        //  The acceptance list from the v0.12 work order: LP and HP at EDGE 0,
        //  50 and 100; every inspector context; minimum, reference and maximum
        //  window sizes.
        const std::vector<Shot> shots =
        {
            { "lp-edge-000", -1, W, H, {
                { edge::param::mode, (float) (int) edge::Mode::lowPass },
                { edge::param::highFreq, 3200.0f }, { edge::param::edge, 0.0f } } },
            { "lp-edge-050", -1, W, H, {
                { edge::param::mode, (float) (int) edge::Mode::lowPass },
                { edge::param::highFreq, 3200.0f }, { edge::param::edge, 50.0f } } },
            { "lp-edge-100", -1, W, H, {
                { edge::param::mode, (float) (int) edge::Mode::lowPass },
                { edge::param::highFreq, 3200.0f }, { edge::param::edge, 100.0f } } },

            { "hp-edge-000", -1, W, H, {
                { edge::param::mode, (float) (int) edge::Mode::highPass },
                { edge::param::lowFreq, 300.0f }, { edge::param::edge, 0.0f } } },
            { "hp-edge-050", -1, W, H, {
                { edge::param::mode, (float) (int) edge::Mode::highPass },
                { edge::param::lowFreq, 300.0f }, { edge::param::edge, 50.0f } } },
            { "hp-edge-100", -1, W, H, {
                { edge::param::mode, (float) (int) edge::Mode::highPass },
                { edge::param::lowFreq, 300.0f }, { edge::param::edge, 100.0f } } },

            { "inspector-low", (int) edge::ui::SelectedControl::low, W, H, {
                { edge::param::mode, (float) (int) edge::Mode::band },
                { edge::param::edge, 62.0f } } },
            { "inspector-lp-high", (int) edge::ui::SelectedControl::high, W, H, {
                { edge::param::mode, (float) (int) edge::Mode::lowPass },
                { edge::param::highFreq, 3200.0f }, { edge::param::edge, 56.0f } } },
            { "inspector-high", (int) edge::ui::SelectedControl::high, W, H, {
                { edge::param::mode, (float) (int) edge::Mode::band },
                { edge::param::edge, 62.0f } } },
            { "inspector-mid", (int) edge::ui::SelectedControl::mid, W, H, {
                { edge::param::mode, (float) (int) edge::Mode::band },
                { edge::param::midGain, 8.0f }, { edge::param::edge, 62.0f } } },
            { "inspector-follow", (int) edge::ui::SelectedControl::follow, W, H, {
                { edge::param::follow, 64.0f }, { edge::param::edge, 56.0f } } },

            { "size-min", -1, edge::ui::metric::minWidth, edge::ui::metric::minHeight, {
                { edge::param::mode, (float) (int) edge::Mode::band },
                { edge::param::edge, 62.0f } } },
            { "size-ref", -1, W, H, {
                { edge::param::mode, (float) (int) edge::Mode::band },
                { edge::param::edge, 62.0f } } },
            { "size-max", -1, edge::ui::metric::maxWidth, edge::ui::metric::maxHeight, {
                { edge::param::mode, (float) (int) edge::Mode::band },
                { edge::param::edge, 62.0f } } },

            { "spread-neg", -1, W, H, {
                { edge::param::mode, (float) (int) edge::Mode::band },
                { edge::param::edge, 62.0f }, { edge::param::spread, -100.0f } } },
            { "spread-zero", -1, W, H, {
                { edge::param::mode, (float) (int) edge::Mode::band },
                { edge::param::edge, 62.0f }, { edge::param::spread, 0.0f } } },
            { "spread-pos", -1, W, H, {
                { edge::param::mode, (float) (int) edge::Mode::band },
                { edge::param::edge, 62.0f }, { edge::param::spread, 100.0f } } },
            { "state-warm", -1, W, H, {
                { edge::param::bite, 70.0f },
                { edge::param::character, (float) (int) edge::Character::warm } } },
            { "state-iron", -1, W, H, {
                { edge::param::bite, 70.0f },
                { edge::param::character, (float) (int) edge::Character::iron } } },
            { "state-bypass", -1, W, H, {
                { edge::param::edge, 62.0f }, { edge::param::bypass, 1.0f } } },

            //  The two analyzer proofs: silence must show NOTHING (the muted
            //  host is expected to look like this), -18 dBFS noise must show
            //  the input spectrum through a closed filter.
            //  The product-page frame the v0.14 sign-off asks for: LP, EDGE
            //  55 %, FOLLOW active, target 1.2 kHz, a techno pattern playing -
            //  rail, target diamond, live puck, trail and energy ring in one
            //  frame, inspector closed.
            { "hero-lp-follow", -1, W, H, {
                { edge::param::mode, (float) (int) edge::Mode::lowPass },
                { edge::param::highFreq, 1200.0f },
                { edge::param::edge, 55.0f },
                { edge::param::follow, 65.0f },
                { edge::param::followAttack, 5.0f },
                { edge::param::followRelease, 220.0f },
                { edge::param::bite, 40.0f } }, 2 },

            { "analyzer-muted", -1, W, H, {
                { edge::param::mode, (float) (int) edge::Mode::lowPass },
                { edge::param::highFreq, 1200.0f }, { edge::param::edge, 70.0f } }, 0 },
            { "analyzer-pink", -1, W, H, {
                { edge::param::mode, (float) (int) edge::Mode::lowPass },
                { edge::param::highFreq, 1200.0f }, { edge::param::edge, 70.0f } }, 1 },
        };

        outputDir.createDirectory();

        for (const auto& shot : shots)
        {
            EdgeAudioProcessor processor;
            processor.prepareToPlay (48000.0, 512);

            for (const auto& v : shot.values)
                setParam (processor, v.first, v.second);

            //  The editor has to exist BEFORE the audio runs: the analyser FIFO
            //  is only written while an editor is open, which is the whole point
            //  of that switch, so processing first left every shot with an empty
            //  spectrum.
            std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditor());
            if (editor == nullptr)
            {
                std::fprintf (stderr, "no editor\n");
                return 1;
            }

            //  A few blocks of pink-ish noise, so the analyser trace and the
            //  follower's live position are real rather than empty.
            juce::Random rng (1234);
            juce::AudioBuffer<float> buf (2, 512);
            juce::MidiBuffer midi;
            float lp = 0.0f;

            //  Audio for the frame. Mode 2 is a little techno engine: a
            //  four-on-the-floor kick (pitched sine drop) with an off-beat
            //  hat, so FOLLOW genuinely pumps and the trail has real
            //  positions to record.
            double phase = 0.0;
            int samplePos = 0;
            const int kickPeriod = 48000 * 60 / 126;    // 126 BPM

            const int blocks = shot.audio == 2 ? 220 : 40;
            for (int block = 0; block < blocks; ++block)
            {
                buf.clear();

                if (shot.audio == 1)
                {
                    for (int i = 0; i < 512; ++i)
                    {
                        const float white = rng.nextFloat() * 2.0f - 1.0f;
                        lp += 0.06f * (white - lp);
                        const float v = 0.35f * (lp * 2.2f + white * 0.35f)
                                      * 0.5f;                       // ~-18 dBFS
                        buf.setSample (0, i, v);
                        buf.setSample (1, i, v);
                    }
                }
                else if (shot.audio == 2)
                {
                    for (int i = 0; i < 512; ++i)
                    {
                        const int t = (samplePos + i) % kickPeriod;
                        const float ts = (float) t / 48000.0f;

                        //  Kick: 150 -> 45 Hz drop with a 90 ms decay.
                        const double hz = 45.0 + 105.0 * std::exp (-ts * 28.0f);
                        phase += juce::MathConstants<double>::twoPi * hz / 48000.0;
                        const float kick = 0.9f * std::exp (-ts * 9.0f)
                                              * (float) std::sin (phase);

                        //  Hat: a noise tick on the off-beat.
                        const int off = (samplePos + i + kickPeriod / 2) % kickPeriod;
                        const float hat = 0.12f * std::exp (-(float) off / 1200.0f)
                                             * (rng.nextFloat() * 2.0f - 1.0f);

                        //  A bit of mid noise so the analyzer has a body.
                        const float white = rng.nextFloat() * 2.0f - 1.0f;
                        lp += 0.10f * (white - lp);
                        const float bed = 0.08f * lp;

                        const float v = kick + hat + bed;
                        buf.setSample (0, i, v);
                        buf.setSample (1, i, v);
                    }

                    samplePos += 512;
                }

                processor.processBlock (buf, midi);

                //  The techno shot pumps the display DURING playback so the
                //  trail and the energy ring have live history; the capture
                //  lands just after a kick.
                if (shot.audio == 2 && block % 4 == 3)
                    juce::MessageManager::getInstance()->runDispatchLoopUntil (14);
            }

            //  Let the editor's timer actually run, so the analyser drains
            //  the FIFO and transforms it. The techno shot must NOT idle here:
            //  a quarter second of silence releases the follower and erases
            //  exactly the live state the frame exists to show - it keeps
            //  playing right up to the capture instead.
            if (shot.audio == 2)
            {
                double phase2 = 0.0;
                int pos2 = samplePos;

                //  Run to just past the next kick onset, pumping as we go, and
                //  capture ~45 ms after the hit while the envelope is high.
                const int nextKick = ((pos2 / kickPeriod) + 1) * kickPeriod;
                const int stopAt = nextKick + 2200;

                while (pos2 < stopAt)
                {
                    for (int i = 0; i < 512; ++i)
                    {
                        const int t = (pos2 + i) % kickPeriod;
                        const float ts = (float) t / 48000.0f;
                        const double hz = 45.0 + 105.0 * std::exp (-ts * 28.0f);
                        phase2 += juce::MathConstants<double>::twoPi * hz / 48000.0;
                        const float kick = 0.9f * std::exp (-ts * 9.0f)
                                              * (float) std::sin (phase2);
                        buf.setSample (0, i, kick);
                        buf.setSample (1, i, kick);
                    }

                    processor.processBlock (buf, midi);
                    pos2 += 512;
                    juce::MessageManager::getInstance()->runDispatchLoopUntil (8);
                }
            }
            else
            {
                juce::MessageManager::getInstance()->runDispatchLoopUntil (250);
            }

            editor->setSize (shot.width, shot.height);

            if (shot.inspector >= 0)
            {
                if (auto* ed = dynamic_cast<EdgeAudioProcessorEditor*> (editor.get()))
                    ed->openInspectorForTest ((edge::ui::SelectedControl) shot.inspector);

                //  The strip fades in over 90 ms; give the animator time to
                //  land on full opacity before the frame is captured.
                juce::MessageManager::getInstance()->runDispatchLoopUntil (200);
            }

            juce::Image image (juce::Image::ARGB, editor->getWidth(), editor->getHeight(), true);
            {
                juce::Graphics g (image);
                editor->paintEntireComponent (g, true);
            }

            const auto file = outputDir.getChildFile (juce::String (shot.name) + ".png");
            file.deleteFile();

            juce::FileOutputStream stream (file);
            if (! stream.openedOk() || ! juce::PNGImageFormat().writeImageToStream (image, stream))
            {
                std::fprintf (stderr, "could not write %s\n", file.getFullPathName().toRawUTF8());
                return 1;
            }

            std::printf ("wrote %s  (%d x %d)\n", file.getFullPathName().toRawUTF8(),
                         image.getWidth(), image.getHeight());
        }

        return 0;
    }
}

namespace
{
    //  The manual's parameter table, written FROM the plug-in rather than typed
    //  next to it. A table maintained by hand is a table that disagrees with the
    //  build within one release.
    int writeParameterTable (const juce::File& out)
    {
        EdgeAudioProcessor processor;

        juce::StringArray rows;
        rows.add ("| Control | ID | Range | Default |");
        rows.add ("|---|---|---|---|");

        for (auto* raw : processor.getParameters())
        {
            auto* rp = dynamic_cast<juce::RangedAudioParameter*> (raw);
            if (rp == nullptr)
                continue;

            const auto& range = rp->getNormalisableRange();

            //  Shown the way the plug-in itself shows them, so the manual and
            //  the read-out cannot describe the same number differently.
            const auto low  = rp->getText (0.0f, 0);
            const auto high = rp->getText (1.0f, 0);
            const auto def  = rp->getText (rp->getDefaultValue(), 0);

            const bool discrete = rp->getNumSteps() > 0
                                    && rp->getNumSteps() < (int) juce::AudioProcessor::getDefaultNumParameterSteps();

            juce::String span = low + " … " + high;
            if (discrete && range.interval > 0.0f)
            {
                juce::StringArray items;
                for (int i = 0; i < rp->getNumSteps(); ++i)
                    items.add (rp->getText ((float) i / (float) juce::jmax (1, rp->getNumSteps() - 1), 0));

                items.removeDuplicates (false);
                span = items.joinIntoString (" / ");
            }

            rows.add ("| " + rp->getName (128) + " | `" + rp->paramID + "` | "
                        + span + " | " + def + " |");
        }

        rows.add ("");
        rows.add ("| # | Preset |");
        rows.add ("|---|---|");
        for (int i = 0; i < edge::kNumPresets; ++i)
            rows.add ("| " + juce::String (i) + " | " + edge::kPresets[i].name + " |");

        out.getParentDirectory().createDirectory();
        if (! out.replaceWithText (rows.joinIntoString ("\n") + "\n"))
        {
            std::fprintf (stderr, "could not write %s\n", out.getFullPathName().toRawUTF8());
            return 1;
        }

        std::printf ("wrote %s  (%d parameters, %d presets)\n",
                     out.getFullPathName().toRawUTF8(),
                     processor.getParameters().size(), edge::kNumPresets);
        return 0;
    }
}

namespace
{
    //  ---- logo mark candidates -------------------------------------------------
    //  A MARK, not a wordmark: something that survives at 32 px in a plug-in
    //  list and still says what EDGE is. Each one is pure JUCE Path geometry.
    //
    //  The shared idea they draw from: EDGE's response is flat, then it falls.
    //  That silhouette is the product.

    //  The canonical curve: flat run, then the fall, inside a unit box.
    juce::Path edgeCurve (juce::Rectangle<float> b, float kneeX = 0.42f)
    {
        juce::Path p;
        const float y0 = b.getY() + b.getHeight() * 0.30f;
        const float kx = b.getX() + b.getWidth() * kneeX;

        p.startNewSubPath (b.getX(), y0);
        p.lineTo (kx, y0);
        p.cubicTo (kx + b.getWidth() * 0.22f, y0,
                   b.getRight() - b.getWidth() * 0.10f, b.getBottom() - b.getHeight() * 0.22f,
                   b.getRight(), b.getBottom());
        return p;
    }

    //  1. THE CUT - the curve carved through a rounded square, the falling
    //     side filled. Reads as a filter at any size.
    void markCut (juce::Graphics& g, juce::Rectangle<float> b, juce::Colour ink, float w)
    {
        auto inner = b.reduced (b.getWidth() * 0.14f);

        juce::Path frame;
        frame.addRoundedRectangle (b, b.getWidth() * 0.22f);
        g.setColour (ink.withAlpha (0.30f));
        g.strokePath (frame, juce::PathStrokeType (w * 0.7f));

        auto curve = edgeCurve (inner);
        juce::Path fill (curve);
        fill.lineTo (inner.getRight(), inner.getBottom());
        fill.lineTo (inner.getX(), inner.getBottom());
        fill.closeSubPath();

        juce::Graphics::ScopedSaveState clip (g);
        juce::Path clipPath;
        clipPath.addRoundedRectangle (b.reduced (w * 0.5f), b.getWidth() * 0.20f);
        g.reduceClipRegion (clipPath);

        g.setColour (ink.withAlpha (0.18f));
        g.fillPath (fill);
        g.setColour (ink);
        g.strokePath (curve, juce::PathStrokeType (w, juce::PathStrokeType::curved));
    }

    //  2. THE JOURNEY - the curve as a dotted route ending in the destination
    //     diamond. The product's own signature object, alone.
    void markJourney (juce::Graphics& g, juce::Rectangle<float> b, juce::Colour ink, float w)
    {
        auto inner = b.reduced (b.getWidth() * 0.10f);
        auto curve = edgeCurve (inner, 0.30f);

        const float total = curve.getLength();
        const float step = juce::jmax (3.0f, b.getWidth() * 0.085f);

        for (float d = 0.0f; d < total - step * 0.5f; d += step)
        {
            const auto pt = curve.getPointAlongPath (d);
            const float t = d / total;
            const float r = w * (0.62f + 0.30f * (1.0f - t));
            g.setColour (ink.withAlpha (0.35f + 0.55f * (1.0f - t)));
            g.fillEllipse (pt.x - r, pt.y - r, r * 2.0f, r * 2.0f);
        }

        const auto end = curve.getPointAlongPath (total);
        const float dr = w * 1.7f;
        juce::Path diamond;
        diamond.addQuadrilateral (end.x, end.y - dr, end.x + dr, end.y,
                                  end.x, end.y + dr, end.x - dr, end.y);
        g.setColour (ink);
        g.strokePath (diamond, juce::PathStrokeType (w * 0.8f));
    }

    //  3. THE MONOGRAM - E, with its middle arm running flat and then falling.
    //     The wordmark's idea, isolated as a mark.
    void markMonogram (juce::Graphics& g, juce::Rectangle<float> b, juce::Colour ink, float w)
    {
        auto inner = b.reduced (b.getWidth() * 0.18f);
        const float x = inner.getX(), y = inner.getY();
        const float h = inner.getHeight(), wd = inner.getWidth();

        juce::Path p;
        p.startNewSubPath (x, y);
        p.lineTo (x, y + h);                       // spine
        p.startNewSubPath (x + w, y);
        p.lineTo (x + wd, y);                      // top
        p.startNewSubPath (x + w, y + h);
        p.lineTo (x + wd, y + h);                  // bottom

        //  the middle arm: flat, then the fall
        p.startNewSubPath (x + w, y + h * 0.5f);
        p.lineTo (x + wd * 0.62f, y + h * 0.5f);
        p.lineTo (x + wd, y + h * 0.5f + h * 0.30f);

        g.setColour (ink);
        g.strokePath (p, { w, juce::PathStrokeType::curved, juce::PathStrokeType::rounded });
    }

    //  4. TWO EDGES - the low edge rising from the left, the high edge falling
    //     to the right, the band between them. The whole plug-in in one shape.
    void markTwoEdges (juce::Graphics& g, juce::Rectangle<float> b,
                       juce::Colour lowInk, juce::Colour highInk, float w)
    {
        auto inner = b.reduced (b.getWidth() * 0.10f);
        const float top = inner.getY() + inner.getHeight() * 0.28f;

        juce::Path lowSide;
        lowSide.startNewSubPath (inner.getX(), inner.getBottom());
        lowSide.cubicTo (inner.getX() + inner.getWidth() * 0.16f, inner.getBottom(),
                         inner.getX() + inner.getWidth() * 0.22f, top,
                         inner.getCentreX(), top);

        juce::Path highSide;
        highSide.startNewSubPath (inner.getCentreX(), top);
        highSide.cubicTo (inner.getRight() - inner.getWidth() * 0.22f, top,
                          inner.getRight() - inner.getWidth() * 0.16f, inner.getBottom(),
                          inner.getRight(), inner.getBottom());

        g.setColour (lowInk);
        g.strokePath (lowSide, { w, juce::PathStrokeType::curved, juce::PathStrokeType::rounded });
        g.setColour (highInk);
        g.strokePath (highSide, { w, juce::PathStrokeType::curved, juce::PathStrokeType::rounded });
    }

    //  5. THE PUCK - the live position sitting on the knee. Minimal: a curve
    //     and the one dot that is travelling along it.
    void markPuck (juce::Graphics& g, juce::Rectangle<float> b, juce::Colour ink,
                   juce::Colour dot, float w)
    {
        auto inner = b.reduced (b.getWidth() * 0.12f);
        auto curve = edgeCurve (inner, 0.34f);

        g.setColour (ink.withAlpha (0.55f));
        g.strokePath (curve, { w, juce::PathStrokeType::curved });

        const auto pt = curve.getPointAlongPath (curve.getLength() * 0.42f);
        const float r = w * 1.9f;
        g.setColour (dot);
        g.fillEllipse (pt.x - r, pt.y - r, r * 2.0f, r * 2.0f);
        g.setColour (juce::Colour (0xff05070C));
        g.drawEllipse (pt.x - r * 0.55f, pt.y - r * 0.55f, r * 1.1f, r * 1.1f, w * 0.7f);
    }

    //  6. THE KNEE - no container, no ornament. Just the silhouette, bold.
    void markKnee (juce::Graphics& g, juce::Rectangle<float> b, juce::Colour ink, float w)
    {
        auto inner = b.reduced (b.getWidth() * 0.06f, b.getHeight() * 0.20f);
        g.setColour (ink);
        g.strokePath (edgeCurve (inner, 0.38f),
                      { w * 1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded });
    }

    //  The app icon: the mark on EDGE's own graph black, with the safe-area
    //  padding macOS and Windows both expect.
    int renderIcon (const juce::File& out, int size)
    {
        juce::Image image (juce::Image::ARGB, size, size, true);
        juce::Graphics g (image);

        const float s = (float) size;
        juce::Path bg;
        bg.addRoundedRectangle (0.0f, 0.0f, s, s, s * 0.22f);

        juce::ColourGradient grad (juce::Colour (0xff141A24), 0.0f, 0.0f,
                                   juce::Colour (0xff05070C), 0.0f, s, false);
        g.setGradientFill (grad);
        g.fillPath (bg);

        g.setColour (juce::Colours::white.withAlpha (0.06f));
        g.strokePath (bg, juce::PathStrokeType (s * 0.008f));

        //  The mark, centred, at 46 % of the canvas - the proportion that
        //  survives being masked into a circle by a launcher.
        const float markH = s * 0.46f, markW = markH * 0.86f;
        edge::ui::drawLogoMark (g, { (s - markW) * 0.5f, (s - markH) * 0.5f, markW, markH },
                                juce::Colour (0xffEDF4FF), s * 0.055f);

        out.getParentDirectory().createDirectory();
        out.deleteFile();
        juce::FileOutputStream stream (out);
        if (! stream.openedOk() || ! juce::PNGImageFormat().writeImageToStream (image, stream))
            return 1;

        std::printf ("wrote %s  (%d x %d)\n", out.getFullPathName().toRawUTF8(), size, size);
        return 0;
    }

    int renderLogoSheet (const juce::File& out)
    {
        constexpr int cell = 190, small = 34, cols = 6;
        const int W = cols * cell + 40, H = cell + 150;

        juce::Image image (juce::Image::ARGB, W, H, true);
        juce::Graphics g (image);

        g.setColour (juce::Colour (0xff05070C));
        g.fillAll();

        const juce::Colour ink (0xffEDF4FF);
        const juce::Colour amber (0xffFFAA3D), cyan (0xff35D5F2), violet (0xff9A7BFF);

        const char* names[cols] = { "1  CUT", "2  JOURNEY", "3  MONOGRAM",
                                    "4  TWO EDGES", "5  PUCK", "6  KNEE" };

        auto drawOne = [&] (int i, juce::Rectangle<float> b, float w)
        {
            switch (i)
            {
                case 0: markCut (g, b, ink, w); break;
                case 1: markJourney (g, b, cyan, w); break;
                case 2: markMonogram (g, b, ink, w); break;
                case 3: markTwoEdges (g, b, amber, cyan, w); break;
                case 4: markPuck (g, b, ink, violet, w); break;
                default: markKnee (g, b, ink, w); break;
            }
        };

        for (int i = 0; i < cols; ++i)
        {
            const auto big = juce::Rectangle<float> (20.0f + (float) (i * cell) + 22.0f,
                                                     28.0f, (float) cell - 44.0f,
                                                     (float) cell - 44.0f);
            drawOne (i, big, 5.5f);

            //  the same mark at plug-in-list size, where most logos die
            const auto tiny = juce::Rectangle<float> (20.0f + (float) (i * cell) + (float) (cell - small) * 0.5f,
                                                      (float) cell + 6.0f,
                                                      (float) small, (float) small);
            drawOne (i, tiny, 1.6f);

            g.setColour (juce::Colour (0xff8D96A3));
            g.setFont (juce::FontOptions (11.0f));
            g.drawText (names[i], 20 + i * cell, cell + 52, cell, 16,
                        juce::Justification::centred, false);
        }

        g.setColour (juce::Colour (0xff687182));
        g.setFont (juce::FontOptions (11.0f));
        g.drawText ("EDGE logo marks - large, and at 34 px where a plug-in list shows them",
                    20, H - 34, W - 40, 16, juce::Justification::centred, false);

        out.getParentDirectory().createDirectory();
        out.deleteFile();
        juce::FileOutputStream stream (out);
        if (! stream.openedOk() || ! juce::PNGImageFormat().writeImageToStream (image, stream))
            return 1;

        std::printf ("wrote %s\n", out.getFullPathName().toRawUTF8());
        return 0;
    }
}

int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI init;

    const juce::StringArray args (argv + 1, juce::jmax (0, argc - 1));

    if (args.contains ("--icon"))
    {
        const int i = args.indexOf ("--icon");
        const juce::String path = i + 1 < args.size() ? args[i + 1]
                                                      : juce::String ("packaging/icon.png");
        return renderIcon (juce::File::getCurrentWorkingDirectory().getChildFile (path), 1024);
    }

    if (args.contains ("--logo-sheet"))
    {
        const int i = args.indexOf ("--logo-sheet");
        const juce::String path = i + 1 < args.size() ? args[i + 1]
                                                      : juce::String ("outputs/logo-sheet.png");
        return renderLogoSheet (juce::File::getCurrentWorkingDirectory().getChildFile (path));
    }

    if (args.contains ("--parameter-table"))
    {
        const int i = args.indexOf ("--parameter-table");
        const juce::String path = i + 1 < args.size() ? args[i + 1]
                                                      : juce::String ("docs/PARAMETER-TABLE.md");
        return writeParameterTable (juce::File::getCurrentWorkingDirectory().getChildFile (path));
    }

    //  An unrecognised switch is a mistake, not a folder name. A stale build of
    //  this tool once took "--parameter-table" for an output directory and
    //  rendered seven PNGs into a folder of that name, which then went into a
    //  commit and a release zip.
    for (const auto& a : args)
    {
        if (a.startsWith ("--"))
        {
            std::fprintf (stderr, "unknown option: %s\n"
                                  "usage: EdgeShot [<output-dir>]\n"
                                  "       EdgeShot --parameter-table [<file.md>]\n",
                          a.toRawUTF8());
            return 2;
        }
    }

    const juce::File dir = args.size() > 0
        ? juce::File::getCurrentWorkingDirectory().getChildFile (args[0])
        : juce::File::getCurrentWorkingDirectory().getChildFile ("ui-shots");

    return render (dir);
}
