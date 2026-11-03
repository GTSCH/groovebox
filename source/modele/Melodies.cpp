#include "Melodies.h"
#include "Morceau.h"

namespace groovebox
{

juce::String nomDuStyle (StyleMelodique s)
{
    switch (s)
    {
        case StyleMelodique::basseDub:       return "Basse dub";
        case StyleMelodique::basseDnb:       return "Basse drum & bass";
        case StyleMelodique::acid:           return "Ligne acid";
        case StyleMelodique::hardTechno:     return "Basse hard techno";
        case StyleMelodique::stabsMinimal:   return "Stabs minimal";
        case StyleMelodique::arpegeElectro:  return "Arpège electro"_fr;
        case StyleMelodique::nappe:          return "Nappe";
    }

    return {};
}

namespace
{
    const juce::StringArray clesDesStyles { "basseDub", "basseDnb", "acid", "hardTechno", "stabsMinimal", "arpegeElectro", "nappe" };

    float hasard (juce::uint32 graine, int a, int b)
    {
        auto h = graine * 0x9E3779B1u ^ (juce::uint32) a * 0x85EBCA77u ^ (juce::uint32) b * 0xC2B2AE3Du;
        h ^= h >> 15; h *= 0x2C1B3C6Du; h ^= h >> 12; h *= 0x297A2D39u; h ^= h >> 15;
        return (float) (h & 0xFFFFFF) / (float) 0x1000000;
    }

    /** Poids rythmique d'un pas de double-croche : début de mesure, de groupe, temps, contretemps, double-croche. */
    float poids (const Mesure& m, int s)
    {
        const int parTemps = juce::jmax (1, 16 / m.denominateur);
        int debut = 0;

        for (auto g : m.groupement)
        {
            if (s == debut * parTemps)
                return s == 0 ? 1.0f : 0.8f;

            debut += g;
        }

        if (s % parTemps == 0)                  return 0.6f;
        if (parTemps >= 2 && s % parTemps == parTemps / 2)   return 0.45f;
        return 0.25f;
    }
}

GenerateurMelodique GenerateurMelodique::parDefaut (StyleMelodique style)
{
    GenerateurMelodique g;
    g.style = style;

    switch (style)
    {
        case StyleMelodique::basseDub:      g.densite = 0.30f; g.registre = 1; g.mouvement = 0.15f; g.longueur = 0.95f; g.accents = 0.2f; g.repetition = 0.8f;  g.glisses = 0.0f;  break;
        case StyleMelodique::basseDnb:      g.densite = 0.45f; g.registre = 1; g.mouvement = 0.40f; g.longueur = 0.60f; g.accents = 0.3f; g.repetition = 0.7f;  g.glisses = 0.3f;  break;
        case StyleMelodique::acid:          g.densite = 0.75f; g.registre = 2; g.mouvement = 0.50f; g.longueur = 0.50f; g.accents = 0.5f; g.repetition = 0.9f;  g.glisses = 0.35f; break;
        case StyleMelodique::hardTechno:    g.densite = 0.80f; g.registre = 1; g.mouvement = 0.05f; g.longueur = 0.35f; g.accents = 0.4f; g.repetition = 1.0f;  g.glisses = 0.0f;  break;
        case StyleMelodique::stabsMinimal:  g.densite = 0.25f; g.registre = 3; g.mouvement = 0.20f; g.longueur = 0.20f; g.accents = 0.3f; g.repetition = 0.85f; g.glisses = 0.0f;  break;
        case StyleMelodique::arpegeElectro: g.densite = 1.00f; g.registre = 3; g.mouvement = 0.30f; g.longueur = 0.60f; g.accents = 0.3f; g.repetition = 1.0f;  g.glisses = 0.0f;  break;
        case StyleMelodique::nappe:         g.densite = 0.15f; g.registre = 3; g.mouvement = 0.30f; g.longueur = 1.00f; g.accents = 0.0f; g.repetition = 0.5f;  g.glisses = 0.0f;  break;
    }

    return g;
}

juce::var GenerateurMelodique::versJson() const
{
    auto o = new juce::DynamicObject();
    o->setProperty ("style", clesDesStyles[(int) style]);
    o->setProperty ("graine", (juce::int64) graine);
    o->setProperty ("densite", densite);
    o->setProperty ("registre", registre);
    o->setProperty ("mouvement", mouvement);
    o->setProperty ("longueur", longueur);
    o->setProperty ("accents", accents);
    o->setProperty ("repetition", repetition);
    o->setProperty ("glisses", glisses);
    return juce::var (o);
}

GenerateurMelodique GenerateurMelodique::depuisJson (const juce::var& o)
{
    GenerateurMelodique g;

    if (const auto i = clesDesStyles.indexOf (o.getProperty ("style", {}).toString()); i >= 0)
        g.style = (StyleMelodique) i;

    auto f = [&o] (const char* nom, float d) { return juce::jlimit (0.0f, 1.0f, (float) o.getProperty (nom, d)); };
    g.graine = (juce::uint32) (juce::int64) o.getProperty ("graine", 1);
    g.densite = f ("densite", g.densite);
    g.registre = juce::jlimit (0, 6, (int) o.getProperty ("registre", g.registre));
    g.mouvement = f ("mouvement", g.mouvement);
    g.longueur = f ("longueur", g.longueur);
    g.accents = f ("accents", g.accents);
    g.repetition = f ("repetition", g.repetition);
    g.glisses = f ("glisses", g.glisses);
    return g;
}

namespace melodies
{
    StyleMelodique styleDuSon (const juce::String& style, bool basse)
    {
        if (style == "Dub")             return basse ? StyleMelodique::basseDub : StyleMelodique::stabsMinimal;
        if (style == "Drum & bass")     return StyleMelodique::basseDnb;
        if (style == "Acid")            return StyleMelodique::acid;
        if (style == "Hard techno")     return basse ? StyleMelodique::hardTechno : StyleMelodique::stabsMinimal;
        if (style == "Minimal")         return StyleMelodique::stabsMinimal;
        if (style == "Electro")         return StyleMelodique::arpegeElectro;
        return StyleMelodique::nappe;
    }

    std::vector<Note> generer (const GenerateurMelodique& g, const Mesure& m, const Gamme& gamme, int longueurMesures)
    {
        std::vector<Note> notes;
        const int parMesure = generateurs::pasParMesure (m);
        const double pas = generateurs::pas (m);
        const int total = parMesure * longueurMesures;
        const int degres = gamme.nombreDeDegres();
        const int quinte = degres >= 7 ? 4 : 3;     // en pentatonique, la quinte est le 4e degré

        // Répétition : motif d'une demi-mesure, d'une mesure, de deux mesures, ou phrase sur tout le clip
        const int motif = juce::jmin (total, g.repetition > 0.85f ? juce::jmax (1, parMesure / 2)
                                           : g.repetition > 0.6f  ? parMesure
                                           : g.repetition > 0.3f  ? 2 * parMesure
                                                                  : total);

        // 1. Le rythme du motif : quels pas reçoivent une note
        std::vector<int> debuts;

        for (int s = 0; s < motif; ++s)
        {
            const int dansLaMesure = s % parMesure;
            const auto w = poids (m, dansLaMesure);
            bool note = false;

            switch (g.style)
            {
                case StyleMelodique::arpegeElectro:
                    note = g.densite >= 0.75f ? true : (dansLaMesure % 2 == 0 && hasard (g.graine, s, 1) < g.densite * 1.6f);
                    break;

                case StyleMelodique::hardTechno:
                {
                    // Basse au contretemps : jamais sur le temps, là où tape la grosse caisse
                    const int parTemps = juce::jmax (1, 16 / m.denominateur);
                    note = dansLaMesure % parTemps != 0 && hasard (g.graine, s, 1) < g.densite * (dansLaMesure % parTemps == parTemps / 2 ? 1.3f : 0.5f);
                    break;
                }

                default:
                    note = (dansLaMesure == 0 && g.densite > 0.1f) || hasard (g.graine, s, 1) < g.densite * 1.6f * w;
                    break;
            }

            if (note && g.densite > 0.0f)
                debuts.push_back (s);
        }

        // 2. Les hauteurs, en degrés de la gamme
        std::vector<int> hauteurs;
        int degre = 0;

        for (size_t k = 0; k < debuts.size(); ++k)
        {
            const int s = debuts[k];

            switch (g.style)
            {
                case StyleMelodique::basseDub:
                    // Fondamentale et quinte, l'octave de temps en temps
                    degre = poids (m, s % parMesure) >= 0.8f ? 0 : (hasard (g.graine, s, 2) < 0.6f ? quinte : (hasard (g.graine, s, 3) < 0.5f ? 0 : degres));
                    break;

                case StyleMelodique::arpegeElectro:
                {
                    // Arpège de l'accord de la fondamentale (1, 3, 5, octave), le sens choisi par le mouvement
                    static constexpr int accord[] = { 0, 2, 4, 7 };
                    const int i = (int) k % 4;
                    degre = accord[g.mouvement > 0.5f ? 3 - i : i] * degres / 7;
                    break;
                }

                case StyleMelodique::hardTechno:
                    degre = hasard (g.graine, s, 2) < g.mouvement * 2.0f ? (hasard (g.graine, s, 3) < 0.5f ? degres : quinte) : 0;
                    break;

                default:
                    if (k == 0 || poids (m, s % parMesure) >= 1.0f)
                        degre = 0;
                    else if (hasard (g.graine, s, 2) < g.mouvement)
                        degre += (hasard (g.graine, s, 3) < 0.5f ? -1 : 1) * (2 + (int) (hasard (g.graine, s, 4) * 5.0f));   // saut
                    else if (hasard (g.graine, s, 5) < 0.7f)
                        degre += hasard (g.graine, s, 6) < 0.5f ? -1 : 1;                                                    // note voisine

                    degre = juce::jlimit (-2, degres + 2, degre);    // un peu sous la fondamentale, jusqu'à l'octave et un peu plus
                    break;
            }

            hauteurs.push_back (degre);
        }

        // 3. Le motif répété sur tout le clip, notes et durées
        const bool accords = g.style == StyleMelodique::stabsMinimal || g.style == StyleMelodique::nappe;

        for (int repetition = 0; repetition * motif < total; ++repetition)
            for (size_t k = 0; k < debuts.size(); ++k)
            {
                const int s = repetition * motif + debuts[k];

                if (s >= total)
                    break;

                const int suivant = k + 1 < debuts.size() ? repetition * motif + debuts[k + 1] : (repetition + 1) * motif + (debuts.empty() ? 0 : debuts[0]);
                const double espace = (double) juce::jmin (suivant, total) - s;
                const bool glisse = g.glisses > 0.0f && hasard (g.graine, (int) k, 7) < g.glisses && k + 1 < debuts.size();

                // Glissé : la note dépasse un peu sur la suivante ; un son monophonique glisse alors de l'une à l'autre
                const double duree = glisse ? (espace + 0.25) * pas : juce::jmax (0.5, espace * (0.15 + 0.85 * g.longueur)) * pas;
                const bool accent = poids (m, s % parMesure) >= 0.6f && hasard (g.graine, (int) k, 8) < g.accents;
                const int velocite = accent ? 122 : 92;

                auto ajouter = [&] (int d)
                {
                    const int note = juce::jlimit (0, 127, gamme.note (d, g.registre));
                    notes.push_back ({ s * pas, juce::jmin (duree, total * pas - s * pas), note, velocite });
                };

                ajouter (hauteurs[k]);

                if (accords)
                {
                    ajouter (hauteurs[k] + 2);
                    ajouter (hauteurs[k] + 4);
                }
            }

        std::sort (notes.begin(), notes.end(), [] (const Note& a, const Note& b) { return a.debut < b.debut || (a.debut == b.debut && a.hauteur < b.hauteur); });
        return notes;
    }
}

} // namespace groovebox
