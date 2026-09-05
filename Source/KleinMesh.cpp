/*
    Klein Bottle Experimental — physical-modelling synthesizer
    Copyright (C) 2026 CVA Labs
    SPDX-License-Identifier: AGPL-3.0-or-later
*/
#include "KleinMesh.h"
#include "KleinMesh.h"

namespace kb
{

KleinMesh::KleinMesh()
{
    resize (16, 8);
}

//==============================================================================
/*  Topology-aware addressing.

    One axis wrap may flip the *other* coordinate (that is the Klein/Mobius
    twist).  Queries are always short-ranged (neighbours, +-1), so a single
    wrap step per axis is sufficient.
*/
int KleinMesh::indexOf (int i, int j, int nx, int ny, AxisMode xm, AxisMode ym) noexcept
{
    // Wrapping axis A with mode `flip` reverses the OTHER coordinate:
    //   Klein  (y flips x):  (x, ny) ~ (nx-1-x, 0)
    //   Mobius (x flips y):  (nx, y) ~ (0, ny-1-y)
    // The neighbour relation stays reciprocal, which is what keeps the
    // waveguide mesh energy-conserving.
    auto wrap = [] (int a, int& other, int n, int otherN, AxisMode m) noexcept -> int
    {
        if (a >= 0 && a < n)
            return a;

        switch (m)
        {
            case AxisMode::clamp:
                return a < 0 ? 0 : n - 1;

            case AxisMode::periodic:
            {
                int r = a % n;
                return r < 0 ? r + n : r;
            }

            case AxisMode::flip:
            default:
            {
                int r = a % n;
                if (r < 0) r += n;
                other = otherN - 1 - other;
                return r;
            }
        }
    };

    int ii = i, jj = j;
    ii = wrap (i,  jj, nx, ny, xm);
    jj = wrap (jj, ii, ny, nx, ym);
    return jj * nx + ii;
}

float KleinMesh::sampleField (const float* field, int nx, int ny,
                              AxisMode xm, AxisMode ym, float u, float v) noexcept
{
    if (field == nullptr || nx <= 0 || ny <= 0)
        return 0.0f;

    const float x  = u * (float) nx;
    const float y  = v * (float) ny;
    const int   i0 = (int) std::floor (x);
    const int   j0 = (int) std::floor (y);
    const float fx = x - (float) i0;
    const float fy = y - (float) j0;
    const int   i1 = i0 + 1;
    const int   j1 = j0 + 1;

    const float a = field[indexOf (i0, j0, nx, ny, xm, ym)];
    const float b = field[indexOf (i1, j0, nx, ny, xm, ym)];
    const float c = field[indexOf (i0, j1, nx, ny, xm, ym)];
    const float d = field[indexOf (i1, j1, nx, ny, xm, ym)];

    return (a * (1.0f - fx) + b * fx) * (1.0f - fy)
         + (c * (1.0f - fx) + d * fx) * fy;
}

//==============================================================================
void KleinMesh::resize (int newNx, int newNy)
{
    nx = std::max (4, newNx);
    ny = std::max (4, newNy);

    const auto ax = axesFor (topo);
    xm = ax.x;
    ym = ax.y;

    bufA.assign ((size_t) nx * (size_t) ny, 0.0f);
    bufB.assign ((size_t) nx * (size_t) ny, 0.0f);
    uPrev = bufA.data();
    uCurr = bufB.data();
    stepCount = 0;

    rebuildTables();
}

void KleinMesh::clear() noexcept
{
    std::fill (bufA.begin(), bufA.end(), 0.0f);
    std::fill (bufB.begin(), bufB.end(), 0.0f);
    uPrev = bufA.data();
    uCurr = bufB.data();
    stepCount = 0;
}

void KleinMesh::setTopology (Topology t)
{
    topo = t;
    const auto ax = axesFor (t);
    xm = ax.x;
    ym = ax.y;
    rebuildTables();
}

void KleinMesh::rebuildTables()
{
    edgeNode.clear();
    edgeNb.clear();

    const bool   tiny     = (nx < 3 || ny < 3);
    const size_t expected = tiny ? (size_t) nx * (size_t) ny
                                 : (size_t) (2 * nx + 2 * ny - 4);
    edgeNode.reserve (expected);
    edgeNb.reserve (4 * expected);

    for (int j = 0; j < ny; ++j)
        for (int i = 0; i < nx; ++i)
        {
            const bool isEdge = tiny || i == 0 || i == nx - 1 || j == 0 || j == ny - 1;
            if (! isEdge)
                continue;

            edgeNode.push_back (j * nx + i);
            edgeNb.push_back (nodeIndex (i + 1, j)); // E
            edgeNb.push_back (nodeIndex (i - 1, j)); // W
            edgeNb.push_back (nodeIndex (i, j + 1)); // N
            edgeNb.push_back (nodeIndex (i, j - 1)); // S
        }
}

//==============================================================================
void KleinMesh::step()
{
    float*      up  = uPrev;
    float*      uc  = uCurr;
    float*      dst = uPrev;   // the next field overwrites the n-1 buffer
    const float s   = sigma;
    const float om  = 1.0f - sigma;

    if (nx >= 3 && ny >= 3)
    {
        for (int j = 1; j < ny - 1; ++j)
        {
            const float* ucj = uc + (size_t) j * nx;
            const float* rn  = uc + (size_t) (j + 1) * nx;
            const float* rs  = uc + (size_t) (j - 1) * nx;
            const float* upj = up + (size_t) j * nx;
            float*       oj  = dst + (size_t) j * nx;

            for (int i = 1; i < nx - 1; ++i)
            {
                const float x = 0.5f * (ucj[i - 1] + ucj[i + 1] + rn[i] + rs[i])
                                - s * ucj[i] - om * upj[i];
                oj[i] = x < -kFieldLimit ? -kFieldLimit : (x > kFieldLimit ? kFieldLimit : x);
            }
        }
    }

    for (size_t e = 0; e < edgeNode.size(); ++e)
    {
        const int   k  = edgeNode[e];
        const int*  nb = &edgeNb[4 * e];
        const float x  = 0.5f * (uc[nb[0]] + uc[nb[1]] + uc[nb[2]] + uc[nb[3]])
                         - s * uc[k] - om * up[k];
        dst[k] = x < -kFieldLimit ? -kFieldLimit : (x > kFieldLimit ? kFieldLimit : x);
    }

    // Project out the two *marginal* modes of the leapfrog scheme: the uniform
    // rigid mode (double root at z = +1) and the Nyquist checkerboard (double
    // root at z = -1, kx = ky = pi).  Both otherwise grow linearly from any
    // broadband excitation until the soft clipper saturates.  The projections
    // are orthogonal to every other mode, so the rest of the mesh still obeys
    // exactly the same wave equation.
    {
        double sumD = 0.0, sumC = 0.0;
        const int total = nx * ny;
        for (int j = 0; j < ny; ++j)
        {
            const float* row = dst + (size_t) j * nx;
            const int pj = j & 1;
            for (int i = 0; i < nx; ++i)
            {
                const float v = row[i];
                sumD += v;
                sumC += ((i + pj) & 1) ? -v : v;
            }
        }
        const float mean = (float) (sumD / (double) total);
        const float nyq  = (float) (sumC / (double) total);
        if (std::abs (mean) > 1.0e-6f || std::abs (nyq) > 1.0e-6f)
        {
            for (int j = 0; j < ny; ++j)
            {
                float* row = dst + (size_t) j * nx;
                const int pj = j & 1;
                for (int i = 0; i < nx; ++i)
                    row[i] -= mean + (((i + pj) & 1) ? -nyq : nyq);
            }
        }
    }

    if ((++stepCount & 0xFF) == 0)
    {
        if (! (std::isfinite (dst[0]) && std::isfinite (dst[(size_t) nx * ny / 2])))
        {
            clear();
            return;
        }
    }

    uPrev = uc;
    uCurr = dst;
}

//==============================================================================
float KleinMesh::read (float u, float v, bool velocity) const
{
    if (uCurr == nullptr)
        return 0.0f;

    float value = sampleField (uCurr, nx, ny, xm, ym, u, v);
    if (velocity && uPrev != nullptr)
        value -= sampleField (uPrev, nx, ny, xm, ym, u, v);
    return value;
}

//==============================================================================
/*  Raised-cosine spatial patch. Only used on triggers (not per sample). */
void KleinMesh::applyPatch (float u, float v, int radius, float amp, PatchMode mode)
{
    if (uCurr == nullptr || uPrev == nullptr)
        return;

    const int   R  = std::max (1, std::min (radius, std::min (nx, ny) - 1));
    const int   ci = (int) std::lround (u * (float) nx);
    const int   cj = (int) std::lround (v * (float) ny);

    std::vector<float> w ((size_t) (2 * R + 1) * (2 * R + 1), 0.0f);
    float wsum = 0.0f;
    size_t idx = 0;
    for (int dj = -R; dj <= R; ++dj)
        for (int di = -R; di <= R; ++di, ++idx)
        {
            const float d = std::sqrt ((float) (di * di + dj * dj)) / (float) R;
            if (d <= 1.0f)
            {
                const float x = 0.5f * (1.0f + std::cos (3.14159265358979f * d));
                w[idx]  = x;
                wsum   += x;
            }
        }

    if (wsum <= 0.0f)
        return;

    idx = 0;
    for (int dj = -R; dj <= R; ++dj)
        for (int di = -R; di <= R; ++di, ++idx)
        {
            if (w[idx] <= 0.0f)
                continue;

            const int   k      = nodeIndex (ci + di, cj + dj);
            const float weight = w[idx] / wsum;

            switch (mode)
            {
                case PatchMode::toPrev: uPrev[k] -= amp * weight; break;
                case PatchMode::toCurr: uCurr[k] += amp * weight; break;
                case PatchMode::noise:  uCurr[k] += amp * weight * (2.0f * rng01 (rng) - 1.0f); break;
            }
        }
}

void KleinMesh::strike (float u, float v, float amp)
{
    // velocity impulse: bias the u[n-1] buffer so that u[n] - u[n-1] > 0.
    // A constant minimal patch keeps the excitation spectrum (and therefore
    // the loudness) consistent across every grid size / note.
    applyPatch (u, v, 1, amp, PatchMode::toPrev);
}

void KleinMesh::pluck (float u, float v, float amp)
{
    applyPatch (u, v, 1, amp, PatchMode::toCurr);
}

void KleinMesh::noiseBurst (float u, float v, float amp, float hardness)
{
    hardness = std::min (std::max (hardness, 0.0f), 1.0f);
    const int R = std::max (1, (int) ((float) std::min (nx, ny) * (0.10f - 0.07f * hardness)));
    applyPatch (u, v, R, amp, PatchMode::noise);
}

//==============================================================================
KleinMesh::GridSpec KleinMesh::specForNote (double fs, double f0, int nxMax, int nodeBudget, float shape)
{
    GridSpec spec;
    constexpr double kRoot2 = 1.4142135623730951;

    int fold = 1;
    while (true)
    {
        const int nxT = (int) std::lround (fs / (kRoot2 * f0 * (double) fold));
        spec.nx = std::min (std::max (nxT, 12), nxMax);

        const int nyShape  = (int) std::lround ((double) spec.nx * shape);
        const int nyBudget = nodeBudget / (fold * spec.nx);
        spec.ny = std::max (4, std::min (std::max (4, nyBudget), std::max (4, nyShape)));
        spec.fold = fold;

        // The pitch can only be hit exactly when nxT fits under the cap;
        // otherwise fold up (a larger fold shrinks the grid quadratically).
        const bool pitchOk = (nxT <= nxMax && nxT >= 12);
        if ((pitchOk && fold * spec.nx * spec.ny <= nodeBudget) || fold >= 16)
            break;
        fold *= 2;
    }
    return spec;
}

void KleinMesh::bowSample (float u, float v, float bowSpeed, float pressure, float sharpness)
{
    if (uCurr == nullptr || uPrev == nullptr)
        return;

    const int ci = (int) std::lround (u * (float) nx);
    const int cj = (int) std::lround (v * (float) ny);
    const int k  = nodeIndex (ci, cj);

    /*  Discrete-time bow: kick the node ALONG its current motion while it is
        slower than the bow, and against it once it is faster.  This gives a
        negative-resistance pump with a natural limit cycle at |v| ~ bowSpeed
        (a direct position kick of tanh(kappa*(S - v)) would act as extra
        damping in the leapfrog recurrence).  */
    const float vn  = uCurr[k] - uPrev[k];          // node velocity
    const float dir = vn >= 0.0f ? 1.0f : -1.0f;
    const float av  = vn * dir;

    float f = pressure * std::tanh (sharpness * (bowSpeed - av));   // + pump / - brake
    f *= dir;
    uCurr[k] += f + pressure * 0.05f * (rng01 (rng) - 0.5f);        // dither seeds modes
}

void KleinMesh::windSample (float u, float v, float amp)
{
    if (uCurr == nullptr || uPrev == nullptr)
        return;

    /*  Continuous breath: a 2-pole low-pass filtered noise jet injected at the
        exciter node (air column driven by turbulent airflow).  The filter keeps
        a "woody" wind character instead of a white-noise hiss; the small
        -amp * uCurr feedback emulates the jet->edge pressure dependency.  */
    windLp1 += 0.18f * ((rng01 (rng) - 0.5f) - windLp1);
    windLp2 += 0.18f * (windLp1 - windLp2);

    const int ci = (int) std::lround (u * (float) nx);
    const int cj = (int) std::lround (v * (float) ny);
    const int k  = nodeIndex (ci, cj);

    uCurr[k] += amp * (windLp2 - 0.15f * uCurr[k]);
}

} // namespace kb