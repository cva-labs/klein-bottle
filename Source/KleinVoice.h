/*
    Klein Bottle Experimental — physical-modelling synthesizer
    Copyright (C) 2026 CVA Labs
    SPDX-License-Identifier: AGPL-3.0-or-later
*/
#pragma once
#pragma once

#include "KleinMesh.h"

#include <cstdint>
#include <vector>

namespace kb
{

/** Block-rate parameter snapshot, mapped from the APVTS by the processor. */
struct VoiceParams
{
    int   excType      = 0;      // 0 mallet, 1 pluck, 2 noise burst, 3 bow, 4 wind
    int   topology     = 0;      // kb::Topology
    int   quality      = 1;      // 0 eco, 1 standard, 2 high
    float sigma        = 0.0f;   // per-sample damping at fold = 1
    float shape        = 0.5f;   // grid aspect ratio ny / nx
    float excU         = 0.32f;
    float excV         = 0.50f;
    float force        = 0.85f;
    float hard         = 0.45f;
    float motion       = 0.0f;   // exciter auto-orbit amount
    float keyTrack     = 0.55f;  // note -> exciter U offset
    float pickU        = 0.68f;
    float pickV        = 0.50f;
    bool  pickVelocity = true;   // false = displacement readout
    float spread       = 0.40f;
    // derived
    float bowSpeed  = 0.15f;
    float bowSharp  = 50.0f;
    int   nxMax     = 112;       // quality cap on the long grid axis
    int   nodeBudget = 10000;    // per-voice node-step budget (per output sample)
};

/** One voice = one Klein bottle mesh. */
struct Voice
{
    bool   active = false;
    int    note   = -1;
    std::uint64_t order = 0;
    bool   bowing = false;
    bool   blowing = false;
    float  velocity = 1.0f;

    int   fold  = 1;              // mesh runs at fold * fs; output pitched down
    float levelComp = 1.0f;       // register tilt compensation (see startVoice)
    float baseU = 0.32f, baseV = 0.50f;
    float liveU = 0.32f, liveV = 0.50f;
    float pan   = 0.0f;

    float dcAx = 0.0f, dcAy = 0.0f;
    float dcBx = 0.0f, dcBy = 0.0f;
    float silenceTime = 0.0f;

    KleinMesh mesh;
    std::vector<float> meshL, meshR;
};

} // namespace kb