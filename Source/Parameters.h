/*
    Klein Bottle Experimental — physical-modelling synthesizer
    Copyright (C) 2026 CVA Labs
    SPDX-License-Identifier: AGPL-3.0-or-later
*/
#pragma once
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace kb
{

namespace param
{
    inline constexpr const char* pitch    = "pitch";
    inline constexpr const char* shape    = "shape";
    inline constexpr const char* decay    = "decay";
    inline constexpr const char* topology = "topology";
    inline constexpr const char* quality  = "quality";
    inline constexpr const char* excType  = "excType";
    inline constexpr const char* excU     = "excU";
    inline constexpr const char* excV     = "excV";
    inline constexpr const char* force    = "force";
    inline constexpr const char* hard     = "hard";
    inline constexpr const char* motion   = "motion";
    inline constexpr const char* keyTrack = "keyTrack";
    inline constexpr const char* pickU    = "pickU";
    inline constexpr const char* pickV    = "pickV";
    inline constexpr const char* pickMode = "pickMode";
    inline constexpr const char* spread   = "spread";
    inline constexpr const char* level    = "level";
}

inline juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    using namespace juce;

    AudioProcessorValueTreeState::ParameterLayout layout;

    // --- resonator -----------------------------------------------------------
    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { param::pitch, 1 }, "Root Note",
        NormalisableRange<float> (24.0f, 96.0f, 1.0f), 48.0f,
        AudioParameterFloatAttributes().withLabel ("semi")));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { param::shape, 1 }, "Body Shape",
        NormalisableRange<float> (0.25f, 1.0f), 0.5f));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { param::decay, 1 }, "Decay",
        NormalisableRange<float> (0.0f, 1.0f), 0.65f));

    layout.add (std::make_unique<AudioParameterChoice> (
        ParameterID { param::topology, 1 }, "Topology",
        StringArray { "Klein Bottle", "Torus", "Mobius Band", "Cylinder", "Membrane" }, 0));

    layout.add (std::make_unique<AudioParameterChoice> (
        ParameterID { param::quality, 1 }, "Mesh Quality",
        StringArray { "Eco", "Standard", "High" }, 1));

    // --- exciter ---------------------------------------------------------------
    layout.add (std::make_unique<AudioParameterChoice> (
        ParameterID { param::excType, 1 }, "Exciter",
        StringArray { "Mallet", "Pluck", "Noise Burst", "Bow", "Wind" }, 0));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { param::excU, 1 }, "Excite Pos U",
        NormalisableRange<float> (0.0f, 1.0f), 0.32f));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { param::excV, 1 }, "Excite Pos V",
        NormalisableRange<float> (0.0f, 1.0f), 0.40f));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { param::force, 1 }, "Force",
        NormalisableRange<float> (0.0f, 1.0f), 0.85f));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { param::hard, 1 }, "Hardness",
        NormalisableRange<float> (0.0f, 1.0f), 0.45f));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { param::motion, 1 }, "Motion",
        NormalisableRange<float> (0.0f, 1.0f), 0.0f));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { param::keyTrack, 1 }, "Key -> Pos U",
        NormalisableRange<float> (0.0f, 1.0f), 0.55f));

    // --- pickup -----------------------------------------------------------------
    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { param::pickU, 1 }, "Pickup Pos U",
        NormalisableRange<float> (0.0f, 1.0f), 0.68f));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { param::pickV, 1 }, "Pickup Pos V",
        NormalisableRange<float> (0.0f, 1.0f), 0.58f));

    layout.add (std::make_unique<AudioParameterChoice> (
        ParameterID { param::pickMode, 1 }, "Pickup Mode",
        StringArray { "Displacement", "Velocity" }, 1));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { param::spread, 1 }, "Stereo Spread",
        NormalisableRange<float> (0.0f, 1.0f), 0.40f));

    // --- output -------------------------------------------------------------------
    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { param::level, 1 }, "Output Level",
        NormalisableRange<float> (-60.0f, 12.0f), -6.0f,
        AudioParameterFloatAttributes().withLabel ("dB")));

    return layout;
}

} // namespace kb