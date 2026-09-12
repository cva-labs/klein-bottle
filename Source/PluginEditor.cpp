/*
    Klein Bottle Experimental — physical-modelling synthesizer
    Copyright (C) 2026 CVA Labs
    SPDX-License-Identifier: AGPL-3.0-or-later
*/
#include "PluginEditor.h"

namespace kb
{

//==============================================================================
juce::AudioProcessorEditor* KleinBottleAudioProcessor::createEditor()
{
    return new KleinBottleAudioProcessorEditor (*this);
}

KleinBottleAudioProcessorEditor::KleinBottleAudioProcessorEditor (KleinBottleAudioProcessor& p)
    : AudioProcessorEditor (p),
      proc (p),
      visualizer (p),
      ribbon (p)
{
    visualizer.addMouseListener (this, false);
    addAndMakeVisible (visualizer);

    addAndMakeVisible (ribbon);

    for (auto& g : group)
        addAndMakeVisible (g);

    presetLabel.setText ("PRESET", juce::dontSendNotification);
    presetLabel.setColour (juce::Label::textColourId, juce::Colour (0xff9aa5b5));
    presetLabel.setFont (juce::Font (juce::FontOptions (12.0f, juce::Font::bold)));
    addAndMakeVisible (presetLabel);
    for (int i = 0; i < proc.getNumPrograms(); ++i)
        presetBox.addItem (proc.getProgramName (i), i + 1);
    presetBox.setSelectedItemIndex (proc.getCurrentProgram(), juce::dontSendNotification);
    presetBox.setColour (juce::ComboBox::backgroundColourId, juce::Colour (0xff20262f));
    presetBox.setColour (juce::ComboBox::outlineColourId, juce::Colour (0xff4fc3f7));
    presetBox.setColour (juce::ComboBox::textColourId, juce::Colour (0xffd5dbe4));
    presetBox.setColour (juce::ComboBox::arrowColourId, juce::Colour (0xff4fc3f7));
    presetBox.onChange = [this] { proc.setCurrentProgram (presetBox.getSelectedItemIndex()); };
    addAndMakeVisible (presetBox);

    addSlider (param::decay,    "Decay").slider.setTooltip ("Controls the acoustic decay time from a short hit to a long ring");
    addCombo  (param::topology, "Topology",  { "Klein Bottle", "Mobius Band", "Membrane" });
    addCombo  (param::excType,  "Exciter",   { "Mallet", "Pluck", "Noise Burst", "Bow",
                                               "Wind" });
    addSlider (param::hard,     "Tone").slider.setTooltip ("Moves from soft and dark to hard and bright");
    addSlider (param::motion,   "Motion").slider.setTooltip ("Orbits the excitation point across the surface");
    addSlider (param::spread,   "Width").slider.setTooltip ("Controls stereo width from mono to wide");

    addSlider (param::level,    "Level",     juce::Slider::LinearHorizontal);

    pluckButton.setButtonText ("Trigger");
    pluckButton.setTooltip ("Preview the selected sound");
    panicButton.setTooltip ("Immediately stop and clear all voices");
    pluckButton.onClick = [this] { proc.triggerPluck(); };
    panicButton.onClick = [this] { proc.panic(); };
    addAndMakeVisible (pluckButton);
    addAndMakeVisible (panicButton);

    setResizable (true, true);
    setResizeLimits (900, 650, 2530, 1690);
    setSize (1180, 820);
    startTimerHz (30);
}

KleinBottleAudioProcessorEditor::~KleinBottleAudioProcessorEditor()
{
    stopTimer();
}

//==============================================================================
KleinBottleAudioProcessorEditor::AttachedSlider&
KleinBottleAudioProcessorEditor::addSlider (const juce::String& id, const juce::String& text,
                                            juce::Slider::SliderStyle style)
{
    auto a = std::make_unique<AttachedSlider>();
    a->id = id;
    a->slider.setSliderStyle (style);
    a->slider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 62, 18);
    a->slider.setColour (juce::Slider::rotarySliderFillColourId, juce::Colour (0xff4fc3f7));
    a->slider.setColour (juce::Slider::trackColourId, juce::Colour (0xff4fc3f7));
    a->slider.setColour (juce::Slider::thumbColourId, juce::Colour (0xffb3e5fc));
    a->slider.setColour (juce::Slider::backgroundColourId, juce::Colour (0xff20262f));
    a->slider.setColour (juce::Slider::textBoxTextColourId, juce::Colour (0xffc8d2e0));
    a->slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colour (0xff2a313d));

    a->label.setText (text, juce::dontSendNotification);
    a->label.setFont (juce::Font (juce::FontOptions (12.0f)));
    a->label.setColour (juce::Label::textColourId, juce::Colour (0xff9aa5b5));
    a->label.setJustificationType (juce::Justification::centredLeft);

    a->attach = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        proc.apvts, id, a->slider);

    addAndMakeVisible (a->slider);
    addAndMakeVisible (a->label);
    sliders.push_back (std::move (a));
    return *sliders.back();
}

KleinBottleAudioProcessorEditor::AttachedCombo&
KleinBottleAudioProcessorEditor::addCombo (const juce::String& id, const juce::String& text,
                                           const juce::StringArray& items)
{
    auto a = std::make_unique<AttachedCombo>();
    a->id = id;
    for (int i = 0; i < items.size(); ++i)
        a->box.addItem (items[i], i + 1);
    a->box.setColour (juce::ComboBox::backgroundColourId, juce::Colour (0xff20262f));
    a->box.setColour (juce::ComboBox::outlineColourId, juce::Colour (0xff2a313d));
    a->box.setColour (juce::ComboBox::textColourId, juce::Colour (0xffc8d2e0));
    a->box.setColour (juce::ComboBox::arrowColourId, juce::Colour (0xff4fc3f7));

    a->label.setText (text, juce::dontSendNotification);
    a->label.setFont (juce::Font (juce::FontOptions (12.0f)));
    a->label.setColour (juce::Label::textColourId, juce::Colour (0xff9aa5b5));
    a->label.setJustificationType (juce::Justification::centredLeft);

    a->attach = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
        proc.apvts, id, a->box);

    addAndMakeVisible (a->box);
    addAndMakeVisible (a->label);
    combos.push_back (std::move (a));
    return *combos.back();
}

KleinBottleAudioProcessorEditor::AttachedSlider*
KleinBottleAudioProcessorEditor::findSlider (const juce::String& id)
{
    for (auto& s : sliders)
        if (s->id == id)
            return s.get();
    return nullptr;
}

KleinBottleAudioProcessorEditor::AttachedCombo*
KleinBottleAudioProcessorEditor::findCombo (const juce::String& id)
{
    for (auto& c : combos)
        if (c->id == id)
            return c.get();
    return nullptr;
}

void KleinBottleAudioProcessorEditor::timerCallback()
{
    visualizer.advance (1.0 / 30.0);
    if (! presetBox.isPopupActive()
        && presetBox.getSelectedItemIndex() != proc.getCurrentProgram())
        presetBox.setSelectedItemIndex (proc.getCurrentProgram(), juce::dontSendNotification);
}

void KleinBottleAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff0e1116));
}

//==============================================================================
void KleinBottleAudioProcessorEditor::resized()
{
    auto area = getLocalBounds().reduced (8);

    ribbon.setBounds (area.removeFromBottom (64));
    area.removeFromBottom (6);

    auto presetRow = area.removeFromTop (36);
    presetLabel.setBounds (presetRow.removeFromLeft (72));
    presetBox.setBounds (presetRow.removeFromLeft (260).reduced (0, 3));
    area.removeFromTop (6);

    const int panelH = juce::jlimit (150, 205, (int) (area.getHeight() * 0.31f));
    auto panelArea = area.removeFromBottom (panelH);
    area.removeFromBottom (6);
    visualizer.setBounds (area);

    const int w0 = (int) ((float) panelArea.getWidth() * 0.40f);
    const int w1 = (int) ((float) panelArea.getWidth() * 0.30f);

    group[0].setBounds (panelArea.removeFromLeft (w0));
    group[1].setBounds (panelArea.removeFromLeft (w1));
    group[2].setBounds (panelArea);

    auto fillGroup = [this] (int gi, const std::vector<std::pair<juce::String, bool>>& rows)
    {
        auto inner = group[gi].getBounds()          // absolute: controls are editor children
                        .withTrimmedLeft (10).withTrimmedTop (22)
                        .withTrimmedRight (10).withTrimmedBottom (8);
        const int n = (int) rows.size();
        if (n == 0)
            return;
        const int rh = inner.getHeight() / n;

        for (int i = 0; i < n; ++i)
        {
            auto row = inner.removeFromTop (rh);
            juce::Component* lbl = nullptr;
            juce::Component* ctl = nullptr;

            if (rows[(size_t) i].second)
            {
                if (auto* c = findCombo (rows[(size_t) i].first))
                {
                    lbl = &c->label;
                    ctl = &c->box;
                }
            }
            else
            {
                if (auto* s = findSlider (rows[(size_t) i].first))
                {
                    lbl = &s->label;
                    ctl = &s->slider;
                }
            }

            if (lbl != nullptr) lbl->setBounds (row.removeFromLeft (70).reduced (0, 2));
            if (ctl != nullptr) ctl->setBounds (row.reduced (1, 3));
        }
    };

    fillGroup (0, { { param::topology, true }, { param::excType, true },
                    { param::decay, false }, { param::hard, false } });
    fillGroup (1, { { param::motion, false }, { param::spread, false } });

    {
        auto inner = group[2].getBounds()           // absolute: controls are editor children
                        .withTrimmedLeft (10).withTrimmedTop (22)
                        .withTrimmedRight (10).withTrimmedBottom (8);
        const int rh = inner.getHeight() / 3;

        auto levelRow = inner.removeFromTop (rh);
        if (auto* s = findSlider (param::level))
        {
            s->label.setBounds (levelRow.removeFromLeft (70).reduced (0, 2));
            s->slider.setBounds (levelRow.reduced (1, 3));
        }

        auto btnRow = inner.removeFromTop (rh).reduced (1, 6);
        pluckButton.setBounds (btnRow.removeFromLeft (btnRow.getWidth() / 2));
        panicButton.setBounds (btnRow);

        inner.removeFromTop (rh);   // spare space
    }
}

} // namespace kb
