#include "Generateurs.h"
#include "Morceau.h"
#include "CarteDeGrooves.h"

namespace groovebox
{

juce::String nomDuMoteur (MoteurRythmique m)
{
    switch (m)
    {
        case MoteurRythmique::euclidien:    return "Euclidien";
        case MoteurRythmique::grooves:      return "Grooves";
        case MoteurRythmique::maison:       return "Maison";
        case MoteurRythmique::batteur:      return "Batteur";
    }

    return {};
}

juce::String nomDuStyleDeBatteur (StyleDeBatteur s)
{
    switch (s)
    {
        case StyleDeBatteur::rock:          return "Rock droit";
        case StyleDeBatteur::breakbeat:     return "Breakbeat";
        case StyleDeBatteur::jungle:        return "Jungle";
        case StyleDeBatteur::dub:           return "Dub demi-tempo";
        case StyleDeBatteur::shuffle:       return "Shuffle";
        case StyleDeBatteur::valse:         return "Valse";
        case StyleDeBatteur::groupe:        return "Groupé"_fr;
    }

    return {};
}

juce::String nomDeLaCymbale (CymbaleDeBatteur c)
{
    switch (c)
    {
        case CymbaleDeBatteur::charleston:          return "Charleston";
        case CymbaleDeBatteur::charlestonOuvert:    return "Charleston ouvert";
        case CymbaleDeBatteur::ride:                return "Ride";
        case CymbaleDeBatteur::cloche:              return "Ride et cloche";
    }

    return {};
}

juce::String Generateur::nomDuClip() const
{
    return moteur == MoteurRythmique::batteur ? nomDuStyleDeBatteur (style) : nomDuMoteur (moteur);
}

namespace
{
    /** Hasard déterministe : la même graine et les mêmes indices donnent toujours le même nombre (0 à 1). */
    float hasard (juce::uint32 graine, int a, int b, int c)
    {
        auto h = graine * 0x9E3779B1u ^ (juce::uint32) a * 0x85EBCA77u ^ (juce::uint32) b * 0xC2B2AE3Du ^ (juce::uint32) c * 0x27D4EB2Fu;
        h ^= h >> 15; h *= 0x2C1B3C6Du; h ^= h >> 12; h *= 0x297A2D39u; h ^= h >> 15;
        return (float) (h & 0xFFFFFF) / (float) 0x1000000;
    }

    int melangeU8 (int a, int b, int equilibre)
    {
        return (a * (255 - equilibre) + b * equilibre) >> 8;
    }

    const juce::StringArray nomsDesMoteurs { "euclidien", "grooves", "maison", "batteur" };
    const juce::StringArray nomsDesStyles { "rock", "breakbeat", "jungle", "dub", "shuffle", "valse", "groupe" };
    const juce::StringArray nomsDesCymbales { "charleston", "charlestonOuvert", "ride", "cloche" };

    /** Débuts de groupe, en pas de double-croche. */
    std::vector<int> debutsDeGroupe (const Mesure& m)
    {
        const int parTemps = juce::jmax (1, 16 / m.denominateur);
        std::vector<int> debuts;
        int t = 0;

        for (auto g : m.groupement)
        {
            debuts.push_back (t * parTemps);
            t += g;
        }

        return debuts;
    }
}

namespace
{
    /** Un groove de batteur sur une ou deux mesures, une ligne par instrument, un caractère par double-croche.
        Grosse caisse et caisse claire : X coup principal, Y principal doux, x secondaire, g note fantôme, r coup de cercle.
        Cymbale : X accent, x normal, h doux, o charleston ouvert, p pied de charleston. */
    struct MotifDeBatteur
    {
        juce::String grosseCaisse, caisseClaire, cymbale;
    };

    std::vector<MotifDeBatteur> motifsDuStyle (StyleDeBatteur style)
    {
        switch (style)
        {
            case StyleDeBatteur::rock:
                return { { "X.......X.x.....", "....X..g....X..g", "X.x.X.x.X.x.X.x." } };
            case StyleDeBatteur::breakbeat:
                return { { "X.x.......xx....", "....X..x.x..X..x", "X.x.x.x.X.x.x.x." },
                         { "X.x.......x.....", ".g..X..x.x..X...", "X.x.x.x.X.x.x.o." } };
            case StyleDeBatteur::jungle:
                return { { "X.X.......X.....", "....X..x.x.gX..x", "x.h.x.h.x.h.x.h." },
                         { "..X.......XX....", ".g..X..x.x..X.gx", "x.h.x.h.x.h.x.o." } };
            case StyleDeBatteur::dub:
                return { { "........X.......", "......g.r.....g.", "h.x.h.x.h.x.h.x." },
                         { "........X.....x.", "......g.r.......", "h.x.h.x.h.x.h.o." } };
            case StyleDeBatteur::shuffle:
                return { { "X.........x.", "..g...X...g.", "X.x.x.X.x.x." } };
            case StyleDeBatteur::valse:
                return { { "X...........", "..g.Y...Y.g.", "X...x.h.x.h." } };
            case StyleDeBatteur::groupe:
                break;
        }

        return {};
    }

    /** Groupé : grosse caisse et caisse claire alternent sur les débuts de groupe ; un dernier groupe impair finit par
        la caisse claire sur sa dernière unité. La cymbale marque chaque unité, accentuée en début de groupe. */
    MotifDeBatteur motifGroupe (const Mesure& m)
    {
        const int n = generateurs::pasParMesure (m);
        const int unite = juce::jmax (1, 16 / m.denominateur);
        const int pasDeCymbale = m.denominateur <= 4 ? juce::jmax (1, unite / 2) : unite;
        MotifDeBatteur motif { juce::String::repeatedString (".", n), juce::String::repeatedString (".", n), juce::String::repeatedString (".", n) };
        auto poser = [n] (juce::String& ligne, int s, juce::juce_wchar c) { if (s >= 0 && s < n) ligne = ligne.substring (0, s) + juce::String::charToString (c) + ligne.substring (s + 1); };
        // Un seul groupe (4/4, 3/4) : chaque temps compte pour un groupe
        auto groupement = m.groupement;

        if (groupement.size() <= 1)
            groupement.assign ((size_t) juce::jmax (1, m.numerateur), 1);

        std::vector<int> debuts;

        for (int i = 0, t = 0; i < (int) groupement.size(); t += groupement[(size_t) i++])
            debuts.push_back (t * unite);

        const int groupes = (int) debuts.size();

        for (int i = 0; i < groupes; ++i)
        {
            const int s = debuts[(size_t) i];
            const int taille = groupement[(size_t) i] * unite;

            if (i == 0)
                poser (motif.grosseCaisse, s, 'X');
            else if (i % 2 == 1)
            {
                poser (motif.caisseClaire, s, 'X');

                if (taille > unite)
                    poser (motif.caisseClaire, s + taille - unite, 'g');
            }
            else
                poser (motif.grosseCaisse, s, 'x');

            if (i == groupes - 1 && i % 2 == 0 && i > 0 && groupement[(size_t) i] >= 3)
                poser (motif.caisseClaire, s + taille - unite, 'X');
        }

        for (int s = 0; s < n; s += pasDeCymbale)
            poser (motif.cymbale, s, std::find (debuts.begin(), debuts.end(), s) != debuts.end() ? 'X' : 'x');

        return motif;
    }
}

namespace generateurs
{
    double pas (const Mesure& m)                { return m.denominateur / 16.0; }
    int pasParMesure (const Mesure& m)          { return juce::jmax (1, m.numerateur * 16 / m.denominateur); }
    bool carteDeGroovesDisponible (const Mesure& m)  { return m.numerateur == 4 && m.denominateur == 4; }

    std::vector<bool> euclide (int coups, int n, int rotation)
    {
        std::vector<bool> motif ((size_t) juce::jmax (0, n), false);
        coups = juce::jlimit (0, n, coups);

        if (n <= 0 || coups == 0)
            return motif;

        // Forme de Bresenham de l'algorithme de Bjorklund : le pas s sonne si (s × coups) mod n < coups
        for (int s = 0; s < n; ++s)
        {
            const int decale = ((s - rotation) % n + n) % n;
            motif[(size_t) s] = (decale * coups) % n < coups;
        }

        return motif;
    }

    std::array<int, 3> niveauGrooves (int pas32, float x, float y)
    {
        // Comme Grids : X et Y sur 0 à 255, quatre nœuds voisins mélangés
        const int xi = juce::jlimit (0, 255, juce::roundToInt (x * 255.0f));
        const int yi = juce::jlimit (0, 255, juce::roundToInt (y * 255.0f));
        const int i = xi >> 6, j = yi >> 6;
        const auto& a = carteDeGrooves::noeuds[(size_t) carteDeGrooves::carte[i][j]];
        const auto& b = carteDeGrooves::noeuds[(size_t) carteDeGrooves::carte[i + 1][j]];
        const auto& c = carteDeGrooves::noeuds[(size_t) carteDeGrooves::carte[i][j + 1]];
        const auto& d = carteDeGrooves::noeuds[(size_t) carteDeGrooves::carte[i + 1][j + 1]];
        const int ex = (xi << 2) & 0xff, ey = (yi << 2) & 0xff;
        std::array<int, 3> niveaux {};

        for (int v = 0; v < 3; ++v)
        {
            const auto o = (size_t) (v * 32 + (pas32 & 31));
            niveaux[(size_t) v] = melangeU8 (melangeU8 (a[o], b[o], ex), melangeU8 (c[o], d[o], ex), ey);
        }

        return niveaux;
    }

    std::array<int, 3> niveauMaison (const Mesure& m, int s, float x, float y)
    {
        // Neuf rythmes de référence bâtis sur le groupement : en X de posé à cassé, en Y d'aéré à chargé
        const int parTemps = juce::jmax (1, 16 / m.denominateur);
        const auto debuts = debutsDeGroupe (m);
        const bool debutDeMesure = s == 0;
        const bool debutDeGroupe = std::find (debuts.begin(), debuts.end(), s) != debuts.end();
        const bool avantUnGroupe = std::find (debuts.begin(), debuts.end(), (s + 1) % pasParMesure (m)) != debuts.end();
        const bool surLeTemps = s % parTemps == 0;
        const bool contretemps = parTemps >= 2 && s % parTemps == parTemps / 2;
        const int dernierGroupe = debuts.back();

        // Un seul groupe (4/4, 3/4...) : la caisse claire va sur les temps pairs (2 et 4)
        const bool unSeulGroupe = debuts.size() == 1;
        const bool tempsPair = surLeTemps && (s / parTemps) % 2 == 1;

        auto reference = [&] (int style, int charge) -> std::array<float, 3>
        {
            float gc = 0, cc = 0, ch = 0;

            switch (style)
            {
                case 0:     // posé : grosse caisse au début des groupes, caisse claire sur le dernier, charleston sur les temps
                    gc = debutDeMesure ? 255.0f : debutDeGroupe ? 170.0f : 0.0f;
                    cc = unSeulGroupe ? (tempsPair ? 230.0f : 0.0f) : (s == dernierGroupe && s > 0 ? 230.0f : 0.0f);
                    ch = surLeTemps ? 170.0f : 0.0f;
                    break;

                case 1:     // dansant : caisse claire sur les groupes, charleston sur les contretemps
                    gc = debutDeMesure ? 255.0f : debutDeGroupe ? 210.0f : (surLeTemps && m.denominateur <= 4 ? 190.0f : 0.0f);
                    cc = unSeulGroupe ? (tempsPair ? 220.0f : 0.0f) : (debutDeGroupe && ! debutDeMesure ? 220.0f : 0.0f);
                    ch = contretemps ? 230.0f : surLeTemps ? 110.0f : 0.0f;
                    break;

                default:    // cassé : syncopes juste avant les groupes, charleston sur toutes les double-croches
                    gc = debutDeMesure ? 255.0f : avantUnGroupe ? 160.0f : debutDeGroupe ? 110.0f : 0.0f;
                    if (unSeulGroupe)
                        cc = tempsPair ? 200.0f : (contretemps && (s / parTemps) % 2 == 0 ? 150.0f : 0.0f);
                    else
                        cc = s == dernierGroupe ? 140.0f : (s == dernierGroupe - 1 ? 150.0f : (contretemps && s > dernierGroupe ? 120.0f : 0.0f));
                    ch = contretemps ? 200.0f : surLeTemps ? 120.0f : 90.0f;
                    break;
            }

            // Charge : aéré retire des coups, chargé ajoute des notes fantômes, différentes selon le style
            if (charge == 0)
            {
                if (! debutDeMesure) gc *= style == 2 && avantUnGroupe ? 0.9f : 0.5f;   // le cassé garde sa syncope
                cc *= 0.85f;
                ch = surLeTemps && (s / parTemps) % 2 == 1 ? ch * 0.4f : (ch < 150.0f ? ch * 0.4f : ch * 0.85f);
            }
            else if (charge == 2)
            {
                const bool derniereDoubleCroche = parTemps >= 2 && s % parTemps == parTemps - 1;

                switch (style)
                {
                    case 0:  ch = juce::jmax (ch, contretemps ? 160.0f : 0.0f);
                             if (avantUnGroupe) gc = juce::jmax (gc, 150.0f);
                             break;
                    case 1:  ch = juce::jmax (ch, 130.0f) + 20.0f;
                             cc = juce::jmax (cc, contretemps ? 150.0f : 0.0f);
                             break;
                    default: cc = juce::jmax (cc, derniereDoubleCroche ? 160.0f : 0.0f);
                             gc = juce::jmax (gc, contretemps && s > pasParMesure (m) / 2 ? 160.0f : 0.0f);
                             ch = juce::jmax (ch, 110.0f);
                             break;
                }
            }

            return { gc, cc, ch };
        };

        // Mélange bilinéaire des quatre références voisines
        const float fx = juce::jlimit (0.0f, 1.0f, x) * 2.0f, fy = juce::jlimit (0.0f, 1.0f, y) * 2.0f;
        const int x0 = juce::jmin (1, (int) fx), y0 = juce::jmin (1, (int) fy);
        const float ax = fx - (float) x0, ay = fy - (float) y0;
        const auto a = reference (x0, y0), b = reference (x0 + 1, y0), c = reference (x0, y0 + 1), d = reference (x0 + 1, y0 + 1);
        std::array<int, 3> niveaux {};

        for (size_t v = 0; v < 3; ++v)
            niveaux[v] = juce::jlimit (0, 255, juce::roundToInt ((a[v] * (1 - ax) + b[v] * ax) * (1 - ay) + (c[v] * (1 - ax) + d[v] * ax) * ay));

        return niveaux;
    }

    std::vector<StyleDeBatteur> stylesPourLaMesure (const Mesure& m)
    {
        if (m.numerateur == 4 && m.denominateur == 4)
            return { StyleDeBatteur::rock, StyleDeBatteur::breakbeat, StyleDeBatteur::jungle, StyleDeBatteur::dub, StyleDeBatteur::groupe };

        if (m.numerateur == 3 && m.denominateur == 4)
            return { StyleDeBatteur::valse, StyleDeBatteur::groupe };

        if ((m.numerateur == 6 || m.numerateur == 12) && m.denominateur == 8)
            return { StyleDeBatteur::shuffle, StyleDeBatteur::groupe };

        return { StyleDeBatteur::groupe };
    }

    StyleDeBatteur styleJoue (StyleDeBatteur s, const Mesure& m)
    {
        const auto styles = stylesPourLaMesure (m);
        return std::find (styles.begin(), styles.end(), s) != styles.end() ? s : styles.front();
    }

    /** Le moteur batteur : le groove du style, allégé par l'espace, avec fantômes, crash et roulements de toms. */
    static std::vector<Note> genererBatteur (const Generateur& g, const Mesure& m, int longueurMesures)
    {
        std::vector<Note> notes;
        const int n = pasParMesure (m);
        const double p = pas (m);
        const auto style = styleJoue (g.style, m);
        auto motifs = style == StyleDeBatteur::groupe ? std::vector<MotifDeBatteur> { motifGroupe (m) } : motifsDuStyle (style);
        const auto debuts = debutsDeGroupe (m);

        struct Coup { int pas, note, velocite, voix; float importance; };

        for (int mesure = 0; mesure < longueurMesures; ++mesure)
        {
            const auto& motif = motifs[(size_t) mesure % motifs.size()];
            const int longueurDuMotif = motif.grosseCaisse.length();
            std::vector<Coup> coups;

            // Le motif, répété si la mesure est plus longue que lui (12/8 avec le motif de 6/8)
            for (int s = 0; s < n; ++s)
            {
                const int i = s % juce::jmax (1, longueurDuMotif);

                switch (motif.grosseCaisse[i])
                {
                    case 'X': coups.push_back ({ s, 36, 120, 0, 1.0f }); break;
                    case 'x': coups.push_back ({ s, 36, 98, 0, 0.5f }); break;
                    default: break;
                }

                switch (motif.caisseClaire[i])
                {
                    case 'X': coups.push_back ({ s, 38, 118, 1, 1.0f }); break;
                    case 'Y': coups.push_back ({ s, 38, 86, 1, 1.0f }); break;
                    case 'x': coups.push_back ({ s, 38, 92, 1, 0.55f }); break;
                    case 'r': coups.push_back ({ s, 37, 105, 1, 1.0f }); break;
                    case 'g':
                        if (hasard (g.graine, mesure, s, 31) < g.fantomes)
                            coups.push_back ({ s, 38, 28 + juce::roundToInt (16.0f * g.fantomes), 3, 0.15f });
                        break;
                    default: break;
                }

                const auto c = motif.cymbale[i];

                if (c == 'p')
                {
                    coups.push_back ({ s, 44, 70, 2, 0.35f });
                    continue;
                }

                if (c != 'X' && c != 'x' && c != 'h' && c != 'o')
                    continue;

                const int velocite = c == 'X' ? 105 : c == 'x' ? 82 : c == 'h' ? 62 : 90;
                const float importance = c == 'X' ? 0.7f : c == 'x' ? 0.45f : c == 'h' ? 0.3f : 0.4f;
                int note = c == 'o' ? 46 : 42;

                switch (g.cymbale)
                {
                    case CymbaleDeBatteur::charleston:          break;
                    case CymbaleDeBatteur::charlestonOuvert:    note = 46; break;
                    case CymbaleDeBatteur::ride:                note = 51; break;
                    case CymbaleDeBatteur::cloche:              note = c == 'X' ? 50 : 51; break;
                }

                coups.push_back ({ s, note, note == 46 && c != 'o' ? velocite - 15 : velocite, 2, importance });
            }

            // Roulement de toms à la fin de la mesure : la cymbale et la caisse claire se taisent pendant ce temps
            const bool roulement = g.roulement > 0 && (mesure + 1) % g.roulement == 0;

            if (roulement)
            {
                const int debut = debuts.size() > 1 ? debuts.back() : n - juce::jmax (2, n / 4);
                coups.erase (std::remove_if (coups.begin(), coups.end(), [debut] (const Coup& c) { return c.pas >= debut && c.note != 36; }), coups.end());
                static constexpr int toms[] = { 38, 45, 45, 43, 43, 41, 41, 41 };

                for (int s = debut; s < n; ++s)
                {
                    const int k = (s - debut) * 8 / juce::jmax (1, n - debut);
                    coups.push_back ({ s, toms[juce::jlimit (0, 7, k)], 82 + (s - debut) * 36 / juce::jmax (1, n - debut), 4, 0.6f });
                }
            }

            // Crash au début du clip puis toutes les « crash » mesures, et après chaque roulement ; jamais à chaque tour
            // d'un clip plus court que l'écart choisi
            const bool apresUnRoulement = g.roulement > 0 && mesure > 0 && mesure % g.roulement == 0;

            if (g.crash > 0 && longueurMesures >= g.crash && (mesure % g.crash == 0 || apresUnRoulement))
            {
                coups.erase (std::remove_if (coups.begin(), coups.end(), [] (const Coup& c) { return c.pas == 0 && c.voix == 2; }), coups.end());
                coups.push_back ({ 0, 49, 112, 2, 0.8f });
            }

            // L'espace : les coups les moins importants s'effacent d'abord ; à fond, il reste le premier temps et le contretemps
            for (auto& c : coups)
            {
                if (c.importance + 1.0e-4f < g.espace)
                    continue;

                int velocite = c.velocite;
                double debut = (mesure * n + c.pas) * p;

                if (g.humanisation > 0.0f)
                {
                    // Un batteur : vélocités qui respirent, caisse claire un peu en arrière, jamais en avance
                    velocite += juce::roundToInt ((hasard (g.graine, mesure * 64 + c.pas, c.voix, 7) - 0.5f) * (c.voix == 3 ? 12.0f : 26.0f) * g.humanisation);
                    debut += (hasard (g.graine, mesure * 64 + c.pas, c.voix, 8) * 0.06 + (c.note == 38 && c.velocite > 80 ? 0.04 : 0.0)) * g.humanisation * p;
                }

                notes.push_back ({ debut, p * 0.5, c.note, juce::jlimit (1, 127, velocite) });
            }
        }

        std::sort (notes.begin(), notes.end(), [] (const Note& a, const Note& b) { return a.debut < b.debut || (a.debut == b.debut && a.hauteur < b.hauteur); });
        return notes;
    }

    std::vector<Note> generer (const Generateur& g, const Mesure& m, int longueurMesures)
    {
        if (g.moteur == MoteurRythmique::batteur)
            return genererBatteur (g, m, longueurMesures);

        std::vector<Note> notes;
        const int n = pasParMesure (m);
        const double p = pas (m);
        const auto debuts = debutsDeGroupe (m);
        const auto moteur = g.moteur == MoteurRythmique::grooves && ! carteDeGroovesDisponible (m) ? MoteurRythmique::maison : g.moteur;

        auto ajouter = [&] (int mesure, int s, int note, int velocite, int voix)
        {
            double debut = (mesure * n + s) * p;

            if (g.humanisation > 0.0f)
            {
                velocite += juce::roundToInt ((hasard (g.graine, mesure * 64 + s, voix, 7) - 0.5f) * 40.0f * g.humanisation);
                debut += hasard (g.graine, mesure * 64 + s, voix, 8) * 0.08 * g.humanisation * p;   // jamais en avance
            }

            notes.push_back ({ debut, p * 0.5, note, juce::jlimit (1, 127, velocite) });
        };

        for (int mesure = 0; mesure < longueurMesures; ++mesure)
        {
            const bool roulement = g.roulement > 0 && (mesure + 1) % g.roulement == 0;
            const int debutDuRoulement = roulement ? n - juce::jmax (2, n / 4) : n;

            if (moteur == MoteurRythmique::euclidien)
            {
                for (int v = 0; v < Generateur::nombreDeVoixEuclidiennes; ++v)
                {
                    const auto& voix = g.voix[(size_t) v];
                    const auto motif = euclide (voix.coups, n, voix.rotation);
                    const auto accents = euclide (voix.accents, juce::jmax (1, voix.coups));
                    int rang = 0;

                    for (int s = 0; s < n; ++s)
                    {
                        if (! motif[(size_t) s])
                            continue;

                        const bool accent = accents[(size_t) (rang++ % juce::jmax (1, voix.coups))];

                        if (v == 1 && s >= debutDuRoulement)
                            continue;

                        if (voix.probabilite < 1.0f && hasard (g.graine, mesure, v, s) >= voix.probabilite)
                            continue;

                        const bool debutDeGroupe = std::find (debuts.begin(), debuts.end(), s) != debuts.end();
                        int velocite = (accent ? 118 : 88) + (debutDeGroupe ? 8 : 0);

                        if (v == 2)
                            velocite = juce::roundToInt (velocite * 0.85f);

                        ajouter (mesure, s, Generateur::notesEuclidiennes[(size_t) v], velocite, v);
                    }
                }
            }
            else
            {
                // Cartes : comme Grids, un niveau par pas, un seuil donné par la densité, du chaos tiré à chaque mesure
                for (int v = 0; v < 3; ++v)
                {
                    const int perturbation = (int) (hasard (g.graine, mesure, v, 99) * 255.0f) * (int) (g.chaos * 255.0f / 4.0f) >> 8;
                    const int seuil = 255 - juce::roundToInt (juce::jlimit (0.0f, 1.0f, g.densites[(size_t) v]) * 255.0f);

                    for (int s = 0; s < n; ++s)
                    {
                        if (v == 1 && s >= debutDuRoulement)
                            continue;

                        auto niveau = moteur == MoteurRythmique::grooves ? niveauGrooves (mesure * n + s, g.x, g.y)[(size_t) v]
                                                                         : niveauMaison (m, s, g.x, g.y)[(size_t) v];
                        niveau = niveau < 255 - perturbation ? niveau + perturbation : 255;

                        if (niveau > seuil && niveau > 0)
                            ajouter (mesure, s, Generateur::notesDesCartes[(size_t) v], niveau > 192 ? (v == 2 ? 105 : 120) : (v == 2 ? 72 : 90), v);
                    }
                }
            }

            // Roulement : caisse claire en double-croches qui monte, à la fin de la mesure
            for (int s = debutDuRoulement; s < n; ++s)
                ajouter (mesure, s, 38, 60 + (s - debutDuRoulement) * 60 / juce::jmax (1, n - debutDuRoulement), 1);
        }

        std::sort (notes.begin(), notes.end(), [] (const Note& a, const Note& b) { return a.debut < b.debut || (a.debut == b.debut && a.hauteur < b.hauteur); });
        return notes;
    }

    std::vector<Note> varier (const std::vector<Note>& depart, const Variation& v, juce::uint32 graine, const Mesure& m, int longueurMesures)
    {
        const double p = pas (m);
        const int n = pasParMesure (m) * longueurMesures;
        const double longueur = n * p;
        auto indexDuPas = [p] (const Note& note) { return (int) std::floor (note.debut / p + 1.0e-6); };
        std::vector<Note> notes;

        // Rotation et inversion (rétrograde), sur la grille des double-croches
        for (auto note : depart)
        {
            int s = indexDuPas (note);
            const double reste = note.debut - s * p;

            if (v.inversion)
                s = n - 1 - s;

            s = ((s + v.rotation) % n + n) % n;
            note.debut = s * p + reste;
            notes.push_back (note);
        }

        // Quantité : des coups glissent d'un pas, d'autres disparaissent ; jamais la grosse caisse du premier temps
        std::vector<Note> resultat;
        int k = 0;

        for (auto note : notes)
        {
            const int s = indexDuPas (note);
            const bool protege = note.hauteur == 36 && s % pasParMesure (m) == 0;
            ++k;

            if (! protege && hasard (graine, k, s, 1) < v.quantite)
            {
                if (hasard (graine, k, s, 2) < 0.5f)
                    continue;

                const int voisin = ((s + (hasard (graine, k, s, 3) < 0.5f ? -1 : 1)) % n + n) % n;
                note.debut = voisin * p;
            }

            // Densité en moins : on retire des coups faibles
            if (! protege && v.densite < 0.0f && hasard (graine, k, s, 4) < -v.densite * 0.4f)
                continue;

            resultat.push_back (note);
        }

        // Densité en plus : de nouveaux coups pour les voix déjà présentes, sur des pas libres
        if (v.densite > 0.0f && ! resultat.empty())
        {
            std::set<int> voix;

            for (auto& note : resultat)
                voix.insert (note.hauteur);

            for (auto hauteur : voix)
                for (int s = 0; s < n; ++s)
                {
                    const bool occupe = std::any_of (resultat.begin(), resultat.end(), [&] (const Note& x) { return x.hauteur == hauteur && indexDuPas (x) == s; });

                    if (! occupe && hasard (graine, hauteur, s, 5) < v.densite * 0.25f)
                        resultat.push_back ({ s * p, p * 0.5, hauteur, 80 });
                }
        }

        // Une note qui sortirait du clip, ou en double sur un même pas, est retirée
        std::sort (resultat.begin(), resultat.end(), [] (const Note& a, const Note& b) { return a.debut < b.debut || (a.debut == b.debut && a.hauteur < b.hauteur); });
        resultat.erase (std::unique (resultat.begin(), resultat.end(), [&] (const Note& a, const Note& b) { return a.hauteur == b.hauteur && indexDuPas (a) == indexDuPas (b); }), resultat.end());
        resultat.erase (std::remove_if (resultat.begin(), resultat.end(), [longueur] (const Note& x) { return x.debut >= longueur - 1.0e-9; }), resultat.end());
        return resultat;
    }
}

Generateur Generateur::parDefaut (MoteurRythmique moteur, const Mesure& m)
{
    Generateur g;
    g.moteur = moteur;
    const int n = generateurs::pasParMesure (m);
    const int groupes = (int) m.groupement.size();

    // Euclidien : grosse caisse sur les groupes, caisse claire décalée, charleston sur les contretemps
    g.voix[0] = { juce::jmax (1, m.denominateur <= 4 ? m.numerateur : groupes), 0, 1, 1.0f };
    g.voix[1] = { juce::jmax (1, groupes / 2), n / juce::jmax (2, groupes), 0, 1.0f };
    g.voix[2] = { n / 2, 1, n / 4, 1.0f };
    g.voix[3] = { 0, 0, 0, 1.0f };

    if (m.numerateur == 4 && m.denominateur == 4)
        g.voix[1] = { 2, 4, 0, 1.0f };      // caisse claire sur 2 et 4

    // Batteur : le premier style de la mesure, humanisé comme un vrai batteur
    if (moteur == MoteurRythmique::batteur)
    {
        g.style = generateurs::stylesPourLaMesure (m).front();
        g.humanisation = 0.5f;
    }

    return g;
}

juce::var Generateur::versJson() const
{
    auto o = new juce::DynamicObject();
    o->setProperty ("moteur", nomsDesMoteurs[(int) moteur]);
    o->setProperty ("graine", (juce::int64) graine);

    juce::Array<juce::var> liste;

    for (auto& v : voix)
        liste.add (juce::Array<juce::var> { v.coups, v.rotation, v.accents, v.probabilite });

    o->setProperty ("voix", liste);
    o->setProperty ("x", x);
    o->setProperty ("y", y);
    o->setProperty ("densites", juce::Array<juce::var> { densites[0], densites[1], densites[2] });
    o->setProperty ("chaos", chaos);
    o->setProperty ("style", nomsDesStyles[(int) style]);
    o->setProperty ("espace", espace);
    o->setProperty ("fantomes", fantomes);
    o->setProperty ("cymbale", nomsDesCymbales[(int) cymbale]);
    o->setProperty ("crash", crash);
    o->setProperty ("humanisation", humanisation);
    o->setProperty ("roulement", roulement);
    return juce::var (o);
}

Generateur Generateur::depuisJson (const juce::var& o)
{
    Generateur g;

    if (const auto i = nomsDesMoteurs.indexOf (o.getProperty ("moteur", {}).toString()); i >= 0)
        g.moteur = (MoteurRythmique) i;

    g.graine = (juce::uint32) (juce::int64) o.getProperty ("graine", 1);

    if (auto liste = o.getProperty ("voix", {}).getArray())
        for (int i = 0; i < std::min (nombreDeVoixEuclidiennes, liste->size()); ++i)
            if (auto a = (*liste)[i].getArray(); a != nullptr && a->size() == 4)
                g.voix[(size_t) i] = { juce::jlimit (0, 64, (int) (*a)[0]), (int) (*a)[1], juce::jlimit (0, 64, (int) (*a)[2]),
                                       juce::jlimit (0.0f, 1.0f, (float) (*a)[3]) };

    g.x = juce::jlimit (0.0f, 1.0f, (float) o.getProperty ("x", 0.5f));
    g.y = juce::jlimit (0.0f, 1.0f, (float) o.getProperty ("y", 0.5f));

    if (auto d = o.getProperty ("densites", {}).getArray(); d != nullptr && d->size() == 3)
        for (size_t i = 0; i < 3; ++i)
            g.densites[i] = juce::jlimit (0.0f, 1.0f, (float) (*d)[(int) i]);

    g.chaos = juce::jlimit (0.0f, 1.0f, (float) o.getProperty ("chaos", 0.0f));
    if (const auto i = nomsDesStyles.indexOf (o.getProperty ("style", {}).toString()); i >= 0)
        g.style = (StyleDeBatteur) i;

    g.espace = juce::jlimit (0.0f, 1.0f, (float) o.getProperty ("espace", 0.0f));
    g.fantomes = juce::jlimit (0.0f, 1.0f, (float) o.getProperty ("fantomes", 0.5f));

    if (const auto i = nomsDesCymbales.indexOf (o.getProperty ("cymbale", {}).toString()); i >= 0)
        g.cymbale = (CymbaleDeBatteur) i;

    g.crash = juce::jlimit (0, 16, (int) o.getProperty ("crash", 4));
    g.humanisation = juce::jlimit (0.0f, 1.0f, (float) o.getProperty ("humanisation", 0.0f));
    g.roulement = juce::jlimit (0, 8, (int) o.getProperty ("roulement", 0));
    return g;
}

} // namespace groovebox
