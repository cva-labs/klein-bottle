/*
    Klein Bottle Experimental — physical-modelling synthesizer
    Copyright (C) 2026 CVA Labs
    SPDX-License-Identifier: AGPL-3.0-or-later
*/
#include "KleinVisualizer.h"
#include "KleinVisualizer.h"

#if __has_include("BinaryData.h")
 #include "BinaryData.h"
 #define KLEIN_HAS_LOGO 1
#else
 #define KLEIN_HAS_LOGO 0
#endif

#include <algorithm>
#include <cmath>

namespace kb
{

KleinVisualizer::KleinVisualizer (KleinBottleAudioProcessor& p)
    : proc (p)
{
#if KLEIN_HAS_LOGO
    logo = juce::ImageCache::getFromMemory (BinaryData::logo_png, (int) BinaryData::logo_pngSize);
#endif
    buildGeometry (0);
    builtTopo = 0;
    setOpaque (true);
}

void KleinVisualizer::buildGeometry (int topoIndex)
{
    base.clear();
    base.reserve ((size_t) nu * (size_t) nv);

    for (int iy = 0; iy < nu; ++iy)          // mesh v (twist / width direction)
        for (int ix = 0; ix < nv; ++ix)      // mesh u (periodic / around direction)
            base.push_back (immersed (topoIndex,
                                      (float) ix / (float) nv,
                                      (float) iy / (float) nu));
}

KleinVisualizer::Vtx KleinVisualizer::immersed (int topoIndex, float mu, float mv) const
{
    constexpr double twoPi = juce::MathConstants<double>::twoPi;

    switch (juce::jlimit (0, 4, topoIndex))
    {
        case 1:   // torus: both axes periodic
        {
            const double th = twoPi * mu;          // big circle   (mesh u)
            const double ph = twoPi * mv;          // tube circle  (mesh v)
            constexpr double R = 1.10, r = 0.45;
            return { (float) ((R + r * std::cos (ph)) * std::cos (th)),
                     (float) ((R + r * std::cos (ph)) * std::sin (th)),
                     (float) (r * std::sin (ph)) };
        }
        case 2:   // mobius band: half twist around (mesh u), free edges (mesh v)
        {
            const double th = twoPi * mu;
            const double s  = (mv - 0.5) * 1.0;
            const double c  = 1.15 + s * std::cos (0.5 * th);
            return { (float) (c * std::cos (th)),
                     (float) (c * std::sin (th)),
                     (float) (s * std::sin (0.5 * th)) };
        }
        case 3:   // cylinder: around (mesh u) periodic, height (mesh v) open
        {
            const double th = twoPi * mu;
            constexpr double R = 1.20;
            return { (float) (R * std::cos (th)),
                     (float) (R * std::sin (th)),
                     (float) ((mv - 0.5) * 1.8) };
        }
        case 4:   // membrane: flat rectangular plate, both edges clamped
            return { (float) ((mu - 0.5) * 3.0),
                     (float) ((mv - 0.5) * 2.0),
                     0.0f };
        case 0:   // klein bottle: figure-8 immersion (periodic u, flipped v)
        default:
        {
            const double U   = twoPi * mv;         // mesh v -> immersion U (around axis)
            const double V   = twoPi * mu;         // mesh u -> immersion V (along tube)
            const double cu2 = std::cos (0.5 * U);
            const double su2 = std::sin (0.5 * U);
            const double sv  = std::sin (V);
            const double s2v = std::sin (2.0 * V);
            const double r   = 2.0 + cu2 * sv - su2 * s2v;
            return { (float) (r * std::cos (U) / 2.2),
                     (float) (r * std::sin (U) / 2.2),
                     (float) ((su2 * sv + cu2 * s2v) / 2.2) };
        }
    }
}

void KleinVisualizer::advance (double deltaSeconds)
{
    proc.getDisplaySnapshot (snap);

    if ((int) snap.topo != builtTopo)      // topology changed -> reshape the view
    {
        builtTopo = juce::jlimit (0, 4, (int) snap.topo);
        buildGeometry (builtTopo);
    }

    if (! dragging)
        yaw += (float) (0.22 * deltaSeconds);

    statusText = juce::String (snap.nx) + "x" + juce::String (snap.ny)
               + "  |  " + juce::String (snap.field.empty() ? 0 : (int) snap.field.size())
               + " nodes";
    repaint();
}

void KleinVisualizer::mouseDown (const juce::MouseEvent& e)
{
    dragging  = true;
    lastMouse = e.position;
}

void KleinVisualizer::mouseDrag (const juce::MouseEvent& e)
{
    const auto d = e.position - lastMouse;
    lastMouse = e.position;
    yaw   += d.x * 0.008f;
    pitchT = juce::jlimit (-1.45f, 1.45f, pitchT + d.y * 0.008f);
}

void KleinVisualizer::mouseUp (const juce::MouseEvent&) { dragging = false; }

void KleinVisualizer::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& wheel)
{
    zoom = juce::jlimit (0.5f, 2.2f, zoom * (1.0f + wheel.deltaY * 0.15f));
}

//==============================================================================
void KleinVisualizer::paint (juce::Graphics& g)
{
    const int W = getWidth();
    const int H = getHeight();

    juce::ColourGradient bg (juce::Colour (0xff10151f), W * 0.5f, H * 0.1f,
                             juce::Colour (0xff080b10), W * 0.5f, (float) H, false);
    g.setGradientFill (bg);
    g.fillAll();

    const bool haveField = ! snap.field.empty() && snap.nx > 0 && snap.ny > 0;
    const int  topoIdx   = juce::jlimit (0, 4, (int) snap.topo);
    const auto axes      = axesFor ((kb::Topology) topoIdx);

    const float cy = std::cos (yaw),    sy = std::sin (yaw);
    const float cp = std::cos (pitchT), sp = std::sin (pitchT);
    const float S  = std::min ((float) W, (float) H) * 0.34f * zoom;

    auto project = [W, H, S, cy, sy, cp, sp] (const Vtx& v, float& sx, float& syy, float& depth)
    {
        const float x1  = v.x * cy - v.y * sy;
        const float y1  = v.x * sy + v.y * cy;
        const float y2  = y1 * cp - v.z * sp;
        const float z2  = y1 * sp + v.z * cp;
        const float persp = 6.0f / (6.0f + z2);
        sx   = (float) W * 0.5f + S * x1 * persp;
        syy  = (float) H * 0.5f - S * y2 * persp;
        depth = z2;
    };

    auto fieldAt = [&] (int iy, int ix) -> float
    {
        if (! haveField)
            return 0.0f;
        const float mu = (float) ix / (float) nv;   // mesh u (periodic direction)
        const float mv = (float) iy / (float) nu;   // mesh v (twisted direction)
        return KleinMesh::sampleField (snap.field.data(), snap.nx, snap.ny,
                                       axes.x, axes.y, mu, mv);
    };

    auto colourFor = [] (float value, float& width)
    {
        const float t   = juce::jlimit (-1.0f, 1.0f, value * 0.85f);
        const float mag = std::abs (t);
        const float hue = t >= 0.0f ? 0.07f : 0.55f;
        width = 1.4f + 2.0f * mag;
        return juce::Colour::fromHSV (hue, 0.75f, 0.62f + 0.38f * mag, 1.0f);
    };

    // ---- project all vertices ------------------------------------------------
    const size_t nPts = (size_t) nu * (size_t) nv;
    std::vector<float> px (nPts), py (nPts), pz (nPts), pf (nPts);
    for (int iy = 0; iy < nu; ++iy)
        for (int ix = 0; ix < nv; ++ix)
        {
            const size_t idx = (size_t) iy * nv + ix;
            project (base[idx], px[idx], py[idx], pz[idx]);
            pf[idx] = fieldAt (iy, ix);
        }

    // ---- build depth-sorted wireframe segments --------------------------------
    struct Seg { float x1, y1, x2, y2, z, w; juce::Colour c; };
    std::vector<Seg> segs;
    segs.reserve (nPts + nPts / 2);

    auto addSeg = [&] (int i0, int i1)
    {
        Seg s;
        s.x1 = px[(size_t) i0]; s.y1 = py[(size_t) i0];
        s.x2 = px[(size_t) i1]; s.y2 = py[(size_t) i1];
        s.z  = 0.5f * (pz[(size_t) i0] + pz[(size_t) i1]);
        s.c  = colourFor (0.5f * (pf[(size_t) i0] + pf[(size_t) i1]), s.w);
        segs.push_back (s);
    };

    for (int iy = 0; iy < nu; ++iy)             // rings around the tube
        for (int ix = 0; ix < nv; ++ix)
            addSeg (iy * nv + ix, iy * nv + (ix + 1) % nv);

    for (int ix = 0; ix < nv; ix += 2)          // lines along the tube
        for (int iy = 0; iy < nu; ++iy)
            addSeg (iy * nv + ix, ((iy + 1) % nu) * nv + ix);

    std::sort (segs.begin(), segs.end(),
               [] (const Seg& a, const Seg& b) { return a.z > b.z; });

    for (const auto& s : segs)
    {
        g.setColour (s.c);
        g.drawLine ({ s.x1, s.y1, s.x2, s.y2 }, s.w);
    }

    // ---- exciter / pickup markers ----------------------------------------------
    auto drawMarker = [&] (float mu, float mv, juce::Colour c, bool filled)
    {
        float sx, syy, depth;
        project (immersed (topoIdx, mu, mv), sx, syy, depth);

        g.setColour (c.withAlpha (0.25f));
        g.fillEllipse (sx - 11.0f, syy - 11.0f, 22.0f, 22.0f);
        g.setColour (c);
        if (filled)
            g.fillEllipse (sx - 5.0f, syy - 5.0f, 10.0f, 10.0f);
        else
            g.drawEllipse (sx - 6.0f, syy - 6.0f, 12.0f, 12.0f, 2.0f);
    };

    drawMarker (snap.excU,  snap.excV,  juce::Colour (0xffff5533), true);
    drawMarker (snap.pickU, snap.pickV, juce::Colour (0xff33ffaa), false);

    // ---- logo & product name (top left) ------------------------------------------
    float textX = 12.0f;
#if KLEIN_HAS_LOGO
    if (! logo.isNull())
    {
        const float lh = 34.0f;
        const float lw = lh * (float) logo.getWidth() / juce::jmax (1.0f, (float) logo.getHeight());
        g.drawImage (logo, juce::Rectangle<float> (10.0f, 6.0f, lw, lh));
        textX = 10.0f + lw + 14.0f;
    }
#endif
    // Title: a touch smaller than the logo mark, with the version beside it.
    const juce::Font titleFont (juce::FontOptions (38.0f, juce::Font::bold));
    const float titleW = (float) titleFont.getStringWidth ("Klein Bottle Experimental");

    g.setColour (juce::Colour (0xffd5dbe4));
    g.setFont (titleFont);
    g.drawText ("Klein Bottle Experimental", textX, 5.0f, titleW + 10.0f, 42.0f,
                juce::Justification::centredLeft);

    g.setColour (juce::Colour (0xff9aa5b5));
    g.setFont (juce::Font (juce::FontOptions (16.0f)));
    g.drawText ("v1.0", textX + titleW + 14.0f, 5.0f, 80.0f, 42.0f,
                juce::Justification::centredLeft);

    // ---- status line (bottom) ------------------------------------------------------
    static const char* topoNames[] = { "Klein Bottle", "Torus", "Mobius Band",
                                       "Cylinder", "Membrane" };

    g.setFont (juce::Font (juce::FontOptions (12.0f)));
    g.setColour (juce::Colour (0xff8a93a2));
    const juce::String info = juce::String (topoNames[topoIdx])
                            + "   |   mesh " + juce::String (snap.nx) + "x" + juce::String (snap.ny)
                            + "   |   drag = rotate, wheel = zoom";
    g.drawText (info, 12, H - 26, W - 24, 18, juce::Justification::bottomLeft);
}

} // namespace kb