#include "Kit.h"

namespace groovebox
{

juce::var Kit::versJson() const
{
    auto racine = new juce::DynamicObject();
    racine->setProperty ("nom", nom);
    racine->setProperty ("compression", compression);
    racine->setProperty ("largeur", largeur);

    juce::Array<juce::var> liste;

    for (auto& p : pads)
    {
        auto o = new juce::DynamicObject();
        o->setProperty ("son", p.son);
        o->setProperty ("hauteur", p.hauteur);
        o->setProperty ("decroissance", p.decroissance);
        o->setProperty ("saturation", p.saturation);
        o->setProperty ("couleur", p.couleur);
        o->setProperty ("volumeDb", p.volumeDb);
        o->setProperty ("etouffement", p.etouffement);
        liste.add (juce::var (o));
    }

    racine->setProperty ("pads", liste);
    return juce::var (racine);
}

Kit Kit::depuisJson (const juce::var& v)
{
    Kit k;
    k.nom = v.getProperty ("nom", {}).toString();
    k.compression = juce::jlimit (0.0f, 1.0f, (float) v.getProperty ("compression", 0.0f));
    k.largeur = juce::jlimit (0.0f, 1.0f, (float) v.getProperty ("largeur", 0.5f));

    if (auto liste = v.getProperty ("pads", {}).getArray())
        for (int i = 0; i < std::min (nombreDePads, liste->size()); ++i)
        {
            const auto& o = (*liste)[i];
            auto& p = k.pads[(size_t) i];
            p.son = o.getProperty ("son", {}).toString();
            p.hauteur = juce::jlimit (-24.0f, 24.0f, (float) o.getProperty ("hauteur", 0.0f));
            p.decroissance = juce::jlimit (0.0f, 1.0f, (float) o.getProperty ("decroissance", 1.0f));
            p.saturation = juce::jlimit (0.0f, 1.0f, (float) o.getProperty ("saturation", 0.0f));
            p.couleur = juce::jlimit (-1.0f, 1.0f, (float) o.getProperty ("couleur", 0.0f));
            p.volumeDb = juce::jlimit (-40.0f, 6.0f, (float) o.getProperty ("volumeDb", 0.0f));
            p.etouffement = juce::jlimit (0, 8, (int) o.getProperty ("etouffement", 0));
        }

    return k;
}

namespace kitsDeDepart
{
    const std::array<juce::String, Kit::nombreDePads>& nomsDesPads()
    {
        static const std::array<juce::String, Kit::nombreDePads> noms {
            "Grosse caisse", "Rimshot", "Caisse claire", "Clap", "Caisse claire 2", "Tom bas", "Charleston fermé"_fr, "Tom moyen",
            "Shaker", "Tom haut", "Charleston ouvert", "Conga", "Cloche", "Crash", "Perc", "Ride" };
        return noms;
    }

    const std::array<juce::String, Kit::nombreDePads>& categoriesDesPads()
    {
        static const std::array<juce::String, Kit::nombreDePads> categories {
            "Grosses caisses", "Percussions", "Caisses claires", "Claps", "Caisses claires", "Toms", "Charlestons", "Toms",
            "Percussions", "Toms", "Charlestons", "Percussions", "Percussions", "Cymbales", "Percussions", "Cymbales" };
        return categories;
    }

    juce::StringArray noms()
    {
        return { "Analogique", "Minimal", "Dub", "Hard techno" };
    }

    Kit kit (const juce::String& nom)
    {
        Kit k;
        k.nom = nom;
        k.compression = nom == "Hard techno" ? 0.6f : nom == "Dub" ? 0.2f : 0.1f;
        k.largeur = nom == "Minimal" ? 0.3f : 0.5f;

        for (int i = 0; i < Kit::nombreDePads; ++i)
        {
            auto& p = k.pads[(size_t) i];
            p.son = categoriesDesPads()[(size_t) i] + "/" + nom + " - " + nomsDesPads()[(size_t) i] + ".wav";
        }

        // Le charleston fermé coupe l'ouvert
        k.pads[42 - Kit::premiereNote].etouffement = 1;
        k.pads[46 - Kit::premiereNote].etouffement = 1;
        return k;
    }

    juce::String nomDuPad (const Kit& kit, int pad)
    {
        const auto& son = kit.pads[(size_t) juce::jlimit (0, Kit::nombreDePads - 1, pad)].son;

        if (son.startsWith (kitsAcoustiques::dossier + "/"))
            return son.fromLastOccurrenceOf ("/", false, false);

        return nomsDesPads()[(size_t) juce::jlimit (0, Kit::nombreDePads - 1, pad)];
    }
}

namespace kitsAcoustiques
{
    juce::StringArray noms()
    {
        return { "Black Pearl", "Blonde Bop", "Blonde Bop fagots", "Red Zeppelin", "Cajón"_fr };
    }

    const std::array<juce::String, Kit::nombreDePads>& instruments (const juce::String& kit)
    {
        // Mêmes rôles que les pads des kits de départ : grosse caisse en 36, caisse claire en 38, charlestons en 42, 44 et 46,
        // toms en 41, 43 et 45, crash en 49, cloche de ride en 50, ride en 51
        static const std::array<juce::String, Kit::nombreDePads> batterie {
            "Grosse caisse", "Coup de cercle", "Caisse claire", "Clap", "Bord de caisse claire", "Tom basse", "Charleston fermé"_fr,
            "Bord de tom basse", "Pied de charleston", "Tom", "Charleston ouvert", "Bord de tom", "Cloche", "Crash", "Cloche de ride", "Ride" };
        static const std::array<juce::String, Kit::nombreDePads> cajon {
            "Cajón grave"_fr, "Claquement de doigts", "Cajón claqué"_fr, "Claps", "Cajón claqué droite"_fr, "Conga grave", "Shaker",
            "Conga grave droite", "Tambourin secoué"_fr, "Conga aiguë"_fr, "Tambourin frappé"_fr, "Conga aiguë droite"_fr, "Claves",
            "Cymbale", "Cloche de cymbale", "Cloche" };
        return kit.startsWith ("Caj") ? cajon : batterie;
    }

    Kit kit (const juce::String& nom)
    {
        Kit k;
        k.nom = nom;
        k.compression = 0.15f;
        k.largeur = 0.6f;

        for (int i = 0; i < Kit::nombreDePads; ++i)
            k.pads[(size_t) i].son = dossier + "/" + nom + "/" + instruments (nom)[(size_t) i];

        // Les trois charlestons se coupent entre eux, comme sur une vraie batterie
        if (! nom.startsWith ("Caj"))
            for (int note : { 42, 44, 46 })
                k.pads[(size_t) (note - Kit::premiereNote)].etouffement = 1;

        return k;
    }
}

} // namespace groovebox
