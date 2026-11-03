#include "Demo.h"

namespace groovebox
{

namespace
{
    using namespace notesBatterie;

    void ajouter (Clip& c, std::initializer_list<double> debuts, double duree, int hauteur, int velocite = 100)
    {
        for (auto d : debuts)
            c.notes.push_back ({ d, duree, hauteur, velocite });
    }

    Clip batterie (bool complete)
    {
        Clip c { complete ? "Groove" : "Intro", 1, {} };
        ajouter (c, { 0.0, 1.0, 2.0, 3.0 }, 0.25, grosseCaisse, 115);

        if (complete)
            ajouter (c, { 1.0, 3.0 }, 0.25, caisseClaire, 105);

        for (int i = 0; i < 8; ++i)
            ajouter (c, { i * 0.5 }, 0.2, charleston, i % 2 == 0 ? 70 : 95);

        if (complete)
            ajouter (c, { 3.5 }, 0.4, charlestonOuvert, 90);

        return c;
    }

    Clip basse()
    {
        // Do, puis la bémol : la fondamentale sur les contretemps, façon techno
        Clip c { "Basse", 2, {} };

        for (int mesure = 0; mesure < 2; ++mesure)
            for (int i = 0; i < 4; ++i)
                ajouter (c, { mesure * 4.0 + i + 0.5 }, 0.4, mesure == 0 ? 36 : 32, i == 0 ? 115 : 95);

        return c;
    }

    Clip nappe()
    {
        Clip c { "Accords", 4, {} };
        const int accords[4][3] = { { 48, 51, 55 }, { 44, 48, 51 }, { 46, 50, 53 }, { 43, 47, 50 } };   // Do m, La b, Si b, Sol

        for (int a = 0; a < 4; ++a)
            for (int n : accords[a])
                ajouter (c, { a * 4.0 }, 3.9, n, 80);

        return c;
    }

    Clip lead()
    {
        Clip c { "Motif", 4, {} };
        const std::pair<double, int> motif[] = { { 0.0, 67 }, { 0.75, 70 }, { 1.5, 72 }, { 3.0, 70 },
                                                 { 4.0, 67 }, { 4.75, 65 }, { 5.5, 63 }, { 7.0, 62 },
                                                 { 8.0, 67 }, { 8.75, 70 }, { 9.5, 72 }, { 11.0, 75 },
                                                 { 12.0, 74 }, { 12.75, 72 }, { 13.5, 70 }, { 15.0, 67 } };

        for (auto [debut, hauteur] : motif)
            ajouter (c, { debut }, 0.6, hauteur, 95);

        return c;
    }

    Clip arpege()
    {
        Clip c { "Arpège"_fr, 1, {} };
        const int notes[] = { 60, 63, 67, 72 };

        for (int i = 0; i < 16; ++i)
            ajouter (c, { i * 0.25 }, 0.2, notes[i % 4], i % 4 == 0 ? 105 : 80);

        return c;
    }

    Clip percussions()
    {
        Clip c { "Percus", 1, {} };
        ajouter (c, { 0.75, 2.75 }, 0.2, rimshot, 90);
        ajouter (c, { 1.0, 3.0 }, 0.3, clap, 100);
        ajouter (c, { 3.5 }, 0.3, tom, 85);
        return c;
    }

    Clip stab()
    {
        Clip c { "Stabs", 2, {} };

        for (double d : { 0.5, 2.5, 4.5, 6.0, 6.5 })
            for (int n : { 60, 63, 67 })
                ajouter (c, { d }, 0.2, n, 95);

        return c;
    }

    Clip texture()
    {
        Clip c { "Souffle", 8, {} };
        ajouter (c, { 0.0 }, 15.5, 72, 60);
        ajouter (c, { 16.0 }, 15.5, 75, 60);
        return c;
    }
}

Morceau morceauDeDemonstration()
{
    Morceau m;
    m.nom = "Démo"_fr;
    m.tempo = 120.0;
    m.mesure = Mesure::standard (4, 4);

    auto scene = [&m] (const juce::String& nom, std::initializer_list<std::pair<int, Clip>> clips)
    {
        Scene s;
        s.nom = nom;

        for (auto& [piste, clip] : clips)
            s.clips[(size_t) piste] = clip;

        m.scenes.push_back (s);
    };

    scene ("Intro",  { { 0, batterie (false) }, { 2, nappe() }, { 7, texture() } });
    scene ("Groove", { { 0, batterie (true) }, { 1, basse() }, { 2, nappe() } });
    scene ("Plein",  { { 0, batterie (true) }, { 1, basse() }, { 2, nappe() }, { 3, lead() }, { 4, arpege() }, { 5, percussions() } });
    scene ("Pause",  { { 2, nappe() }, { 6, stab() }, { 7, texture() } });
    scene ("Retour", { { 0, batterie (true) }, { 1, basse() }, { 3, lead() }, { 4, arpege() }, { 5, percussions() }, { 6, stab() } });
    m.assurerScenes (8);
    return m;
}

} // namespace groovebox
