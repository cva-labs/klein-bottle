/*
    Klein Bottle Experimental — physical-modelling synthesizer
    Copyright (C) 2026 CVA Labs
    SPDX-License-Identifier: AGPL-3.0-or-later
*/
#pragma once
#pragma once

#include <cmath>

#include <juce_audio_utils/juce_audio_utils.h>

#include "Parameters.h"
#include "PluginProcessor.h"
#include "KleinVisualizer.h"

namespace kb
{

//==============================================================================
/*  Continuous "ribbon" controller that replaces the on-screen keyboard:
    drag horizontally to play (re-triggers on every semitone crossing, like a
    monophonic ribbon), vertical position sets the velocity (top = loudest). */
class NoteRibbon : public juce::Component
{
public:
    explicit NoteRibbon (KleinBottleAudioProcessor& p) : proc (p) {}

    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat();
        g.setColour (juce::Colour (0xff171c24));
        g.fillRoundedRectangle (b, 6.0f);
        g.setColour (juce::Colour (0xff2a313d));
        g.drawRoundedRectangle (b.reduced (0.5f), 6.0f, 1.0f);

        const float x0 = b.getX() + 8.0f;
        const float w  = b.getWidth() - 16.0f;

        for (int n = kLow; n <= kHigh; ++n)
        {
            const float x  = x0 + w * (n - kLow) / (float) (kHigh - kLow);
            const bool isC = (n % 12) == 0;
            g.setColour (isC ? juce::Colour (0xff4fc3f7) : juce::Colour (0xff2a313d));
            const float inset = isC ? 22.0f : 12.0f;
            g.drawLine (x, b.getY() + inset, x, b.getBottom() - 18.0f, isC ? 1.4f : 1.0f);

            if (isC)
            {
                g.setColour (juce::Colour (0xff8a93a2));
                g.setFont (juce::Font (juce::FontOptions (11.0f)));
                g.drawText (noteName (n), x - 16.0f, b.getBottom() - 17.0f, 32.0f, 14.0f,
                            juce::Justification::centred);
            }
        }

        if (currentNote >= 0)
        {
            const float x = x0 + w * (currentNote - kLow) / (float) (kHigh - kLow);
            g.setColour (juce::Colour (0xffff5533).withAlpha (0.25f));
            g.fillRect (x - 8.0f, b.getY() + 6.0f, 16.0f, b.getHeight() - 26.0f);
            g.setColour (juce::Colour (0xffff5533));
            g.fillRect (x - 2.0f, b.getY() + 6.0f, 4.0f, b.getHeight() - 26.0f);
            g.setFont (juce::Font (juce::FontOptions (12.0f, juce::Font::bold)));
            g.drawText (noteName (currentNote), x - 24.0f, b.getY() + 2.0f, 48.0f, 14.0f,
                        juce::Justification::centred);
        }

        g.setColour (juce::Colour (0xff66707f));
        g.setFont (juce::Font (juce::FontOptions (11.0f)));
        g.drawText ("DRAG TO PLAY  -  VERTICAL POSITION = VELOCITY",
                    b.getX() + 12.0f, b.getY() + 3.0f, 300.0f, 14.0f,
                    juce::Justification::topLeft);
    }

    void mouseDown (const juce::MouseEvent& e) override { play (e); }
    void mouseDrag (const juce::MouseEvent& e) override { play (e); }
    void mouseUp   (const juce::MouseEvent&)    override { stop(); }

private:
    static constexpr int kLow = 24, kHigh = 96;

    static juce::String noteName (int n)
    {
        static const char* names[] = { "C", "C#", "D", "D#", "E", "F",
                                       "F#", "G", "G#", "A", "A#", "B" };
        return juce::String (names[n % 12]) + juce::String (n / 12 - 1);
    }

    int noteFromX (juce::Point<float> pos) const
    {
        const float t = juce::jlimit (0.0f, 1.0f, (pos.x - 8.0f) / ((float) getWidth() - 16.0f));
        return juce::jlimit (kLow, kHigh, kLow + (int) std::lround (t * (kHigh - kLow)));
    }

    void play (const juce::MouseEvent& e)
    {
        const int   n   = noteFromX (e.position);
        const float vel = juce::jlimit (0.25f, 1.0f,
                                        1.0f - 0.75f * e.position.y / (float) getHeight());
        if (n == currentNote)
            return;

        if (currentNote >= 0)
            proc.keyboardState.noteOff (1, currentNote, 0.0f);

        currentNote = n;
        proc.keyboardState.noteOn (1, n, vel);
        repaint();
    }

    void stop()
    {
        if (currentNote >= 0)
        {
            proc.keyboardState.noteOff (1, currentNote, 0.0f);
            currentNote = -1;
            repaint();
        }
    }

    KleinBottleAudioProcessor& proc;
    int currentNote = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NoteRibbon)
};

//==============================================================================
class KleinBottleAudioProcessorEditor : public juce::AudioProcessorEditor,
                                        private juce::Timer
{
public:
    explicit KleinBottleAudioProcessorEditor (KleinBottleAudioProcessor&);
    ~KleinBottleAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    struct AttachedSlider
    {
        juce::Slider slider;
        juce::Label  label;
        juce::String id;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attach;
    };

    struct AttachedCombo
    {
        juce::ComboBox box;
        juce::Label    label;
        juce::String   id;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> attach;
    };

    void timerCallback() override;
    AttachedSlider& addSlider (const juce::String& id, const juce::String& text,
                               juce::Slider::SliderStyle style = juce::Slider::LinearBar);
    AttachedCombo&  addCombo (const juce::String& id, const juce::String& text,
                              const juce::StringArray& items);

    AttachedSlider* findSlider (const juce::String& id);
    AttachedCombo*  findCombo  (const juce::String& id);

    struct GroupPanel : juce::Component
    {
        GroupPanel (const juce::String& t) : title (t) {}

        void paint (juce::Graphics& g) override
        {
            auto b = getLocalBounds().toFloat();
            g.setColour (juce::Colour (0xff171c24));
            g.fillRoundedRectangle (b, 6.0f);
            g.setColour (juce::Colour (0xff2a313d));
            g.drawRoundedRectangle (b.reduced (0.5f), 6.0f, 1.0f);
            g.setColour (juce::Colour (0xff9aa5b5));
            g.setFont (juce::Font (juce::FontOptions (13.0f, juce::Font::bold)));
            g.drawText (title, 10, 4, getWidth() - 20, 16, juce::Justification::centredLeft);
        }

        juce::String title;
    };

    KleinBottleAudioProcessor& proc;
    KleinVisualizer visualizer;
    NoteRibbon ribbon;

    std::vector<std::unique_ptr<AttachedSlider>> sliders;
    std::vector<std::unique_ptr<AttachedCombo>>  combos;

    juce::TextButton pluckButton { "Pluck" };
    juce::TextButton panicButton { "Panic" };
    GroupPanel group[4] { { "Resonator" }, { "Exciter" }, { "Pickup" }, { "Output" } };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KleinBottleAudioProcessorEditor)
};

} // namespace kb