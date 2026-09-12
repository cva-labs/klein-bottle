/*
    Klein Bottle Experimental — physical-modelling synthesizer
    Copyright (C) 2026 CVA Labs
    SPDX-License-Identifier: AGPL-3.0-or-later
*/
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "PluginProcessor.h"

namespace kb
{

//==============================================================================
/*  Live 3D view of the instrument: the resonator drawn as a wireframe whose
    vertices are coloured by the current mesh vibration, rebuilt to match the
    selected topology (Klein bottle, torus, Mobius band, cylinder, membrane).
    Drag to rotate.  Mesh (u,v) parameter space maps onto the Klein immersion
    as  immersion-U = 2*pi*v,  immersion-V = 2*pi*u  (so a wave circling the
    twisted direction of the mesh visibly circles the bottle and returns
    travelling the other way).                                                     */
class KleinVisualizer : public juce::Component
{
public:
    explicit KleinVisualizer (KleinBottleAudioProcessor& processor);

    void paint (juce::Graphics&) override;
    void resized() override {}

    void mouseDown  (const juce::MouseEvent&) override;
    void mouseDrag  (const juce::MouseEvent&) override;
    void mouseUp    (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    /** call periodically (GUI thread): refreshes the snapshot & auto-rotates */
    void advance (double deltaSeconds);

private:
    struct Vtx { float x = 0, y = 0, z = 0; };

    void buildGeometry (int topoIndex);
    /** 3D point of mesh parameter (mu, mv) for the given topology index. */
    Vtx immersed (int topoIndex, float mu, float mv) const;

    KleinBottleAudioProcessor& proc;
    KleinBottleAudioProcessor::DisplaySnapshot snap;
    juce::Image logo;             // CVA Labs logo (embedded resource, may be null)

    std::vector<Vtx> base;        // immersion points, mesh-Y-major grid
    int builtTopo = -1;           // topology the `base` wireframe was built for
    int nu = 30;                  // immersion U samples  (= mesh V / twist direction)
    int nv = 60;                  // immersion V samples  (= mesh U / periodic direction)

    float yaw = 0.70f, pitchT = 0.42f, zoom = 1.0f;
    bool dragging = false;
    juce::Point<float> lastMouse;
    juce::String statusText;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KleinVisualizer)
};

} // namespace kb
