/*
    Klein Bottle Experimental — headless DSP validation suite
    Copyright (C) 2026 CVA Labs
    SPDX-License-Identifier: AGPL-3.0-or-later
*/
/*
    Headless validation suite for the Klein bottle waveguide mesh.
/*
    Headless validation suite for the Klein bottle waveguide mesh.

    Run:  KleinMeshTests   (exit code 0 = all passed)
*/
#include "../Source/KleinMesh.h"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

static int gFailures = 0;

#define CHECK(cond, msg)                                                                  \
    do                                                                                    \
    {                                                                                     \
        if (! (cond))                                                                     \
        {                                                                                 \
            ++gFailures;                                                                  \
            std::printf ("  [FAIL] %s  (line %d)\n", (msg), __LINE__);                    \
        }                                                                                 \
        else                                                                              \
        {                                                                                 \
            std::printf ("  [ok]   %s\n", (msg));                                         \
        }                                                                                 \
    } while (false)

static double rmsOf (const std::vector<float>& v)
{
    if (v.empty()) return 0.0;
    double s = 0.0;
    for (float x : v) s += (double) x * x;
    return std::sqrt (s / (double) v.size());
}

//==============================================================================
static void testTopologyTables()
{
    std::printf ("test: Klein bottle identifications\n");
    kb::KleinMesh m;
    const int nx = 8, ny = 4;
    m.resize (nx, ny);

    bool ok = true;
    for (int i = 0; i < nx; ++i)
    {
        // (x, ny) ~ (nx-1-x, 0)   and   (x, -1) ~ (nx-1-x, ny-1)  : the twist
        ok = ok && m.nodeIndex (i, ny) == m.nodeIndex (nx - 1 - i, 0);
        ok = ok && m.nodeIndex (i, -1) == m.nodeIndex (nx - 1 - i, ny - 1);
        // x axis is plain periodic
        ok = ok && m.nodeIndex (nx, i % ny) == m.nodeIndex (0, i % ny);
        ok = ok && m.nodeIndex (-1, i % ny) == m.nodeIndex (nx - 1, i % ny);
    }
    CHECK (ok, "periodic x + flipped y neighbour tables are consistent");

    // torus must NOT flip
    m.setTopology (kb::Topology::torus);
    CHECK (m.nodeIndex (3, ny) == m.nodeIndex (3, 0), "torus y wrap keeps x");
}

//==============================================================================
static void testStability()
{
    std::printf ("test: stability & bounded energy\n");
    kb::KleinMesh m;
    m.setSampleRate (48000.0);
    m.resize (64, 32);
    m.setDamping (0.0f);

    m.strike (0.30f, 0.40f, 2.5f);
    m.strike (0.70f, 0.80f, -2.0f);
    m.pluck  (0.50f, 0.10f, 1.5f);

    const size_t N = (size_t) m.getNx() * (size_t) m.getNy();
    double firstSum = -1.0, maxSum = 0.0;
    bool finite = true;

    for (int n = 0; n < 48000 * 3; ++n)
    {
        m.step();
        if (n % 480 == 0)
        {
            const float* f = m.currentField();
            double s = 0.0;
            for (size_t k = 0; k < N; ++k)
            {
                s += (double) f[k] * f[k];
                if (! std::isfinite (f[k])) finite = false;
            }
            if (firstSum < 0.0) firstSum = s;
            maxSum = std::max (maxSum, s);
        }
    }

    CHECK (finite, "field stays finite (lossless, heavy excitation)");
    CHECK (maxSum < 4.0 * (firstSum + 1.0e-9), "energy bounded (lossless mesh)");

    // with damping it must die out completely
    m.setDamping (kb::KleinMesh::sigmaForT60 (0.4, 48000.0));
    for (int n = 0; n < 48000 * 3; ++n)
        m.step();

    const float* f = m.currentField();
    double s = 0.0;
    for (size_t k = 0; k < N; ++k) s += (double) f[k] * f[k];
    CHECK (std::sqrt (s / (double) N) < 1.0e-4, "decays to silence with damping");
}

//==============================================================================
static void testDecayTime()
{
    std::printf ("test: decay time (T60)\n");
    kb::KleinMesh m;
    m.setSampleRate (48000.0);
    m.resize (64, 32);
    const double t60Target = 0.5;
    m.setDamping (kb::KleinMesh::sigmaForT60 (t60Target, 48000.0));
    m.strike (0.5f, 0.5f, 1.8f);

    std::vector<float> rmsHistory;
    std::vector<float> block;
    block.reserve (4800);
    double peakRms = 0.0;

    for (int n = 0; n < 48000 * 4; ++n)
    {
        m.step();
        block.push_back (m.currentField()[(size_t) (m.getNx() / 2) + (size_t) (m.getNy() / 2) * (size_t) m.getNx()]);
        if ((int) block.size() == 4800)
        {
            const double r = rmsOf (block);
            peakRms = std::max (peakRms, r);
            rmsHistory.push_back ((float) r);
            block.clear();
        }
    }

    double t60 = -1.0;
    for (size_t i = 0; i < rmsHistory.size(); ++i)
        if (rmsHistory[i] < peakRms / 1000.0 && t60 < 0.0)
            t60 = (double) i * 4800.0 / 48000.0;

    CHECK (t60 > t60Target * 0.5 && t60 < t60Target * 1.6,
           ("T60 measured " + std::to_string (t60) + " s (target 0.5 s)").c_str());
}

//==============================================================================
static void testPitch()
{
    std::printf ("test: mesh fundamental pitch\n");
    kb::KleinMesh m;
    m.setSampleRate (48000.0);
    m.resize (96, 48);
    m.setDamping (kb::KleinMesh::sigmaForT60 (20.0, 48000.0));
    m.strike (0.5f, 0.5f, 1.5f);

    const int N = 8192;
    std::vector<float> sig ((size_t) N);
    for (int n = 0; n < N; ++n)
    {
        m.step();
        sig[(size_t) n] = m.read (0.35f, 0.5f, false);
    }

    // Goertzel scan over 200..600 Hz
    double bestMag = 0.0;
    int bestF = 0;
    for (int f = 200; f <= 600; ++f)
    {
        const double w     = 2.0 * 3.14159265358979 * (double) f / 48000.0;
        const double coeff = 2.0 * std::cos (w);
        double s0 = 0.0, s1 = 0.0, s2 = 0.0;
        for (int n = 0; n < N; ++n)
        {
            s0 = (double) sig[(size_t) n] + coeff * s1 - s2;
            s2 = s1;
            s1 = s0;
        }
        const double mag = std::sqrt (s1 * s1 + s2 * s2 - coeff * s1 * s2);
        if (mag > bestMag) { bestMag = mag; bestF = f; }
    }

    const double expected = 48000.0 / (1.4142135623730951 * 96.0);
    CHECK (std::abs ((double) bestF - expected) < 0.06 * expected,
           ("fundamental ~" + std::to_string (expected) + " Hz, measured "
            + std::to_string (bestF) + " Hz").c_str());
}

//==============================================================================
static void testBow()
{
    std::printf ("test: bow sustain & release\n");
    kb::KleinMesh m;
    m.setSampleRate (48000.0);
    m.resize (64, 32);
    m.setDamping (kb::KleinMesh::sigmaForT60 (3.0, 48000.0));

    // give the friction pump a second to build the limit cycle
    for (int n = 0; n < 48000; ++n)
    {
        m.bowSample (0.4f, 0.5f, 0.05f, 0.01f, 50.0f);
        m.step();
    }

    double s = 0.0;
    float peak = 0.0f;
    for (int n = 0; n < 4800; ++n)
    {
        m.bowSample (0.4f, 0.5f, 0.05f, 0.01f, 50.0f);
        m.step();
        const float x = m.read (0.55f, 0.5f, true);
        s += (double) x * x;
        peak = std::max (peak, std::abs (m.read (0.55f, 0.5f, false)));
    }
    const double rmsBow = std::sqrt (s / 4800.0);
    CHECK (rmsBow > 0.0008, ("bow sustains oscillation (rms " + std::to_string (rmsBow) + ")").c_str());
    CHECK (peak < 6.0f, "bow level stays sane (no clamp runaway)");

    // T60 = 3 s -> after 3 s the level must have dropped by ~60 dB
    for (int n = 0; n < 144000; ++n)
        m.step();

    s = 0.0;
    for (int n = 0; n < 4800; ++n)
    {
        m.step();
        const float x = m.read (0.55f, 0.5f, true);
        s += (double) x * x;
    }
    const double rmsAfter = std::sqrt (s / 4800.0);
    CHECK (rmsAfter < rmsBow * 0.5, "sound decays after the bow leaves");
}

//==============================================================================
static void testWind()
{
    std::printf ("test: wind sustain & release\n");
    kb::KleinMesh m;
    m.setSampleRate (48000.0);
    m.resize (64, 32);
    m.setDamping (kb::KleinMesh::sigmaForT60 (3.0, 48000.0));

    // let the breath build the air-column oscillation
    for (int n = 0; n < 48000; ++n)
    {
        m.windSample (0.4f, 0.5f, 0.045f);
        m.step();
    }

    double s = 0.0;
    float peak = 0.0f;
    for (int n = 0; n < 4800; ++n)
    {
        m.windSample (0.4f, 0.5f, 0.045f);
        m.step();
        const float x = m.read (0.55f, 0.5f, true);
        s += (double) x * x;
        peak = std::max (peak, std::abs (m.read (0.55f, 0.5f, false)));
    }
    const double rmsWind = std::sqrt (s / 4800.0);
    CHECK (rmsWind > 0.0008, ("wind sustains oscillation (rms " + std::to_string (rmsWind) + ")").c_str());
    CHECK (peak < 6.0f, "wind level stays sane (no clamp runaway)");

    // T60 = 3 s -> after 3 s the level must have dropped by ~60 dB
    for (int n = 0; n < 144000; ++n)
        m.step();

    s = 0.0;
    for (int n = 0; n < 4800; ++n)
    {
        m.step();
        const float x = m.read (0.55f, 0.5f, true);
        s += (double) x * x;
    }
    const double rmsAfter = std::sqrt (s / 4800.0);
    CHECK (rmsAfter < rmsWind * 0.5, "sound decays after the breath stops");
}

//==============================================================================
static void testNoiseAndRead()
{
    std::printf ("test: noise burst & continuity across the twist\n");
    kb::KleinMesh m;
    m.setSampleRate (48000.0);
    m.resize (64, 32);
    m.setDamping (kb::KleinMesh::sigmaForT60 (1.5, 48000.0));
    m.noiseBurst (0.5f, 0.95f, 1.2f, 0.8f);

    bool nonZero = false;
    for (int n = 0; n < 2000; ++n)
    {
        m.step();
        if (std::abs (m.read (0.5f, 0.9f, false)) > 1.0e-3f) nonZero = true;
    }
    CHECK (nonZero, "noise burst produces signal");

    // read(u, v ~ 1) must match read(mirror(u), v ~ 0)
    double maxDiff = 0.0, maxAbs = 1.0e-9;
    for (int i = 0; i < 16; ++i)
    {
        const float u = (float) i / 16.0f;
        const float a = m.read (u, 0.996f, false);
        const float b = m.read (1.0f - u - 1.0f / 64.0f, 0.004f, false);
        maxDiff = std::max (maxDiff, (double) std::abs (a - b));
        maxAbs  = std::max (maxAbs, (double) std::abs (a));
    }
    CHECK (maxDiff < 0.35 * maxAbs,
           ("field continuous across identification (diff " + std::to_string (maxDiff)
            + " vs " + std::to_string (maxAbs) + ")").c_str());
}

//==============================================================================
/*  Regression test for the sizing bug: every note must map to a mesh whose
    fundamental actually equals the requested pitch (previously low notes were
    clamped to nxMax and silently retuned to ~f0Floor). */
static void testVoicePitchMapping()
{
    std::printf ("test: note -> grid pitch mapping\n");
    struct Q { int nxMax; int budget; };
    const Q qs[3] { { 80, 4000 }, { 112, 10000 }, { 144, 25000 } };

    bool allOk = true;
    double worst = 0.0;
    for (int q = 0; q < 3; ++q)
        for (int note = 24; note <= 96; ++note)
            for (float shape : { 0.25f, 0.5f, 1.0f })
            {
                const double f0 = 440.0 * std::pow (2.0, (note - 69) / 12.0);
                const auto spec = kb::KleinMesh::specForNote (48000.0, f0,
                                                              qs[q].nxMax, qs[q].budget, shape);
                const double achieved = 48000.0 / (1.4142135623730951 * spec.nx) / spec.fold;
                const double err = std::abs (achieved - f0) / f0;
                worst = std::max (worst, err);
                if (err > 0.03)
                    allOk = false;
            }
    CHECK (allOk, ("all notes/qualities/shapes map within 3% (worst "
                   + std::to_string (worst * 100.0) + "%)").c_str());
}

//==============================================================================
/*  Per-note loudness spread through the full voice path (strike -> mesh ->
    velocity pickup -> DC block -> fold-down).  Keeps the low register from
    quietly disappearing under the high one. */
static void testVoiceLevelSpread()
{
    std::printf ("test: per-note level spread\n");
    std::vector<double> level;

    for (int note = 24; note <= 96; note += 4)
    {
        const double f0 = 440.0 * std::pow (2.0, (note - 69) / 12.0);
        const auto spec = kb::KleinMesh::specForNote (48000.0, f0, 112, 10000, 0.5f);

        kb::KleinMesh m;
        m.setSampleRate (48000.0);
        m.resize (spec.nx, spec.ny);
        m.setDamping (kb::KleinMesh::sigmaForT60 (2.0, 48000.0) / (float) spec.fold);
        m.strike (0.4f, 0.42f, 3.0f * 0.85f * (0.15f + 0.85f * 0.81f));  // force .85, vel .9

        const int nOut  = 9600;                    // 0.2 s
        const int meshN = nOut * spec.fold;
        std::vector<float> meshSig ((size_t) meshN);
        float dcx = 0.0f, dcy = 0.0f;
        float toneState = 0.0f;
        const float toneCoeff = 0.035f + 0.965f * 0.45f * 0.45f;

        for (int s = 0; s < meshN; ++s)
        {
            m.step();
            const float a  = m.read (0.68f, 0.58f, true);
            const float ya = a - dcx + 0.9975f * dcy;
            dcx = a; dcy = ya;
            toneState += toneCoeff * (ya - toneState);
            meshSig[(size_t) s] = toneState;
        }

        std::vector<float> out ((size_t) nOut);
        const float inv = 1.0f / (float) spec.fold;
        double phase = 0.0;
        const double phaseInc = 6.283185307179586 * f0 / 48000.0;
        float tonalEnv = 0.008f;
        const float tonalDecay = (float) std::exp (-6.90775527898 / (2.0 * 48000.0));
        for (int s = 0; s < nOut; ++s)
        {
            float acc = 0.0f;
            const int base = s * spec.fold;
            for (int k = 0; k < spec.fold; ++k)
                acc += meshSig[(size_t) (base + k)];
            const float tonal = (float) std::sin (phase) * tonalEnv;
            phase += phaseInc;
            if (phase >= 6.283185307179586)
                phase -= 6.283185307179586;
            tonalEnv *= tonalDecay;

            // Mirror the processor's neutral register gain, tuned core, voice
            // make-up, equal-power centre pan and driven master limiter.
            out[(size_t) s] = std::tanh ((acc * inv + tonal) * 3.18f);
        }

        double acc = 0.0;
        for (int s = nOut / 2; s < nOut; ++s)
            acc += (double) out[(size_t) s] * out[(size_t) s];
        const double rms = std::sqrt (acc / (double) (nOut / 2));

        level.push_back (rms);
    }

    double lo = 1.0e30, hi = 0.0;
    int loNote = -1, hiNote = -1;
    for (size_t i = 0; i < level.size(); ++i)
    {
        const int note = 24 + (int) i * 4;
        std::printf ("    note %d: rms %.6f\n", note, level[i]);
        if (level[i] < lo) { lo = level[i]; loNote = note; }
        if (level[i] > hi) { hi = level[i]; hiNote = note; }
    }

    CHECK (hi < 1.5, ("absolute level sane (max rms " + std::to_string (hi) + ")").c_str());
    CHECK (hi / (lo + 1.0e-9) < 4.0, ("note-to-note spread < 12 dB (hi/lo = "
                                      + std::to_string (hi / (lo + 1.0e-9))
                                      + ", lo note " + std::to_string (loNote)
                                      + ", hi note " + std::to_string (hiNote) + ")").c_str());
}

int main()
{
    std::setvbuf (stdout, nullptr, _IONBF, 0);   // survive hard kills mid-run
    std::printf ("== KleinMesh validation suite ==\n");
    testTopologyTables();
    testStability();
    testDecayTime();
    testPitch();
    testBow();
    testWind();
    testNoiseAndRead();
    testVoicePitchMapping();
    testVoiceLevelSpread();

    if (gFailures == 0)
    {
        std::printf ("\nALL TESTS PASSED\n");
        return 0;
    }
    std::printf ("\n%d FAILURE(S)\n", gFailures);
    return 1;
}
