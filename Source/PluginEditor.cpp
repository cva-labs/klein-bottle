/*
    Klein Bottle Experimental — physical-modelling synthesizer
    Copyright (C) 2026 CVA Labs
    SPDX-License-Identifier: AGPL-3.0-or-later
*/
#include "PluginEditor.h"
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

    addSlider (param::pitch,    "Note",      juce::Slider::LinearHorizontal);
    addSlider (param::shape,    "Shape");
    addSlider (param::decay,    "Decay");
    addCombo  (param::topology, "Topology",  { "Klein Bottle", "Torus", "Mobius Band",
                                               "Cylinder", "Membrane" });
    addCombo  (param::quality,  "Quality",   { "Eco", "Standard", "High" });

    addCombo  (param::excType,  "Type",      { "Mallet", "Pluck", "Noise Burst", "Bow",
                                               "Wind" });
    addSlider (param::excU,     "Pos U");
    addSlider (param::excV,     "Pos V");
    addSlider (param::force,    "Force");
    addSlider (param::hard,     "Hardness");
    addSlider (param::motion,   "Motion");

    addSlider (param::pickU,    "Pos U");
    addSlider (param::pickV,    "Pos V");
    addCombo  (param::pickMode, "Mode",      { "Displacement", "Velocity" });
    addSlider (param::spread,   "Spread");

    addSlider (param::level,    "Level",     juce::Slider::LinearHorizontal);

    pluckButton.onClick = [this] { proc.triggerPluck(); };
    panicButton.onClick = [this] { proc.panic(); };
    addAndMakeVisible (pluckButton);
    addAndMakeVisible (panicButton);

    setResizable (true, true);
    setResizeLimits (1242, 846, 2530, 1690);
    setSize (1322, 925);   // +15% over the original 1150 x 805
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

    const int panelH = juce::jlimit (170, 236, (int) (area.getHeight() * 0.36f));
    auto panelArea = area.removeFromBottom (panelH);
    area.removeFromBottom (6);
    visualizer.setBounds (area);

    const int w0 = (int) ((float) panelArea.getWidth() * 0.27f);
    const int w1 = (int) ((float) panelArea.getWidth() * 0.27f);
    const int w2 = (int) ((float) panelArea.getWidth() * 0.24f);
    const int w3 = panelArea.getWidth() - w0 - w1 - w2;

    group[0].setBounds (panelArea.removeFromLeft (w0));
    group[1].setBounds (panelArea.removeFromLeft (w1));
    group[2].setBounds (panelArea.removeFromLeft (w2));
    group[3].setBounds (panelArea);

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

    fillGroup (0, { { param::pitch, false }, { param::shape, false }, { param::decay, false },
                    { param::topology, true }, { param::quality, true } });
    fillGroup (1, { { param::excType, true }, { param::excU, false }, { param::excV, false },
                    { param::force, false }, { param::hard, false }, { param::motion, false } });
    fillGroup (2, { { param::pickU, false }, { param::pickV, false },
                    { param::pickMode, true }, { param::spread, false } });

    {
        auto inner = group[3].getBounds()           // absolute: controls are editor children
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