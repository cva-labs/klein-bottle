/*
    Klein Bottle Experimental — physical-modelling synthesizer
    Copyright (C) 2026 CVA Labs
    SPDX-License-Identifier: AGPL-3.0-or-later
*/
#pragma once
#pragma once

/*
    KleinMesh - a digital waveguide mesh living on a Klein bottle.

    This is a discretised 2D wave equation (rectilinear waveguide mesh / FDTD):

        u[n+1](i,j) = 0.5 * ( u[n](i-1,j) + u[n](i+1,j) + u[n](i,j-1) + u[n](i,j+1) )
                      - sigma * u[n](i,j) - (1 - sigma) * u[n-1](i,j)

    which is exactly the lossless scattering of a 4-way waveguide junction at the
    special Courant number c*dt/dx = 1/sqrt(2).  `sigma` is a velocity (viscous)
    damping coefficient; every mode then decays with |z| ~ sqrt(1 - sigma) per sample.

    The grid is a *quotient space*: each axis wraps with one of three modes,

        periodic : (x, y) ~ (x + nx, y)                       (cylinder direction)
        flip     : (x, y) ~ (x, y + ny) with x -> (nx - 1 - x)  (the twist!)
        clamp    : reflecting edge

    The Klein bottle is  periodic in x  +  flip in y:
    a wave that travels around y comes back with x reversed - it can never
    consistently have an "inside" or an "outside".  That twisted identification
    is what makes this instrument sound different from a torus/plate.

    Pure C++ (no JUCE) so it can be unit tested headlessly.
*/

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <random>
#include <vector>

namespace kb
{

enum class AxisMode : int { periodic = 0, flip = 1, clamp = 2 };
enum class Topology  : int { klein = 0, torus = 1, mobius = 2, cylinder = 3, membrane = 4 };

struct TopologyAxes { AxisMode x = AxisMode::periodic; AxisMode y = AxisMode::periodic; };

[[nodiscard]] inline TopologyAxes axesFor (Topology t) noexcept
{
    switch (t)
    {
        case Topology::torus:    return { AxisMode::periodic, AxisMode::periodic };
        case Topology::mobius:   return { AxisMode::flip,     AxisMode::clamp     };
        case Topology::cylinder: return { AxisMode::periodic, AxisMode::clamp     };
        case Topology::membrane: return { AxisMode::clamp,    AxisMode::clamp     };
        case Topology::klein:
        default:                 return { AxisMode::periodic, AxisMode::flip      };
    }
}

class KleinMesh
{
public:
    static constexpr float kFieldLimit = 8.0f;

    KleinMesh();

    void setSampleRate (double sr) noexcept { fs = (sr > 1000.0 ? sr : 48000.0); }

    /** (Re)allocate the grid and clear it. */
    void resize (int newNx, int newNy);
    void clear() noexcept;
    void setTopology (Topology t);
    void setDamping (float sigmaPerSample) noexcept
    {
        sigma = std::min (std::max (sigmaPerSample, 0.0f), 0.05f);
    }

    int      getNx() const noexcept { return nx; }
    int      getNy() const noexcept { return ny; }
    Topology getTopology() const noexcept { return topo; }

    /** Approximate fundamental of the longest axis:  fs / (sqrt(2) * nx). */
    [[nodiscard]] double fundamentalHz (int gridNx) const noexcept
    {
        return fs / (1.4142135623730951 * (double) std::max (gridNx, 4));
    }

    /* --- exciters ------------------------------------------------------- */
    void strike     (float u, float v, float amp);               // velocity impulse (mallet)
    void pluck      (float u, float v, float amp);               // displacement bump
    void noiseBurst (float u, float v, float amp, float hardness);
    void bowSample  (float u, float v, float bowSpeed, float pressure, float sharpness);
    void windSample (float u, float v, float amp);              // continuous breath (wind)

    /* --- readout & time step --------------------------------------------- */
    [[nodiscard]] float read (float u, float v, bool velocity) const;
    void step();

    /* --- topology-aware helpers (shared with GUI & tests) ----------------- */
    [[nodiscard]] static int indexOf (int i, int j, int nx, int ny,
                                      AxisMode xm, AxisMode ym) noexcept;
    [[nodiscard]] static float sampleField (const float* field, int nx, int ny,
                                            AxisMode xm, AxisMode ym,
                                            float u, float v) noexcept;
    [[nodiscard]] int nodeIndex (int i, int j) const noexcept
    {
        return indexOf (i, j, nx, ny, xm, ym);
    }
    [[nodiscard]] const float* currentField() const noexcept { return uCurr; }

    /** sigma for a desired T60 (seconds) at the given sample rate. */
    [[nodiscard]] static float sigmaForT60 (double t60Seconds, double sampleRate) noexcept
    {
        const double s = 13.8156 / (sampleRate * std::max (t60Seconds, 1.0e-3));
        return (float) std::min (s, 0.05);
    }

    /** Note -> grid sizing. Pitch exactness comes first (raise `fold` until
        fs/(sqrt(2)*nx) can hit the note), the node budget is applied second. */
    struct GridSpec { int nx = 16; int ny = 8; int fold = 1; };
    [[nodiscard]] static GridSpec specForNote (double sampleRate, double f0,
                                               int nxMax, int nodeBudget, float shape);

private:
    enum class PatchMode { toPrev, toCurr, noise };

    void rebuildTables();
    void applyPatch (float u, float v, int radius, float amp, PatchMode mode);

    double   fs    = 48000.0;
    Topology topo  = Topology::klein;
    AxisMode xm    = AxisMode::periodic;
    AxisMode ym    = AxisMode::flip;
    int      nx    = 0;
    int      ny    = 0;
    float    sigma = 0.0f;

    std::vector<float> bufA, bufB;
    float* uPrev = nullptr;   // u[n-1]
    float* uCurr = nullptr;   // u[n]

    // Boundary nodes and their pre-resolved 4 neighbour indices (E, W, N, S).
    std::vector<int> edgeNode, edgeNb;

    std::mt19937                          rng   { 0x6B6C4549u };
    std::uniform_real_distribution<float> rng01 { 0.0f, 1.0f };
    std::uint64_t stepCount = 0;
    float windLp1 = 0.0f;         // wind exciter breath-noise filter state
    float windLp2 = 0.0f;
};

} // namespace kb