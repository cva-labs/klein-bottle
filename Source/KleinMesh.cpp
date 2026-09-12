/*
    Klein Bottle Experimental — physical-modelling synthesizer
    Copyright (C) 2026 CVA Labs
    SPDX-License-Identifier: AGPL-3.0-or-later
*/
#include "KleinMesh.h"

#include <array>
#include <cstring>
#if defined (__AVX2__)
 #include <immintrin.h>
#endif

namespace kb
{

namespace
{
    /*  Bit-level finiteness probe.  Unlike std::isfinite this cannot be folded
        to a constant by fast-math optimisation (/fp:fast, -ffast-math), so the
        NaN / Inf safety net of the mesh survives aggressive vectorisation.  */
    inline bool bitsFinite (float x) noexcept
    {
        static_assert (sizeof (float) == sizeof (std::uint32_t));
        std::uint32_t bits = 0;
        std::memcpy (&bits, &x, sizeof (bits));
        return (bits & 0x7F800000u) != 0x7F800000u;   // exponent != all-ones
    }

#if defined (__AVX2__)
    /*  One full row of the mesh stencil, 8 nodes per vector step.  `rn` / `rs`
        are the +y / -y neighbour sources; when the topology's y wrap flips x
        (the Klein twist) the source row is walked backwards, expressed as a
        lane-reversed vector load.  For x-periodic surfaces the two row-end
        nodes close the x wrap in plain scalar.  The marginal-mode sums are
        folded into the sweep, so no extra grid pass is needed.              */
    inline void sweepRowAVX (const float* __restrict upj, const float* __restrict ucj,
                             const float* __restrict rn, const float* __restrict rs,
                             bool revN, bool revS, bool xPeriodicEnds,
                             float* __restrict oj, int nxv, int pj,
                             float s, float om,
                             const __m256& vS, const __m256& vOm, const __m256& vHalf,
                             const __m256& vLim, const __m256& vNegLim,
                             const __m256& checker, const __m256& signFlip,
                             __m256& accD, __m256& accC, double& tailD, double& tailC)
    {
        const __m256 sgn = (((1 + pj) & 1) != 0)
                               ? _mm256_xor_ps (checker, signFlip)
                               : checker;
        const __m256i revIdx = _mm256_setr_epi32 (7, 6, 5, 4, 3, 2, 1, 0);

        const int end = nxv - 1;
        int i = 1;
        for (; i + 8 <= end; i += 8)
        {
            const __m256 rnV = revN ? _mm256_permutevar8x32_ps (
                                         _mm256_loadu_ps (rn + (nxv - 8 - i)), revIdx)
                                    : _mm256_loadu_ps (rn + i);
            const __m256 rsV = revS ? _mm256_permutevar8x32_ps (
                                         _mm256_loadu_ps (rs + (nxv - 8 - i)), revIdx)
                                    : _mm256_loadu_ps (rs + i);
            __m256 x = _mm256_mul_ps (vHalf,
                         _mm256_add_ps (
                           _mm256_add_ps (_mm256_loadu_ps (ucj + i - 1),
                                          _mm256_loadu_ps (ucj + i + 1)),
                           _mm256_add_ps (rnV, rsV)));
            x = _mm256_fnmadd_ps (vS,  _mm256_loadu_ps (ucj + i), x);
            x = _mm256_fnmadd_ps (vOm, _mm256_loadu_ps (upj + i), x);
            x = _mm256_max_ps (_mm256_min_ps (x, vLim), vNegLim);
            _mm256_storeu_ps (oj + i, x);
            accD = _mm256_add_ps (accD, x);
            accC = _mm256_add_ps (accC, _mm256_mul_ps (sgn, x));
        }
        for (; i < end; ++i)   // scalar tail (same formula)
        {
            const float n  = revN ? rn[nxv - 1 - i] : rn[i];
            const float sv = revS ? rs[nxv - 1 - i] : rs[i];
            float x = 0.5f * (ucj[i - 1] + ucj[i + 1] + n + sv)
                      - s * ucj[i] - om * upj[i];
            x = std::min (std::max (x, -KleinMesh::kFieldLimit), KleinMesh::kFieldLimit);
            oj[i] = x;
            tailD += x;
            tailC += (float) (1 - 2 * (((i + pj) & 1))) * x;
        }

        if (! xPeriodicEnds)
            return;

        // x-periodic row ends: W of i = 0 is nx-1, E of nx-1 is 0
        {
            const float n0 = revN ? rn[nxv - 1] : rn[0];
            const float s0 = revS ? rs[nxv - 1] : rs[0];
            float x = 0.5f * (ucj[nxv - 1] + ucj[1] + n0 + s0)
                      - s * ucj[0] - om * upj[0];
            x = std::min (std::max (x, -KleinMesh::kFieldLimit), KleinMesh::kFieldLimit);
            oj[0] = x;
            tailD += x;
            tailC += (float) (1 - 2 * (pj & 1)) * x;
        }
        {
            const int   i  = nxv - 1;
            const float nE = revN ? rn[0] : rn[i];
            const float sE = revS ? rs[0] : rs[i];
            float x = 0.5f * (ucj[i - 1] + ucj[0] + nE + sE)
                      - s * ucj[i] - om * upj[i];
            x = std::min (std::max (x, -KleinMesh::kFieldLimit), KleinMesh::kFieldLimit);
            oj[i] = x;
            tailD += x;
            tailC += (float) (1 - 2 * (((i + pj) & 1))) * x;
        }
    }
#endif
}

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
                // All callers are short-ranged (a in [-(n-1), 2n)), so this is
                // equivalent to a % n (sign-fixed) without the integer division.
                return a < 0 ? a + n : (a >= n ? a - n : a);
            }

            case AxisMode::flip:
            default:
            {
                const int r = a < 0 ? a + n : (a >= n ? a - n : a);
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
//==============================================================================
void KleinMesh::reserveMax (int maxNx, int maxNy)
{
    const size_t maxNodes = (size_t) std::max (4, maxNx) * (size_t) std::max (4, maxNy);
    bufA.reserve (maxNodes);
    bufB.reserve (maxNodes);
    bufC.reserve (maxNodes);

    const size_t maxEdges = (size_t) (2 * maxNx + 2 * maxNy);
    edgeNode.reserve (maxEdges);
    edgeNb.reserve (4 * maxEdges);
    edgeCk.reserve (maxEdges);
}

void KleinMesh::resize (int newNx, int newNy)
{
    nx = std::max (4, newNx);
    ny = std::max (4, newNy);

    const auto ax = axesFor (topo);
    xm = ax.x;
    ym = ax.y;

    bufA.assign ((size_t) nx * (size_t) ny, 0.0f);
    bufB.assign ((size_t) nx * (size_t) ny, 0.0f);
    bufC.assign ((size_t) nx * (size_t) ny, 0.0f);
    uPrev = bufA.data();
    uCurr = bufB.data();
    uNext = bufC.data();
    stepCount = 0;

    rebuildTables();
}

void KleinMesh::clear() noexcept
{
    std::fill (bufA.begin(), bufA.end(), 0.0f);
    std::fill (bufB.begin(), bufB.end(), 0.0f);
    std::fill (bufC.begin(), bufC.end(), 0.0f);
    uPrev = bufA.data();
    uCurr = bufB.data();
    uNext = bufC.data();
    stepCount = 0;
}

void KleinMesh::setTopology (Topology t)
{
    if (topo == t)
        return;

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
    edgeCk.clear();

    const bool   tiny     = (nx < 3 || ny < 3);
    const size_t expected = tiny ? (size_t) nx * (size_t) ny
                                 : (size_t) (2 * nx + 2 * ny - 4);
    edgeNode.reserve (expected);
    edgeNb.reserve (4 * expected);
    edgeCk.reserve (expected);

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
            edgeCk.push_back (((i + j) & 1) != 0 ? -1.0f : 1.0f);
        }
}

//==============================================================================
void KleinMesh::step()
{
    /*  Three rotating buffers: the update reads u[n] and u[n-1] and writes
        u[n+1] into the spare buffer, so the destination never aliases a
        source.  The __restrict qualified pointers let the compiler
        auto-vectorise the sweeps (the old two-buffer version, where the
        destination overwrote the u[n-1] buffer, had to assume aliasing and
        fell back to scalar code - roughly 3x slower).                     */
    float* __restrict up  = uPrev;    // u[n-1]
    float* __restrict uc  = uCurr;    // u[n]
    float* __restrict dst = uNext;    // u[n+1]
    const float s   = sigma;
    const float om  = 1.0f - sigma;

    // Loop bounds are copied to locals: through the member pointers the stores
    // below could otherwise alias the member variables nx / ny, which forces
    // the vectoriser into scalar fallbacks.
    const int nxv = nx;
    const int nyv = ny;

    // The marginal-mode projections below are measured while the new field is
    // written, so no separate full-grid measurement sweep is needed.
    double sumD = 0.0, sumC = 0.0;
    bool interiorDone = false;
    bool edgesDone = false;

#if defined (__AVX2__)
    if (nxv >= 3 && nyv >= 3)
    {
        const __m256 vS      = _mm256_set1_ps (s);
        const __m256 vOm     = _mm256_set1_ps (om);
        const __m256 vHalf   = _mm256_set1_ps (0.5f);
        const __m256 vLim    = _mm256_set1_ps (kFieldLimit);
        const __m256 vNegLim = _mm256_sub_ps (_mm256_setzero_ps(), vLim);
        const __m256 signFlip = _mm256_set1_ps (-0.0f);
        const __m256 checker  = _mm256_setr_ps (1.0f, -1.0f, 1.0f, -1.0f,
                                                1.0f, -1.0f, 1.0f, -1.0f);
        __m256 accD = _mm256_setzero_ps();
        __m256 accC = _mm256_setzero_ps();
        double tailD = 0.0, tailC = 0.0;

        auto hsum = [] (__m256 v) -> double
        {
            __m128 lo = _mm256_castps256_ps128 (v);
            lo = _mm_add_ps (lo, _mm256_extractf128_ps (v, 1));
            lo = _mm_add_ps (lo, _mm_movehl_ps (lo, lo));
            lo = _mm_add_ss (lo, _mm_shuffle_ps (lo, lo, 1));
            return (double) _mm_cvtss_f32 (lo);
        };

        if (xm == AxisMode::periodic)
        {
            /*  x-periodic surfaces (Klein / Torus / Cylinder): every row is a
                1-D periodic stencil, so ALL nodes - the y-boundary rows and
                the row-end columns included - go through the vectorised sweep.
                Degenerate low-note grids (ny = 4 in high-rate sessions) stay
                fully vectorised instead of falling into the gather tables.  */
            for (int j = 0; j < nyv; ++j)
            {
                const size_t row = (size_t) j * nxv;
                const float* ucj = uc + row;
                const float* upj = up + row;
                float*       oj  = dst + row;

                const float* rn;
                const float* rs;
                bool revN = false, revS = false;

                if (j == 0)
                {
                    rn = uc + nxv;                            // row 1
                    if (ym == AxisMode::flip)       { rs = uc + (size_t) (nyv - 1) * nxv; revS = true; }
                    else if (ym == AxisMode::clamp) { rs = ucj; }         // reflecting boundary
                    else                            { rs = uc + (size_t) (nyv - 1) * nxv; }
                }
                else if (j == nyv - 1)
                {
                    rs = uc + (size_t) (nyv - 2) * nxv;       // row ny-2
                    if (ym == AxisMode::flip)       { rn = uc; revN = true; }
                    else if (ym == AxisMode::clamp) { rn = ucj; }         // reflecting boundary
                    else                            { rn = uc; }
                }
                else
                {
                    rn = uc + (size_t) (j + 1) * nxv;
                    rs = uc + (size_t) (j - 1) * nxv;
                }

                sweepRowAVX (upj, ucj, rn, rs, revN, revS, true, oj, nxv, j & 1,
                             s, om, vS, vOm, vHalf, vLim, vNegLim, checker, signFlip,
                             accD, accC, tailD, tailC);
            }
            interiorDone = true;   // every node of every row was swept above
            edgesDone = true;
        }
        else
        {
            /*  Mobius / Membrane flip or clamp along x: the interior has no
                wrap involvement and still sweeps vectorised; the wrap-affected
                columns and y-boundary rows fall through to the edge tables.  */
            for (int j = 1; j < nyv - 1; ++j)
            {
                const size_t row = (size_t) j * nxv;
                sweepRowAVX (up + row, uc + row,
                             uc + (size_t) (j + 1) * nxv, uc + (size_t) (j - 1) * nxv,
                             false, false, false, dst + row, nxv, j & 1,
                             s, om, vS, vOm, vHalf, vLim, vNegLim, checker, signFlip,
                             accD, accC, tailD, tailC);
            }
            interiorDone = true;
        }

        sumD = hsum (accD) + tailD;
        sumC = hsum (accC) + tailC;
    }
#endif
    if (! interiorDone && nxv >= 3 && nyv >= 3)
    {
        const int nxm1 = nxv - 1;
        for (int j = 1; j < nyv - 1; ++j)
        {
            const float* __restrict ucj = uc + (size_t) j * nxv;
            const float* __restrict rn  = uc + (size_t) (j + 1) * nxv;
            const float* __restrict rs  = uc + (size_t) (j - 1) * nxv;
            const float* __restrict upj = up + (size_t) j * nxv;
            float*       __restrict oj  = dst + (size_t) j * nxv;
            const int    pj = j & 1;

            for (int i = 1; i < nxm1; ++i)
            {
                float x = 0.5f * (ucj[i - 1] + ucj[i + 1] + rn[i] + rs[i])
                          - s * ucj[i] - om * upj[i];
                x = std::min (std::max (x, -kFieldLimit), kFieldLimit);
                oj[i] = x;
                sumD += x;
                // checkerboard sign, branchless (ternaries block auto-vectorisation)
                sumC += (float) (1 - 2 * (((i + pj) & 1))) * x;
            }
        }
    }

    if (! edgesDone)
    {
        const size_t edgeCount = edgeNode.size();
        const int*   __restrict edgeIdx = edgeNode.data();
        const int*   __restrict edgeNb4 = edgeNb.data();
        const float* __restrict edgeCk4 = edgeCk.data();
        for (size_t e = 0; e < edgeCount; ++e)
        {
            const int k  = edgeIdx[e];
            const int* nb = edgeNb4 + 4 * e;
            float x = 0.5f * (uc[nb[0]] + uc[nb[1]] + uc[nb[2]] + uc[nb[3]])
                      - s * uc[k] - om * up[k];
            x = std::min (std::max (x, -kFieldLimit), kFieldLimit);
            dst[k] = x;
            sumD += x;
            sumC += edgeCk4[e] * x;   // sign pre-computed in rebuildTables()
        }
    }

    // Project out the two *marginal* modes of the leapfrog scheme: the uniform
    // rigid mode (double root at z = +1) and the Nyquist checkerboard (double
    // root at z = -1, kx = ky = pi).  Both otherwise grow linearly from any
    // broadband excitation until the soft clipper saturates.  The projections
    // are orthogonal to every other mode, so the rest of the mesh still obeys
    // exactly the same wave equation.
    {
        const float mean = (float) (sumD / (double) (nxv * nyv));
        const float nyq  = (float) (sumC / (double) (nxv * nyv));
        if (std::abs (mean) > 1.0e-6f || std::abs (nyq) > 1.0e-6f)
        {
            for (int j = 0; j < nyv; ++j)
            {
                const int pj = j & 1;
                float* __restrict row = dst + (size_t) j * nxv;
                // (i + pj) even -> (mean + nyq), odd -> (mean - nyq), written
                // branchless so the sweep stays vectorisable.
                for (int i = 0; i < nxv; ++i)
                    row[i] -= mean + nyq * (float) (1 - 2 * (((i + pj) & 1)));
            }
        }
    }

    if ((++stepCount & 0xFF) == 0)
    {
        if (! (bitsFinite (dst[0]) && bitsFinite (dst[(size_t) nxv * nyv / 2])))
        {
            clear();
            return;
        }
    }

    // rotate: (prev, curr, next) <- (curr, next, prev)
    uPrev = uc;
    uCurr = dst;
    uNext = up;
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

    constexpr int maxRadius = 14;
    const int   R  = std::max (1, std::min ({ radius, std::min (nx, ny) - 1, maxRadius }));
    const int   ci = (int) std::lround (u * (float) nx);
    const int   cj = (int) std::lround (v * (float) ny);

    // The largest supported mesh is 144x144 and the softest noise patch uses
    // at most radius 14. Keep this scratch storage on the stack: exciters are
    // triggered from the audio callback and must never allocate from the heap.
    constexpr int maxWidth  = 2 * maxRadius + 1;
    std::array<float, maxWidth * maxWidth> w {};
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

    int  fold      = 1;
    bool pitchBent = false;
    while (true)
    {
        const int nxT = (int) std::lround (fs / (kRoot2 * f0 * (double) fold));
        spec.nx = std::min (std::max (nxT, 12), nxMax);
        pitchBent = (nxT > nxMax);   // the cap forced a sharper grid: pitch bends

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

    //  The fold cap was reached while the pitch was already bent (the grid cap
    //  could never fit nxT, typical for very low notes in high-rate sessions):
    //  the ny = 4 floor would then blow past the node budget (e.g. 16x112x4 at
    //  192 kHz = 3x the budget).  Shrink the grid further - the pitch is
    //  approximate here anyway - so the CPU cost stays bounded.
    if (pitchBent && fold * spec.nx * spec.ny > nodeBudget)
    {
        spec.nx = std::max (12, nodeBudget / (fold * 4));
        spec.ny = 4;
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
