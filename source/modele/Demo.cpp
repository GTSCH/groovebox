#include "Demo.h"
#include "moteur/SonsDeSynthe.h"

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

//==============================================================================
// Démo 2 : 6/8, ré mineur, lourde. Les temps sont en croches : 6 par mesure.
namespace
{
    namespace pads
    {
        constexpr int grosseCaisse = 36, caisseClaire = 38, clap = 39, tomBas = 41, charleston = 42, tomMoyen = 43,
                      shaker = 44, tomHaut = 45, charlestonOuvert = 46, crash = 49, ride = 51;
    }

    Clip clip (const juce::String& nom, int mesures, std::initializer_list<Note> notes)
    {
        return Clip { nom, mesures, notes };
    }

    Clip silence()                   { return Clip { "Silence", 1, {} }; }

    Clip accords (const juce::String& nom, std::initializer_list<std::array<int, 3>> suite, int velocite = 82)
    {
        Clip c { nom, (int) suite.size(), {} };
        double debut = 0.0;

        for (auto& accord : suite)
        {
            for (int n : accord)
                c.notes.push_back ({ debut, 5.9, n, velocite });

            debut += 6.0;
        }

        return c;
    }

    // La batterie : lourde, à demi-temps (grosse caisse sur 1, caisse claire sur le 2e temps pointé)
    Clip batterieLourde()
    {
        using namespace pads;
        Clip c { "Lourd", 2, {} };

        for (int mesure = 0; mesure < 2; ++mesure)
        {
            const double m = mesure * 6.0;
            ajouter (c, { m, m + 2.5, m + 4.0 }, 0.25, grosseCaisse, 127);
            ajouter (c, { m + 3.0 }, 0.3, caisseClaire, 120);
            ajouter (c, { m + 3.0 }, 0.3, clap, 95);

            for (int i = 0; i < 6; ++i)
                ajouter (c, { m + i }, 0.2, charleston, i % 3 == 0 ? 100 : 70);
        }

        ajouter (c, { 0.0 }, 1.0, crash, 110);
        ajouter (c, { 10.5 }, 0.25, tomHaut, 105);
        ajouter (c, { 11.0, 11.5 }, 0.25, caisseClaire, 115);
        return c;
    }

    Clip batterieConstruction()
    {
        using namespace pads;
        Clip c { "Construction", 2, {} };

        for (int mesure = 0; mesure < 2; ++mesure)
        {
            const double m = mesure * 6.0;
            ajouter (c, { m, m + 0.5, m + 2.5, m + 4.0, m + 4.5 }, 0.25, grosseCaisse, 127);
            ajouter (c, { m + 3.0 }, 0.3, caisseClaire, 122);
            ajouter (c, { m + 3.0 }, 0.3, clap, 100);
            ajouter (c, { m + 1.5, m + 5.5 }, 0.2, caisseClaire, 55);     // notes fantômes

            for (int i = 0; i < 6; ++i)
                ajouter (c, { m + i }, 0.4, ride, i % 3 == 0 ? 105 : 80);
        }

        ajouter (c, { 0.0 }, 1.0, crash, 115);
        ajouter (c, { 9.0, 9.5, 10.0 }, 0.25, tomMoyen, 110);
        ajouter (c, { 10.5, 11.0, 11.5 }, 0.25, tomBas, 120);
        return c;
    }

    // Le sommet : double grosse caisse en continu, crash à chaque mesure
    Clip batterieSommet()
    {
        using namespace pads;
        Clip c { "Sommet", 2, {} };

        for (int mesure = 0; mesure < 2; ++mesure)
        {
            const double m = mesure * 6.0;

            for (int i = 0; i < 12; ++i)
                ajouter (c, { m + i * 0.5 }, 0.2, grosseCaisse, i % 6 == 0 ? 127 : 108);

            ajouter (c, { m + 3.0 }, 0.3, caisseClaire, 127);
            ajouter (c, { m + 3.0 }, 0.3, clap, 110);
            ajouter (c, { m }, 1.0, crash, 120);
            ajouter (c, { m + 5.5 }, 0.4, charlestonOuvert, 90);
        }

        ajouter (c, { 10.0, 10.5, 11.0, 11.5 }, 0.25, caisseClaire, 125);
        return c;
    }

    Clip dernierCoup()
    {
        using namespace pads;
        Clip c { "Dernier coup", 4, {} };
        ajouter (c, { 0.0 }, 0.3, grosseCaisse, 127);
        ajouter (c, { 0.0 }, 0.3, caisseClaire, 120);
        ajouter (c, { 0.0 }, 2.0, crash, 127);
        return c;
    }

    // Les toms qui arrivent, comme des tambours au loin, et un shaker
    Clip tambours (bool forts)
    {
        using namespace pads;
        Clip c { forts ? "Tambours" : "Tambours au loin", 1, {} };
        const int v = forts ? 120 : 85;
        ajouter (c, { 0.0, 2.0 }, 0.3, tomBas, v);
        ajouter (c, { 3.0 }, 0.3, tomMoyen, v - 10);
        ajouter (c, { 4.5, 5.0 }, 0.25, tomHaut, v - 15);

        for (int i = 0; i < 12; ++i)
            ajouter (c, { i * 0.5 }, 0.1, shaker, i % 2 == 0 ? 60 : 40);

        return c;
    }

    // La basse : ré, puis si bémol et la ; le riff appuie sur les deux temps pointés
    Clip basseRiff()
    {
        return clip ("Riff", 2, { { 0.0, 1.3, 38, 127 }, { 1.5, 0.4, 38, 100 }, { 2.0, 0.4, 38, 105 }, { 3.0, 1.3, 41, 120 }, { 4.5, 0.4, 43, 100 }, { 5.0, 0.9, 45, 110 },
                                  { 6.0, 1.3, 34, 127 }, { 7.5, 0.4, 34, 100 }, { 8.0, 0.4, 34, 105 }, { 9.0, 1.3, 33, 120 }, { 10.5, 0.4, 33, 100 }, { 11.0, 0.9, 36, 110 } });
    }
}

Morceau morceauDeDemonstration2()
{
    Morceau m;
    m.nom = "Démo 2"_fr;
    m.tempo = 120.0;
    m.mesure = Mesure::standard (6, 8);
    m.gamme.fondamentale = 2;       // ré
    m.gamme.mode = Gamme::Mode::mineur;
    m.clic.decompte = 1;
    m.delay.division = 0;           // croche pointée : un écho tous les trois quarts de temps
    m.delay.retour = 0.4f;
    m.reverb.taille = 0.75f;
    m.reverb.duree = 0.6f;
    m.compressionDeSortie = 0.45f;

    // Les sons : le kit Hard techno, la basse industrielle saturée, une nappe sombre, des cuivres, des stabs durs
    auto synthe = [&m] (int piste, const juce::String& nom, SonProvisoire son, const juce::String& surge, float volumeDb)
    {
        auto& p = m.pistes[(size_t) piste];
        p.nom = nom;
        p.son = son;
        p.sonDeSynthe = surge;
        p.volumeDb = volumeDb;

        if (auto s = sonsDeSynthe::trouver (surge))
            for (int k = 0; k < 8; ++k)
                p.macros[(size_t) k] = s->macros[(size_t) k].depart;
    };

    auto& batterie = m.pistes[0];
    batterie.nom = "Batterie";
    batterie.kit = kitsDeDepart::kit ("Hard techno");
    batterie.effets[0] = Effet::parDefaut (TypeEffet::punch);
    batterie.effets[0]->reglages = { 0.8f, 0.5f, 0.55f, 0.0f };
    batterie.effets[1] = Effet::parDefaut (TypeEffet::grave);
    batterie.effets[1]->reglages = { 0.55f, 0.35f, 0.5f, 0.0f };
    batterie.envoiReverbDb = -20.0f;
    batterie.volumeDb = -4.0f;      // le déroulé la fait monter jusqu'au sommet

    synthe (1, "Basse", SonProvisoire::basse, "Basse industrielle", -3.0f);
    m.pistes[1].effets[0] = Effet::parDefaut (TypeEffet::distorsion);
    m.pistes[1].effets[0]->reglages = { 0.45f, 0.45f, 0.45f, 0.55f };

    synthe (2, "Nappe", SonProvisoire::nappe, "Nappe sombre", -6.0f);
    m.pistes[2].envoiReverbDb = -6.0f;
    synthe (3, "Cuivres", SonProvisoire::lead, "Brass electro", -4.0f);
    m.pistes[3].envoiDelayDb = -12.0f;
    m.pistes[3].envoiReverbDb = -10.0f;
    synthe (4, "Arpège"_fr, SonProvisoire::arpege, "Arpège electro"_fr, -11.0f);
    m.pistes[4].envoiDelayDb = -8.0f;

    auto& pisteDesTambours = m.pistes[5];
    pisteDesTambours.nom = "Tambours";
    pisteDesTambours.son = SonProvisoire::percussions;
    pisteDesTambours.kit = kitsDeDepart::kit ("Hard techno");
    pisteDesTambours.volumeDb = -4.0f;
    pisteDesTambours.envoiReverbDb = -12.0f;

    synthe (6, "Stabs", SonProvisoire::stab, "Stab hard", -5.0f);
    m.pistes[6].envoiReverbDb = -8.0f;
    m.pistes[6].envoiDelayDb = -14.0f;
    synthe (7, "Grondement", SonProvisoire::texture, "Grondement", -6.0f);
    m.pistes[7].envoiReverbDb = -10.0f;

    // Les clips
    const auto nappe = accords ("Ré mineur"_fr, { { 50, 53, 57 }, { 46, 50, 53 }, { 43, 46, 50 }, { 45, 49, 52 } });     // Ré m, Si b, Sol m, La
    const auto nappeFinale = clip ("Accord final", 2, { { 0.0, 11.5, 50, 85 }, { 0.0, 11.5, 53, 80 }, { 0.0, 11.5, 57, 80 }, { 0.0, 11.5, 62, 70 } });
    const auto grondement = clip ("Grondement", 4, { { 0.0, 23.5, 38, 70 } });
    const auto basseTenue = clip ("Basse tenue", 4, { { 0.0, 11.5, 38, 100 }, { 12.0, 5.5, 34, 100 }, { 18.0, 5.5, 33, 100 } });
    const auto basseFinale = clip ("Ré final"_fr, 2, { { 0.0, 11.5, 38, 115 } });
    const auto arpege = clip ("Arpège"_fr, 1, { { 0.0, 0.4, 62, 105 }, { 0.5, 0.4, 65, 80 }, { 1.0, 0.4, 69, 85 }, { 1.5, 0.4, 74, 95 }, { 2.0, 0.4, 69, 80 }, { 2.5, 0.4, 65, 80 },
                                              { 3.0, 0.4, 62, 100 }, { 3.5, 0.4, 65, 80 }, { 4.0, 0.4, 69, 85 }, { 4.5, 0.4, 72, 95 }, { 5.0, 0.4, 69, 80 }, { 5.5, 0.4, 65, 80 } });
    const auto cuivres = clip ("Thème"_fr, 4, { { 0.0, 2.5, 62, 110 }, { 3.0, 1.4, 65, 100 }, { 4.5, 1.4, 64, 100 },
                                               { 6.0, 2.5, 62, 110 }, { 9.0, 1.4, 60, 100 }, { 10.5, 1.4, 58, 100 },
                                               { 12.0, 2.5, 57, 110 }, { 15.0, 1.4, 58, 100 }, { 16.5, 1.4, 60, 100 },
                                               { 18.0, 3.9, 61, 115 }, { 22.0, 1.9, 57, 105 } });
    Clip stabs { "Coups", 2, {} };

    for (auto [debut, accord] : { std::pair { 0.0, std::array { 62, 65, 69 } }, std::pair { 3.0, std::array { 62, 65, 69 } }, std::pair { 4.5, std::array { 62, 65, 69 } },
                                  std::pair { 6.0, std::array { 58, 62, 65 } }, std::pair { 9.0, std::array { 57, 61, 64 } }, std::pair { 10.5, std::array { 57, 61, 64 } } })
        for (int n : accord)
            stabs.notes.push_back ({ debut, 0.4, n, 115 });

    auto scene = [&m] (const juce::String& nom, std::initializer_list<std::pair<int, Clip>> clips)
    {
        Scene s;
        s.nom = nom;

        for (auto& [piste, c] : clips)
            s.clips[(size_t) piste] = c;

        m.scenes.push_back (s);
    };

    scene ("Intro",        { { 1, basseTenue }, { 2, nappe }, { 7, grondement } });
    scene ("Approche",     { { 1, basseTenue }, { 2, nappe }, { 4, arpege }, { 5, tambours (false) }, { 7, grondement } });
    scene ("Batterie",     { { 0, batterieLourde() }, { 1, basseRiff() }, { 2, nappe }, { 4, arpege }, { 5, silence() }, { 7, grondement } });
    scene ("Construction", { { 0, batterieConstruction() }, { 1, basseRiff() }, { 2, nappe }, { 3, cuivres }, { 4, arpege }, { 5, tambours (true) } });
    scene ("Sommet",       { { 0, batterieSommet() }, { 1, basseRiff() }, { 2, nappe }, { 3, cuivres }, { 4, arpege }, { 5, tambours (true) }, { 6, stabs } });
    scene ("Fin",          { { 0, dernierCoup() }, { 1, basseFinale }, { 2, nappeFinale }, { 3, silence() }, { 4, silence() }, { 5, silence() }, { 6, silence() }, { 7, grondement } });
    m.assurerScenes (8);
    return m;
}

Deroule derouleDeLaDemo2 (const Morceau& m)
{
    Deroule d;
    d.nom = "Démo 2 en entier"_fr;
    auto etat = m.versJson();
    std::array<int, nombreDePistes> scenes;
    scenes.fill (-1);

    // Un réglage qui glisse d'une valeur à une autre, un cran par croche, sur une partie de la section
    auto rampe = [] (std::vector<EvenementDePrise>& gestes, const juce::String& chemin, double de, double a, double debut, double duree)
    {
        for (double t = 0.0; t <= duree + 1.0e-6; t += 1.0)
        {
            EvenementDePrise e;
            e.genre = EvenementDePrise::Genre::reglages;
            e.temps = debut + t;
            const double x = t / duree;
            juce::Array<juce::var> paire { juce::var (chemin), juce::var (de + (a - de) * x) };
            e.changements = juce::Array<juce::var> { juce::var (paire) };
            gestes.push_back (e);
        }
    };

    auto section = [&] (int scene, int mesures, Enchainement entree, std::vector<EvenementDePrise> gestes)
    {
        Section s;
        s.scene = scene;
        s.nom = m.scenes[(size_t) scene].nom;
        s.entree = entree;
        s.longueur = mesures * 6.0;
        s.avance = 1.0;         // lancé une croche avant la mesure, comme au Push
        s.etat = etat;

        for (int p = 0; p < nombreDePistes; ++p)
            if (m.clip (p, scene) != nullptr)
                scenes[(size_t) p] = scene;     // une case vide laisse la piste continuer

        s.scenesDeDepart = scenes;
        std::sort (gestes.begin(), gestes.end(), [] (auto& a, auto& b) { return a.temps < b.temps; });
        s.evenements = gestes;

        // L'état de la section suivante : celui-ci, avec ses réglages
        for (auto& e : gestes)
            etat = ecarts::appliquer (etat, e.changements);

        d.sections.push_back (s);
    };

    auto entree = [] (TypeEnchainement type, int mesures, bool montee = false, float intensite = 0.7f)
    {
        Enchainement e;
        e.type = type;
        e.mesures = mesures;
        e.montee = montee;
        e.intensite = intensite;
        return e;
    };

    std::vector<EvenementDePrise> approche, construction, sommet, fin;

    // Approche : la nappe s'ouvre
    rampe (approche, "/pistes/2/macros/0", 0.3, 0.55, 0.0, 24.0);

    // Construction : les cuivres et la basse s'ouvrent, le grain de la basse monte
    rampe (construction, "/pistes/3/macros/0", 0.25, 0.7, 0.0, 48.0);
    rampe (construction, "/pistes/1/macros/0", 0.4, 0.6, 0.0, 48.0);
    rampe (construction, "/pistes/1/macros/2", 0.6, 0.85, 12.0, 36.0);
    rampe (construction, "/pistes/0/volumeDb", -4.0, -2.0, 0.0, 48.0);

    // Sommet : tout est ouvert, la distorsion de la basse pousse
    rampe (sommet, "/pistes/1/effets/0/reglages/0", 0.45, 0.65, 0.0, 24.0);
    rampe (sommet, "/pistes/6/macros/0", 0.5, 0.85, 0.0, 48.0);
    rampe (sommet, "/pistes/0/volumeDb", -2.0, 0.0, 0.0, 12.0);

    // Fin : le volume général s'éteint sur les deux dernières mesures, la queue d'écho sonne par-dessus
    rampe (fin, "/volumeGeneralDb", 0.0, -30.0, 12.0, 12.0);

    section (0, 4, entree (TypeEnchainement::coupe, 1), {});
    section (1, 4, entree (TypeEnchainement::fondu, 2), approche);
    section (2, 8, entree (TypeEnchainement::coupe, 1, true, 0.8f), {});
    section (3, 8, entree (TypeEnchainement::filtre, 1, false, 0.6f), construction);
    section (4, 8, entree (TypeEnchainement::coupe, 1, true, 1.0f), sommet);
    section (5, 4, entree (TypeEnchainement::queueDEcho, 2, false, 0.9f), fin);
    return d;
}

} // namespace groovebox
