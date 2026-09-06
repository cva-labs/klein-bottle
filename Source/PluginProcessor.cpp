/*
    Klein Bottle Experimental — physical-modelling synthesizer
    Copyright (C) 2026 CVA Labs
    SPDX-License-Identifier: AGPL-3.0-or-later
*/
#include "PluginProcessor.h"
#include "PluginProcessor.h"
#include "Parameters.h"

#include <cmath>

namespace kb
{

namespace
{
    float wrap01f (float x) noexcept { return x - std::floor (x); }

    // Fixed per-voice make-up gain (+9.5 dB) applied before the master stage.
    constexpr float kVoiceMakeUp = 3.0f;

    // Bow / wind exciters are self-limiting (friction pump / filtered breath),
    // so their sustain sits far below the struck sounds.  Lift each of them
    // (individually) closer to the mallet/pluck level.
    constexpr float kBowMakeUp  = 2.2f;
    constexpr float kWindMakeUp = 1.3f;
}

//==============================================================================
KleinBottleAudioProcessor::KleinBottleAudioProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createParameterLayout())
{
    pPitch     = P (param::pitch);
    pShape     = P (param::shape);
    pDecay     = P (param::decay);
    pTopology  = P (param::topology);
    pQuality   = P (param::quality);
    pExcType   = P (param::excType);
    pExcU      = P (param::excU);
    pExcV      = P (param::excV);
    pForce     = P (param::force);
    pHard      = P (param::hard);
    pMotion    = P (param::motion);
    pKeyTrack  = P (param::keyTrack);
    pPickU     = P (param::pickU);
    pPickV     = P (param::pickV);
    pPickMode  = P (param::pickMode);
    pSpread    = P (param::spread);
    pLevel     = P (param::level);

    keyboardState.addListener (this);
}

KleinBottleAudioProcessor::~KleinBottleAudioProcessor()
{
    keyboardState.removeListener (this);
}

std::atomic<float>* KleinBottleAudioProcessor::P (const char* id) const
{
    auto* p = apvts.getRawParameterValue (id);
    jassert (p != nullptr);
    return p;
}

//==============================================================================
void KleinBottleAudioProcessor::prepareToPlay (double sampleRate, int /*samplesPerBlock*/)
{
    fs = (sampleRate > 1000.0 ? sampleRate : 48000.0);

    for (auto& v : voices)
    {
        v.active = false;
        v.mesh.setSampleRate (fs);
        // Worst case across all quality settings / body-shape values (nxMax
        // 144 @ high quality, shape up to 1.0 -> ny can equal nx): reserving
        // it up front means resize() below never reallocates on the audio
        // thread when a note-on picks a different grid size (e.g. rapid
        // voice-stealing during a fast glissando).
        v.mesh.reserveMax (144, 144);
        v.mesh.clear();
        // Pre-reserve the per-voice fold-down scratch so hosts that chop audio
        // into varying block sizes never trigger a vector reallocation (and a
        // click) on the audio thread.  Covers 4096-sample blocks at fold 16.
        v.meshL.reserve (kMeshReserveSamples);
        v.meshR.reserve (kMeshReserveSamples);
    }

    levelSmooth.reset (fs, 0.03);
    levelSmooth.setCurrentAndTargetValue (
        juce::Decibels::decibelsToGain (pLevel != nullptr ? pLevel->load() : -6.0f));

    {
        const juce::ScopedLock sl (pendingLock);
        pendingQueue.clear();
    }
    sustainedNotes.clear();
    sustainHeld = false;
    modWheel.store (0.0f);
    modWheelTarget = 0.0f;
}

bool KleinBottleAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono();
}

//==============================================================================
void KleinBottleAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void KleinBottleAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

//==============================================================================
KleinBottleAudioProcessor::BlockParams KleinBottleAudioProcessor::readBlockParams() const
{
    BlockParams bp;

    bp.pitch    = pPitch->load();
    bp.shape    = pShape->load();
    bp.decay    = pDecay->load();
    bp.topology = (int) pTopology->load();
    bp.quality  = (int) pQuality->load();
    bp.excType  = (int) pExcType->load();
    bp.excU     = pExcU->load();
    bp.excV     = pExcV->load();
    bp.force    = pForce->load();
    bp.hard     = pHard->load();
    bp.motion   = pMotion->load();
    bp.keyTrack = pKeyTrack->load();
    bp.pickU    = pPickU->load();
    bp.pickV    = pPickV->load();
    bp.pickMode = (int) pPickMode->load();
    bp.spread   = pSpread->load();
    bp.levelDb  = pLevel->load();

    const double t60 = 0.02 * std::pow (1500.0, (double) bp.decay);   // 20 ms .. 30 s
    bp.sigma       = KleinMesh::sigmaForT60 (t60, fs);
    bp.motionAmt   = bp.motion;
    bp.motionRate  = 0.05f + 0.75f * bp.motion;
    bp.bowSpeed    = 0.045f + 0.085f * bp.hard;   // ~ node velocities of the limit cycle
    bp.bowSharp    = 10.0f + 90.0f * bp.hard;
    bp.levelGain   = juce::Decibels::decibelsToGain (bp.levelDb);

    static constexpr int nxMaxs[3]   = { 80, 112, 144 };
    static constexpr int budgets[3]  = { 4000, 10000, 25000 };   // at the 48 kHz reference rate
    const int q = juce::jlimit (0, 2, bp.quality);
    bp.nxMax      = nxMaxs[q];
    //  The budget counts node-steps per output sample, so the per-SECOND cost
    //  (budget * fs) would grow linearly with the host sample rate: low notes
    //  saturate the budget and a single note below ~A3 would peg a core in
    //  96/192 kHz sessions (the standalone at 48 kHz stays fine - which made
    //  this look like a host-only bug).  Normalise to the 48 kHz reference so
    //  the load is rate-independent; very high rates trade a little timbre
    //  density for bounded CPU.
    const double rateScale = juce::jlimit (0.25, 2.0, 48000.0 / fs);
    bp.nodeBudget = (int) std::max (2500.0, (double) budgets[q] * rateScale);

    return bp;
}

//==============================================================================
Voice* KleinBottleAudioProcessor::findVoiceToSteal()
{
    for (auto& v : voices)
        if (! v.active)
            return &v;

    Voice* oldest = &voices[0];
    for (auto& v : voices)
        if (v.order < oldest->order)
            oldest = &v;
    return oldest;
}

Voice* KleinBottleAudioProcessor::findNewestActiveVoice()
{
    Voice* newest = nullptr;
    for (auto& v : voices)
        if (v.active && (newest == nullptr || v.order > newest->order))
            newest = &v;
    return newest;
}

int KleinBottleAudioProcessor::countActiveVoices() const
{
    int n = 0;
    for (const auto& v : voices)
        if (v.active)
            ++n;
    return n;
}

void KleinBottleAudioProcessor::startVoice (int note, float velocity, const BlockParams& bp)
{
    const bool lowNoteGliss = (note <= 57 && bp.excType <= 2);
    Voice* picked = lowNoteGliss ? findNewestActiveVoice() : nullptr;
    if (picked == nullptr)
        picked = findVoiceToSteal();

    Voice& voice = *picked;

    voice.active   = true;
    voice.note     = note;
    voice.velocity = juce::jlimit (0.0f, 1.0f, velocity);
    voice.order    = ++voiceOrder;
    voice.bowing   = (bp.excType == 3);
    voice.blowing  = (bp.excType == 4);
    voice.silenceTime = 0.0f;
    voice.dcAx = voice.dcAy = voice.dcBx = voice.dcBy = 0.0f;

    // ---- note -> grid resolution (pitch) ---------------------------------
    const double f0 = 440.0 * std::pow (2.0, (note - 69) / 12.0);
    const int targetPoly = juce::jlimit (1, kMaxVoices, countActiveVoices() + (voice.active ? 0 : 1));
    const int voiceBudget = std::max (1200, bp.nodeBudget / targetPoly);
    const auto spec = KleinMesh::specForNote (fs, f0, bp.nxMax, voiceBudget, bp.shape);
    const int nx      = spec.nx;
    const int ny      = spec.ny;
    const int fold    = spec.fold;

    // ---- allocate / retune -------------------------------------------------
    const auto topo = (kb::Topology) juce::jlimit (0, 4, bp.topology);
    if (voice.mesh.getTopology() != topo)
        voice.mesh.setTopology (topo);
    if (voice.mesh.getNx() != nx || voice.mesh.getNy() != ny)
        voice.mesh.resize (nx, ny);
    else
        voice.mesh.clear();

    voice.mesh.setSampleRate (fs);
    // Bow / wind notes keep ringing like a violin or flute: soften the viscous
    // damping so their release tail is ~3x longer than a struck note's.
    const float sustainScale = (bp.excType == 3 || bp.excType == 4) ? 0.35f : 1.0f;
    voice.mesh.setDamping (sustainScale * bp.sigma / (float) fold);   // keep T60 independent of fold
    voice.fold = fold;

    // ---- exciter placement -------------------------------------------------
    // Register tilt: folded/low voices are naturally quieter (box-average of
    // the oversampled signal + lower pickup efficiency), so lift them and trim
    // the top octaves.  Slope 3.5 dB/octave anchored at C3.
    const double compDb = juce::jlimit (-6.0, 8.0, -3.5 * std::log2 (f0 / 130.81278265));
    voice.levelComp = (float) std::pow (10.0, compDb / 20.0);

    voice.baseU = wrap01f (bp.excU + bp.keyTrack * (float) (note - 24) / 72.0f);
    voice.baseV = wrap01f (bp.excV);
    voice.pan   = juce::jlimit (-1.0f, 1.0f, (bp.pickU - 0.5f) * 1.6f * bp.spread);

    const float mu = wrap01f (voice.baseU + 0.40f * bp.motionAmt * (float) std::cos (motionPhase));
    const float mv = wrap01f (voice.baseV + 0.40f * bp.motionAmt * (float) std::sin (1.7 * motionPhase)
                              + 0.35f * modWheel.load());
    voice.liveU = mu;
    voice.liveV = mv;

    const float vel2 = voice.velocity * voice.velocity;
    const float amp  = bp.force * (0.15f + 0.85f * vel2);

    switch (bp.excType)
    {
        case 1:  voice.mesh.pluck (mu, mv, 2.2f * amp); break;
        case 2:  voice.mesh.noiseBurst (mu, mv, 2.0f * amp, bp.hard); break;
        case 3:  break;   // the bow is applied per sample in renderVoice
        case 4:  break;   // wind breath is applied per sample in renderVoice
        case 0:
        default: voice.mesh.strike (mu, mv, 3.0f * amp); break;
    }

    displayVoice = (int) (&voice - voices);
}

void KleinBottleAudioProcessor::releaseNote (int note)
{
    Voice* found = nullptr;
    for (auto& v : voices)
        if (v.active && v.note == note)
            found = &v;   // newest voice with this note wins

    if (found != nullptr)
    {
        found->bowing  = false;
        found->blowing = false;
    }
}

void KleinBottleAudioProcessor::allNotesOff (bool hard)
{
    for (auto& v : voices)
    {
        if (! v.active)
            continue;
        v.bowing  = false;
        v.blowing = false;
        if (hard)
        {
            v.active = false;
            v.mesh.clear();
        }
    }
}

void KleinBottleAudioProcessor::panic()
{
    sustainedNotes.clear();
    sustainHeld = false;
    allNotesOff (true);
    keyboardState.allNotesOff (0);
    keyboardState.allNotesOff (1);
}

//==============================================================================
void KleinBottleAudioProcessor::renderVoice (int index, juce::AudioBuffer<float>& buffer,
                                             const BlockParams& bp)
{
    Voice& v = voices[index];
    if (! v.active)
        return;

    KleinMesh& mesh      = v.mesh;
    const int numSamples = buffer.getNumSamples();
    const int meshN      = numSamples * v.fold;

    if ((int) v.meshL.size() < meshN)
    {
        v.meshL.resize ((size_t) meshN);
        v.meshR.resize ((size_t) meshN);
    }

    // live exciter position (block rate): key/velocity base + Motion orbit + mod wheel
    const float mu = wrap01f (v.baseU + 0.40f * bp.motionAmt * (float) std::cos (motionPhase));
    const float mv = wrap01f (v.baseV + 0.40f * bp.motionAmt * (float) std::sin (1.7 * motionPhase)
                              + 0.35f * modWheel.load());
    v.liveU = mu;
    v.liveV = mv;

    const float pu2      = wrap01f (bp.pickU + 0.25f * bp.spread);
    const bool  velRead  = (bp.pickMode == 1);
    const float bowPress = 0.014f * bp.force * (0.3f + 0.7f * v.velocity);
    const float windAmp  = 0.045f * bp.force * (0.3f + 0.7f * v.velocity);

    for (int s = 0; s < meshN; ++s)
    {
        if (v.bowing)
            mesh.bowSample (mu, mv, bp.bowSpeed, bowPress, bp.bowSharp);
        else if (v.blowing)
            mesh.windSample (mu, mv, windAmp);

        mesh.step();

        const float a = mesh.read (bp.pickU, bp.pickV, velRead);
        const float b = mesh.read (pu2, bp.pickV, velRead);

        const float ya = a - v.dcAx + 0.9975f * v.dcAy;   // DC blocker (a DC mode
        v.dcAx = a; v.dcAy = ya;                          //  survives on closed manifolds)
        const float yb = b - v.dcBx + 0.9975f * v.dcBy;
        v.dcBx = b; v.dcBy = yb;

        v.meshL[(size_t) s] = ya;
        v.meshR[(size_t) s] = yb;
    }

    float* outL = buffer.getWritePointer (0);
    float* outR = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : outL;

    const float invFold = 1.0f / (float) v.fold;
    const float theta   = (juce::jlimit (-1.0f, 1.0f, v.pan) + 1.0f)
                          * juce::MathConstants<float>::pi * 0.25f;
    const float gl = std::cos (theta);
    const float gr = std::sin (theta);
    float peak = 0.0f;

    const float susGain = v.bowing ? kBowMakeUp : (v.blowing ? kWindMakeUp : 1.0f);

    for (int s = 0; s < numSamples; ++s)
    {
        float sl = 0.0f, sr = 0.0f;
        const int base = s * v.fold;
        for (int f = 0; f < v.fold; ++f)      // box-average fold-down
        {
            sl += v.meshL[(size_t) (base + f)];
            sr += v.meshR[(size_t) (base + f)];
        }
        sl *= invFold;
        sr *= invFold;

        peak = std::max (peak, std::max (std::abs (sl), std::abs (sr)));
        outL[s] += sl * v.levelComp * kVoiceMakeUp * susGain * gl;
        outR[s] += sr * v.levelComp * kVoiceMakeUp * susGain * gr;
    }

    if (peak < 1.0e-5f)
    {
        v.silenceTime += (float) numSamples / (float) fs;
        if (v.silenceTime > 0.4f && ! v.bowing && ! v.blowing)
            v.active = false;
    }
    else
    {
        v.silenceTime = 0.0f;
    }
}

//==============================================================================
void KleinBottleAudioProcessor::updateSnapshot (const BlockParams& bp)
{
    const juce::ScopedLock sl (snapshotLock);

    if (displayVoice < 0 || displayVoice >= kMaxVoices)
    {
        snapshot.topo = bp.topology;   // reshape the view even while idle
        return;
    }

    Voice& v = voices[displayVoice];
    const float* field = v.mesh.currentField();
    if (field == nullptr)
        return;

    const size_t n = (size_t) v.mesh.getNx() * (size_t) v.mesh.getNy();
    snapshot.field.assign (field, field + n);
    snapshot.nx      = v.mesh.getNx();
    snapshot.ny      = v.mesh.getNy();
    // while a voice is ringing, report its own mesh topology (it was fixed at
    // note start); an idle display voice follows the parameter immediately
    snapshot.topo    = v.active ? (int) v.mesh.getTopology() : bp.topology;
    snapshot.excType = bp.excType;
    snapshot.excU    = v.liveU;
    snapshot.excV    = v.liveV;
    snapshot.pickU   = bp.pickU;
    snapshot.pickV   = bp.pickV;
    snapshot.motion  = bp.motion;
    snapshot.phase   = motionPhase;
}

void KleinBottleAudioProcessor::getDisplaySnapshot (DisplaySnapshot& out) const
{
    const juce::ScopedLock sl (snapshotLock);
    out = snapshot;
}

//==============================================================================
void KleinBottleAudioProcessor::handleNoteOn (juce::MidiKeyboardState* /*source*/,
                                              int /*midiChannel*/,
                                              int midiNoteNumber, float velocity)
{
    const juce::ScopedLock sl (pendingLock);
    pendingQueue.push_back ({ true, midiNoteNumber, velocity });
}

void KleinBottleAudioProcessor::handleNoteOff (juce::MidiKeyboardState* /*source*/,
                                               int /*midiChannel*/,
                                               int midiNoteNumber, float /*velocity*/)
{
    const juce::ScopedLock sl (pendingLock);
    pendingQueue.push_back ({ false, midiNoteNumber, 0.0f });
}

//==============================================================================
void KleinBottleAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer,
                                              juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();
    buffer.clear();
    if (numSamples <= 0)
        return;

    // ---- 1) MIDI ------------------------------------------------------------
    for (const auto metadata : midi)
    {
        auto m = metadata.getMessage();
        keyboardState.processNextMidiEvent (m);

        if (m.isController())
        {
            const int cc  = m.getControllerNumber();
            const int val = m.getControllerValue();

            if (cc == 64)                       // sustain pedal
            {
                const bool down = (val >= 64);
                if (down && ! sustainHeld)
                {
                    sustainHeld = true;
                }
                else if (! down && sustainHeld)
                {
                    sustainHeld = false;
                    for (int n : sustainedNotes)
                        releaseNote (n);
                    sustainedNotes.clear();
                }
            }
            else if (cc == 1)                   // mod wheel
            {
                modWheelTarget = val / 127.0f;
            }
            else if (cc == 120 || cc == 123)    // all notes off / reset
            {
                sustainedNotes.clear();
                sustainHeld = false;
                allNotesOff (true);
            }
        }
    }

    const BlockParams bp = readBlockParams();

    // ---- 1b) live topology switch ---------------------------------------------
    //  Re-identify every resonator (ringing or idle) the moment the parameter
    //  changes: the view and the sound reshape together, no note needed.
    if (bp.topology != lastTopology)
    {
        lastTopology = bp.topology;
        for (auto& v : voices)
            v.mesh.setTopology ((kb::Topology) bp.topology);
    }

    // ---- 2) slow modulations --------------------------------------------------
    modWheel.store (modWheel.load() + (modWheelTarget - modWheel.load()) * 0.25f);
    motionPhase += juce::MathConstants<double>::twoPi * (double) bp.motionRate
                   * (double) numSamples / fs;
    if (motionPhase > juce::MathConstants<double>::twoPi * 4096.0)
        motionPhase = std::fmod (motionPhase, juce::MathConstants<double>::twoPi);

    // ---- 3) GUI pluck ----------------------------------------------------------
    if (pluckRequested.exchange (false))
        startVoice ((int) bp.pitch, 0.95f, bp);

    // ---- 4) note events queued from the GUI keyboard ----------------------------
    {
        std::vector<PendingEvent> local;
        {
            const juce::ScopedLock sl (pendingLock);
            local.swap (pendingQueue);
        }
        for (const auto& e : local)
        {
            if (e.on)
            {
                startVoice (e.note, e.vel, bp);
            }
            else
            {
                if (sustainHeld)
                    sustainedNotes.push_back (e.note);
                else
                    releaseNote (e.note);
            }
        }
    }

    // ---- 5) render voices ---------------------------------------------------------
    for (int i = 0; i < kMaxVoices; ++i)
        renderVoice (i, buffer, bp);

    // ---- 6) master ------------------------------------------------------------------
    levelSmooth.setTargetValue (bp.levelGain);

    float* L = buffer.getWritePointer (0);
    float* R = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : L;
    for (int s = 0; s < numSamples; ++s)
    {
        const float g = levelSmooth.getNextValue();
        L[s] = std::tanh (L[s] * g * 3.0f);   // driven into the tanh: loud by
        R[s] = std::tanh (R[s] * g * 3.0f);   // design, still a safety limiter
    }

    // Refresh the GUI snapshot at ~timer rate, not at block rate: hosts that
    // chop audio into small blocks (or run high sample rates) would otherwise
    // copy the display field thousands of times per second.
    snapshotCountdown -= numSamples;
    if (snapshotCountdown <= 0)
    {
        snapshotCountdown = 2048;
        updateSnapshot (bp);
    }
}

} // namespace kb

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new kb::KleinBottleAudioProcessor();
}