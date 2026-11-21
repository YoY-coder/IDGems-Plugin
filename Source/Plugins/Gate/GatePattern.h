#pragma once
#include "../../Core/JuceIncludes.h"

namespace idg::gate
{
/** Motif de gate : points (x, y) reliés par des segments courbés, sur un cycle [0, 1].
    Deux points au même x = front vertical (marche on/off nette).
    Taille fixe (pas d'allocation) : peut être copié tel quel vers le thread audio. */
struct GatePattern
{
    struct Pt { float x = 0, y = 0, curve = 0; };
    static constexpr int maxPts = 512;

    int n = 0;
    Pt pts[maxPts];

    // ------------------------------------------------------------------ lecture
    /** Courbure d'un segment : c > 0 reste en haut puis tombe vite (ou monte tard), c < 0 l'inverse. */
    static float curveInterp (float t, float c)
    {
        if (std::abs (c) < 0.001f) return t;
        const float k = c * 10.0f;
        return (std::exp (k * t) - 1.0f) / (std::exp (k) - 1.0f);
    }

    float segValue (int i, float x) const
    {
        const auto& a = pts[i]; const auto& b = pts[i + 1];
        const float span = b.x - a.x;
        if (span <= 1.0e-7f) return b.y;
        return a.y + (b.y - a.y) * curveInterp (juce::jlimit (0.0f, 1.0f, (x - a.x) / span), a.curve);
    }

    /** Valeur en x. `hint` mémorise le segment courant (lecture séquentielle rapide dans le thread audio). */
    float valueAt (float x, int& hint) const
    {
        if (n < 2) return 1.0f;
        if (x <= pts[0].x) { hint = 0; return pts[0].y; }
        if (x >= pts[n - 1].x) { hint = n - 2; return pts[n - 1].y; }
        int i = juce::jlimit (0, n - 2, hint);
        if (! (pts[i].x <= x && x < pts[i + 1].x))
        {
            if (x >= pts[i + 1].x) { while (i < n - 2 && x >= pts[i + 1].x) ++i; }
            else { while (i > 0 && x < pts[i].x) --i; }
        }
        hint = i;
        return segValue (i, x);
    }

    float valueAt (float x) const { int h = 0; return valueAt (x, h); }

    /** Limite à gauche de x (valeur juste avant x). */
    float leftValue (float x) const
    {
        for (int i = 0; i < n; ++i) if (pts[i].x >= x - 1.0e-6f && std::abs (pts[i].x - x) < 1.0e-6f) return pts[i].y;
        return valueAt (x);
    }
    /** Limite à droite de x (valeur juste après x). */
    float rightValue (float x) const
    {
        for (int i = n - 1; i >= 0; --i) if (std::abs (pts[i].x - x) < 1.0e-6f) return pts[i].y;
        return valueAt (x);
    }

    // ------------------------------------------------------------------ édition (thread interface)
    std::vector<Pt> toVector() const { return std::vector<Pt> (pts, pts + n); }

    void fromVector (std::vector<Pt> v)
    {
        std::stable_sort (v.begin(), v.end(), [] (const Pt& a, const Pt& b) { return a.x < b.x; });
        // nettoyage : 3 points au même x -> on garde le premier et le dernier ; plats redondants supprimés
        bool changed = true;
        while (changed && v.size() > 2)
        {
            changed = false;
            for (size_t i = 1; i + 1 < v.size(); ++i)
            {
                const auto& a = v[i - 1]; const auto& b = v[i]; const auto& c = v[i + 1];
                const bool sameX = std::abs (a.x - b.x) < 1.0e-6f && std::abs (b.x - c.x) < 1.0e-6f;
                const bool flat = std::abs (a.y - b.y) < 1.0e-5f && std::abs (b.y - c.y) < 1.0e-5f;
                const bool dupe = std::abs (a.x - b.x) < 1.0e-6f && std::abs (a.y - b.y) < 1.0e-5f;
                if (dupe && ! sameX) { v[i - 1].curve = b.curve; v.erase (v.begin() + (long) i); changed = true; break; }
                if (sameX || flat) { v.erase (v.begin() + (long) i); changed = true; break; }
            }
        }
        if (v.empty()) v = { { 0, 1, 0 }, { 1, 1, 0 } };
        if (v.size() == 1) v.push_back ({ 1.0f, v[0].y, 0 });
        v.front().x = 0.0f;
        v.back().x = 1.0f;
        if (v.size() > (size_t) maxPts) v.resize ((size_t) maxPts), v.back().x = 1.0f;
        n = (int) v.size();
        for (int i = 0; i < n; ++i)
        {
            pts[i] = v[(size_t) i];
            pts[i].y = juce::jlimit (0.0f, 1.0f, pts[i].y);
            pts[i].curve = juce::jlimit (-1.0f, 1.0f, pts[i].curve);
        }
    }

    /** Courbure du segment qui contient x (dernier point d'abscisse <= x). */
    float curveAt (float x) const
    {
        float c = 0.0f;
        for (int i = 0; i < n; ++i) if (pts[i].x <= x + 1.0e-6f) c = pts[i].curve;
        return c;
    }

    /** Remplace la zone [a, b] par un plat à la valeur v (mode « pas » : garde les courbes ailleurs). */
    void setRange (float a, float b, float v)
    {
        const float la = leftValue (a), rb = rightValue (b), cb = curveAt (b);
        std::vector<Pt> out;
        for (int i = 0; i < n; ++i)
            if (pts[i].x < a - 1.0e-6f) out.push_back (pts[i]);
        if (a > 1.0e-6f) out.push_back ({ a, la, 0 });
        out.push_back ({ a, v, 0 });
        out.push_back ({ b, v, 0 });
        if (b < 1.0f - 1.0e-6f) out.push_back ({ b, rb, cb });
        for (int i = 0; i < n; ++i)
            if (pts[i].x > b + 1.0e-6f) out.push_back (pts[i]);
        fromVector (out);
    }

    void invert()
    {
        auto v = toVector();
        for (auto& p : v) { p.y = 1.0f - p.y; p.curve = -p.curve; }
        fromVector (v);
    }

    /** Décale le motif de d (fraction de cycle, positif = vers la droite), avec rebouclage. */
    void shift (float d)
    {
        d = d - std::floor (d);
        if (d < 1.0e-6f || d > 1.0f - 1.0e-6f) return;
        const float cut = 1.0f - d; // le point du motif qui arrivera en 0
        const float l = leftValue (cut), r = rightValue (cut);
        std::vector<Pt> out { { 0.0f, r, curveAt (cut) } };
        for (int i = 0; i < n; ++i)
            if (pts[i].x > cut + 1.0e-6f) out.push_back ({ pts[i].x - cut, pts[i].y, pts[i].curve });
        out.push_back ({ d, pts[0].y, pts[0].curve });
        for (int i = 1; i < n; ++i)
            if (pts[i].x < cut - 1.0e-6f) out.push_back ({ pts[i].x + d, pts[i].y, pts[i].curve });
        out.push_back ({ 1.0f, l, 0 });
        fromVector (out);
    }

    // ------------------------------------------------------------------ fabrication
    /** Motif en marches à partir de niveaux par pas (0 = fermé, 1 = ouvert). */
    static GatePattern steps (const std::vector<float>& levels)
    {
        GatePattern p;
        std::vector<Pt> v;
        const int g = juce::jmax (1, (int) levels.size());
        for (int i = 0; i < g; ++i)
        {
            v.push_back ({ (float) i / g, levels[(size_t) i], 0 });
            v.push_back ({ (float) (i + 1) / g, levels[(size_t) i], 0 });
        }
        p.fromVector (v);
        return p;
    }

    static GatePattern fromBits (const char* bits)
    {
        std::vector<float> lv;
        for (const char* c = bits; *c; ++c)
        {
            if (*c == 'x' || *c == 'X') lv.push_back (1.0f);
            else if (*c == '-' || *c == '.') lv.push_back (0.0f);
            else if (*c >= '1' && *c <= '9') lv.push_back ((float) (*c - '0') / 10.0f);
        }
        return steps (lv);
    }

    static const juce::StringArray& presetNames()
    {
        static const juce::StringArray n {
            IDG_T ("Ouvert (plein)"), "Croches", "Doubles", "Doubles off", IDG_T ("Contretemps"),
            "Trance 1", "Trance 2", "Trance 3", "Triolets", "Euclide 3/8", "Euclide 5/8", "Euclide 7/16",
            "Stutter roll", "Hachoir 32", "Sidechain", "Sidechain dur", IDG_T ("Rampes tendues"), "Glitch" };
        return n;
    }

    static GatePattern preset (int idx)
    {
        auto euclid = [] (int k, int nSteps)
        {
            std::vector<float> lv ((size_t) nSteps);
            for (int i = 0; i < nSteps; ++i) lv[(size_t) i] = ((i * k) % nSteps) < k ? 1.0f : 0.0f;
            return steps (lv);
        };
        switch (idx)
        {
            case 0: return steps ({ 1.0f });
            case 1: return fromBits ("x-x-x-x-x-x-x-x-");
            case 2: return fromBits ("x-x-x-x-x-x-x-x-x-x-x-x-x-x-x-x-");
            case 3: return fromBits ("-x-x-x-x-x-x-x-x");
            case 4: return fromBits ("--x---x---x---x-");
            case 5: return fromBits ("x-xx-x-xx-x-xx-x");
            case 6: return fromBits ("xx-x-xx-x-xx-xx-");
            case 7: return fromBits ("x--x--x-x--x--x-");
            case 8: return fromBits ("x-xx-xx-xx-xx-xx-xx-xx-x");
            case 9: return euclid (3, 8);
            case 10: return euclid (5, 8);
            case 11: return euclid (7, 16);
            case 12:
            {
                // roulement qui accélère : croches, doubles, puis triples-croches sur la dernière moitié
                std::vector<float> lv (64);
                for (int i = 0; i < 64; ++i)
                    lv[(size_t) i] = (i < 16 ? (i % 8 < 4) : i < 32 ? (i % 4 < 2) : (i % 2 == 0)) ? 1.0f : 0.0f;
                return steps (lv);
            }
            case 13:
            {
                std::vector<float> lv (64);
                for (int i = 0; i < 64; ++i) lv[(size_t) i] = (i % 2 == 0) ? 1.0f : 0.0f;
                return steps (lv);
            }
            case 14: case 15:
            {
                // 4 pompes par cycle : chute franche, remontée courbée
                const float c = idx == 14 ? -0.45f : -0.8f;
                std::vector<Pt> v;
                for (int i = 0; i < 4; ++i)
                {
                    const float x0 = i * 0.25f;
                    v.push_back ({ x0, 1.0f, 0 });
                    v.push_back ({ x0, 0.0f, c });
                    v.push_back ({ x0 + (idx == 14 ? 0.2f : 0.16f), 1.0f, 0 });
                }
                v.push_back ({ 1.0f, 1.0f, 0 });
                GatePattern p; p.fromVector (v); return p;
            }
            case 16:
            {
                std::vector<Pt> v;
                for (int i = 0; i < 8; ++i)
                {
                    const float x0 = i / 8.0f;
                    v.push_back ({ x0, 0.0f, 0 });
                    v.push_back ({ x0, 1.0f, 0.7f });
                    v.push_back ({ x0 + 0.1f, 0.0f, 0 });
                }
                v.push_back ({ 1.0f, 0.0f, 0 });
                GatePattern p; p.fromVector (v); return p;
            }
            default: return fromBits ("x-xxx--x-x-xxxx-x---xx-x-xx-x-x-");
        }
    }

    // ------------------------------------------------------------------ sauvegarde
    juce::String toString() const
    {
        juce::String s;
        for (int i = 0; i < n; ++i)
            s << juce::String (pts[i].x, 5) << "," << juce::String (pts[i].y, 4) << "," << juce::String (pts[i].curve, 3) << ";";
        return s;
    }

    static GatePattern fromString (const juce::String& str)
    {
        std::vector<Pt> v;
        for (auto& item : juce::StringArray::fromTokens (str, ";", ""))
        {
            auto f = juce::StringArray::fromTokens (item, ",", "");
            if (f.size() >= 3)
                v.push_back ({ juce::jlimit (0.0f, 1.0f, f[0].getFloatValue()), juce::jlimit (0.0f, 1.0f, f[1].getFloatValue()),
                               juce::jlimit (-1.0f, 1.0f, f[2].getFloatValue()) });
        }
        if (v.size() < 2) return preset (5);
        GatePattern p; p.fromVector (v);
        return p;
    }
};
} // namespace idg::gate
