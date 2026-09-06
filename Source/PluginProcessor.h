/*
    Klein Bottle Experimental — physical-modelling synthesizer
    Copyright (C) 2026 CVA Labs
    SPDX-License-Identifier: AGPL-3.0-or-later
*/
#pragma once
#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

#include "KleinVoice.h"

namespace kb
{

class KleinBottleAudioProcessorEditor;

//==============================================================================
/*  KleinBottle - physical modelling instrument.

    Each voice owns a digital waveguide mesh whose boundary identification is
    the one of a Klein bottle (periodic in x, flipped in y).  Notes are mapped
    to grid resolution (pitch) and exciter position (timbre).
*/
class KleinBottleAudioProcessor : public juce::AudioProcessor,
                                  private juce::MidiKeyboardState::Listener
{
public:
    static constexpr int kMaxVoices = 4;

    KleinBottleAudioProcessor();
    ~KleinBottleAudioProcessor() override;

    //==========================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Klein Bottle Experimental"; }
    bool acceptsMidi() const override  { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 30.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    //==========================================================================
    /*  GUI interface ---------------------------------------------------------- */
    struct DisplaySnapshot
    {
        std::vector<float> field;
        int   nx = 0, ny = 0;
        int   topo = 0, excType = 0;
        float excU = 0.32f, excV = 0.50f;
        float pickU = 0.68f, pickV = 0.50f;
        float motion = 0.0f;
        double phase = 0.0;
    };

    void getDisplaySnapshot (DisplaySnapshot& out) const;
    void triggerPluck() { pluckRequested.store (true, std::memory_order_relaxed); }
    void panic();

    juce::AudioProcessorValueTreeState apvts;
    juce::MidiKeyboardState keyboardState;

private:
    //==========================================================================
    struct BlockParams
    {
        float pitch = 48.0f, shape = 0.5f, decay = 0.65f;
        int   topology = 0, quality = 1, excType = 0, pickMode = 1;
        float excU = 0.32f, excV = 0.50f, force = 0.85f, hard = 0.45f;
        float motion = 0.0f, keyTrack = 0.55f;
        float pickU = 0.68f, pickV = 0.50f, spread = 0.40f, levelDb = -12.0f;

        // derived
        float sigma = 0.0f, motionRate = 0.0f, motionAmt = 0.0f;
        float bowSpeed = 0.15f, bowSharp = 50.0f, levelGain = 0.25f;
        int   nxMax = 112, nodeBudget = 10000;
    };

    struct PendingEvent
    {
        bool  on  = false;
        int   note = 0;
        float vel = 1.0f;
    };

    void handleNoteOn (juce::MidiKeyboardState* source, int midiChannel,
                       int midiNoteNumber, float velocity) override;
    void handleNoteOff (juce::MidiKeyboardState* source, int midiChannel,
                        int midiNoteNumber, float velocity) override;

    void startVoice (int note, float velocity, const BlockParams& bp);
    void releaseNote (int note);
    void allNotesOff (bool hard);
    void renderVoice (int index, juce::AudioBuffer<float>& buffer, const BlockParams& bp);
    BlockParams readBlockParams() const;
    void updateSnapshot (const BlockParams& bp);
    Voice* findVoiceToSteal();
    Voice* findNewestActiveVoice();
    int countActiveVoices() const;
    std::atomic<float>* P (const char* id) const;

    //==========================================================================
    std::atomic<float>* pPitch = nullptr;
    std::atomic<float>* pShape = nullptr;
    std::atomic<float>* pDecay = nullptr;
    std::atomic<float>* pTopology = nullptr;
    std::atomic<float>* pQuality = nullptr;
    std::atomic<float>* pExcType = nullptr;
    std::atomic<float>* pExcU = nullptr;
    std::atomic<float>* pExcV = nullptr;
    std::atomic<float>* pForce = nullptr;
    std::atomic<float>* pHard = nullptr;
    std::atomic<float>* pMotion = nullptr;
    std::atomic<float>* pKeyTrack = nullptr;
    std::atomic<float>* pPickU = nullptr;
    std::atomic<float>* pPickV = nullptr;
    std::atomic<float>* pPickMode = nullptr;
    std::atomic<float>* pSpread = nullptr;
    std::atomic<float>* pLevel = nullptr;

    Voice    voices[kMaxVoices];
    int      displayVoice = -1;
    int      lastTopology = -1;         // topology currently loaded into the meshes
    int      snapshotCountdown = 0;     // samples left until the next GUI snapshot
    std::uint64_t voiceOrder = 0;

    // 4096-sample blocks x fold 16: largest realistic per-voice scratch buffer
    static constexpr int kMeshReserveSamples = 4096 * 16;

    double fs = 48000.0;
    std::atomic<bool>  pluckRequested { false };
    std::atomic<float> modWheel { 0.0f };
    float  modWheelTarget = 0.0f;
    bool   sustainHeld = false;
    std::vector<int> sustainedNotes;

    mutable juce::CriticalSection pendingLock;
    std::vector<PendingEvent> pendingQueue;

    double motionPhase = 0.0;

    mutable juce::CriticalSection snapshotLock;
    DisplaySnapshot snapshot;

    juce::SmoothedValue<float> levelSmooth;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KleinBottleAudioProcessor)
};

} // namespace kb