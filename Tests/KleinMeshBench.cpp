/*
    Klein Bottle Experimental — per-voice CPU benchmark
    Copyright (C) 2026 CVA Labs
    SPDX-License-Identifier: AGPL-3.0-or-later
*/
/*
    Renders the exact per-voice work the plugin performs in renderVoice()
    (mesh step + exciter + 2 pickups + DC blockers + fold-down) for realistic
    notes/qualities and reports the fraction of one CPU core each voice needs.

    Run:  KleinMeshBench
*/
#include "../Source/KleinMesh.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

// minimal jlimit stand-in (keeps the bench JUCE-free like the mesh)
namespace juceTest { inline double jlimitD (double lo, double hi, double v) noexcept
                     { return v < lo ? lo : (v > hi ? hi : v); } }

namespace
{
    constexpr double kRefFs = 48000.0;

    struct Result { double secondsAudio, secondsCpu, energy; float maxAbs; };

    // mirrors readBlockParams(): budget normalised to the 48 kHz reference rate
    int adaptiveBudget (double fsIn, int baseBudget)
    {
        const double scale = juceTest::jlimitD (0.25, 2.0, kRefFs / fsIn);
        return (int) std::max (2500.0, (double) baseBudget * scale);
    }

    Result renderVoiceLike (double fsIn, double f0, int nxMax, int nodeBudget,
                            float shape, double t60, double seconds)
    {
        const auto spec = kb::KleinMesh::specForNote (fsIn, f0, nxMax, nodeBudget, shape);

        kb::KleinMesh m;
        m.setSampleRate (fsIn);
        m.resize (spec.nx, spec.ny);
        m.setDamping (kb::KleinMesh::sigmaForT60 (t60, fsIn) / (float) spec.fold);
        m.strike (0.4f, 0.42f, 3.0f * 0.85f * (0.15f + 0.85f * 0.81f));

        const int nOut  = (int) (fsIn * seconds);
        const int meshN = nOut * spec.fold;
        std::vector<float> meshSig ((size_t) meshN, 0.0f);

        const float pu2 = 0.68f + 0.25f * 0.40f;   // pickU + 0.25 * spread (as in renderVoice)
        const auto t0 = std::chrono::steady_clock::now();

        float dcx = 0.0f, dcy = 0.0f, dc2x = 0.0f, dc2y = 0.0f;
        for (int s = 0; s < meshN; ++s)
        {
            m.step();
            const float a = m.read (0.68f, 0.58f, true);
            const float b = m.read (pu2, 0.58f, true);
            const float ya = a - dcx + 0.9975f * dcy;   dcx = a;  dcy = ya;
            const float yb = b - dc2x + 0.9975f * dc2y; dc2x = b; dc2y = yb;
            meshSig[(size_t) s] = ya + yb;
        }

        const auto t1 = std::chrono::steady_clock::now();
        Result r;
        r.secondsAudio = (double) nOut / fsIn;
        r.secondsCpu   = std::chrono::duration<double> (t1 - t0).count();

        // audio-equivalence checksums (compared before/after code changes)
        double energy = 0.0;
        float maxAbs  = 0.0f;
        for (int s = nOut / 4; s < meshN; ++s)
        {
            const double x = meshSig[(size_t) s];
            energy += x * x;
            maxAbs  = std::max (maxAbs, (float) std::abs (x));
        }
        r.energy = energy;
        r.maxAbs = maxAbs;
        return r;
    }
}

int main()
{
    std::setvbuf (stdout, nullptr, _IONBF, 0);   // survive hard kills mid-run
    std::printf ("== KleinMesh per-voice CPU bench (0.4 s of audio per case) ==\n\n");
    std::printf ("%-9s %-6s %-7s %-4s %-10s %-9s %-8s %-12s %-9s\n",
                 "quality", "note", "grid", "fold", "nodes/step", "cpu sec", "% of core",
                 "energy", "maxAbs");

    struct Case { const char* q; int nxMax, budget; };
    const Case cases[] = { { "Eco", 80, 4000 }, { "Standard", 112, 10000 }, { "High", 144, 25000 } };
    const struct { int note; const char* name; } notes[] = {
        { 36, "C2" }, { 48, "C3" }, { 60, "C4" }, { 69, "A4" } };

    for (const auto& c : cases)
        for (const auto& n : notes)
        {
            const double f0 = 440.0 * std::pow (2.0, (n.note - 69) / 12.0);
            const auto spec = kb::KleinMesh::specForNote (kRefFs, f0, c.nxMax, c.budget, 0.5f);
            const auto r = renderVoiceLike (kRefFs, f0, c.nxMax, c.budget, 0.5f, 2.0, 0.4);

            std::printf ("%-9s %-6s %3dx%-3d %-4d %-10d %-9.3f %-7.0f%% %-12.6f %-9.6f\n",
                         c.q, n.name, spec.nx, spec.ny, spec.fold,
                         spec.nx * spec.ny, r.secondsCpu,
                         100.0 * r.secondsCpu / r.secondsAudio,
                         r.energy, r.maxAbs);
            std::fflush (stdout);
        }

    // ------------------------------------------------------------------
    // Sample-rate independence: the plugin normalises the node budget to
    // the 48 kHz reference, so a saturated low note must cost the same
    // fraction of a core in 44.1/48/96/192 kHz sessions.
    std::printf ("\n== host sample-rate scaling (Standard, adaptive budget) ==\n\n");
    std::printf ("%-8s %-6s %-7s %-4s %-10s %-9s %-8s\n",
                 "rate", "note", "grid", "fold", "nodes/step", "cpu sec", "% of core");

    const int rates[] = { 44100, 48000, 96000, 192000 };
    const struct { int note; const char* name; } lowNotes[] = { { 36, "C2" }, { 57, "A3" } };

    for (int rate : rates)
        for (const auto& n : lowNotes)
        {
            const double f0   = 440.0 * std::pow (2.0, (n.note - 69) / 12.0);
            const int budget  = adaptiveBudget ((double) rate, 10000);
            const auto spec   = kb::KleinMesh::specForNote ((double) rate, f0, 112, budget, 0.5f);
            const auto r      = renderVoiceLike ((double) rate, f0, 112, budget, 0.5f, 2.0, 0.25);

            std::printf ("%-8d %-6s %3dx%-3d %-4d %-10d %-9.3f %-7.0f%%\n",
                         rate, n.name, spec.nx, spec.ny, spec.fold,
                         spec.nx * spec.ny, r.secondsCpu,
                         100.0 * r.secondsCpu / r.secondsAudio);
            std::fflush (stdout);
        }

    return 0;
}
