#include "Sampler.h"

namespace groovebox
{

juce::String nomDuModeSampler (ModeSampler m)
{
    switch (m)
    {
        case ModeSampler::unCoup:   return "Un coup";
        case ModeSampler::notes:    return "En notes";
        case ModeSampler::decoupe:  return "Découpé"_fr;
        case ModeSampler::boucle:   return "Boucle";
    }

    return {};
}

std::pair<double, double> ReglagesSampler::partieJouee() const
{
    const double d = juce::jlimit (0.0, 0.99, (double) debut);
    const double f = d + (1.0 - d) * juce::jlimit (0.01, 1.0, (double) longueur);
    return { d, f };
}

double ReglagesSampler::attaqueEnSecondes() const
{
    return attaque <= 0.0f ? 0.0 : 0.002 * std::pow (2.0 / 0.002, (double) attaque);
}

double ReglagesSampler::relacheEnSecondes() const
{
    return 0.005 * std::pow (4.0 / 0.005, (double) juce::jlimit (0.0f, 1.0f, relache));
}

juce::var ReglagesSampler::versJson() const
{
    auto o = new juce::DynamicObject();
    o->setProperty ("son", son);
    o->setProperty ("mode", (int) mode);
    o->setProperty ("debut", debut);
    o->setProperty ("longueur", longueur);
    o->setProperty ("hauteur", hauteur);
    o->setProperty ("filtre", filtre);
    o->setProperty ("attaque", attaque);
    o->setProperty ("relache", relache);
    o->setProperty ("envers", envers);
    o->setProperty ("volumeDb", volumeDb);
    o->setProperty ("mesuresDeBoucle", mesuresDeBoucle);
    return juce::var (o);
}

ReglagesSampler ReglagesSampler::depuisJson (const juce::var& v)
{
    ReglagesSampler r;

    if (! v.isObject())
        return r;

    r.son = v.getProperty ("son", {}).toString();
    r.mode = (ModeSampler) juce::jlimit (0, nombreDeModesSampler - 1, (int) v.getProperty ("mode", 0));
    r.debut = juce::jlimit (0.0f, 0.99f, (float) v.getProperty ("debut", 0.0f));
    r.longueur = juce::jlimit (0.01f, 1.0f, (float) v.getProperty ("longueur", 1.0f));
    r.hauteur = juce::jlimit (-24.0f, 24.0f, (float) v.getProperty ("hauteur", 0.0f));
    r.filtre = juce::jlimit (-1.0f, 1.0f, (float) v.getProperty ("filtre", 0.0f));
    r.attaque = juce::jlimit (0.0f, 1.0f, (float) v.getProperty ("attaque", 0.0f));
    r.relache = juce::jlimit (0.0f, 1.0f, (float) v.getProperty ("relache", 0.2f));
    r.envers = v.getProperty ("envers", false);
    r.volumeDb = juce::jlimit (-40.0f, 6.0f, (float) v.getProperty ("volumeDb", 0.0f));
    r.mesuresDeBoucle = juce::jlimit (0, 16, (int) v.getProperty ("mesuresDeBoucle", 0));
    return r;
}

namespace sampler
{
    double tempoDansLeNom (const juce::String& chemin)
    {
        // Le premier nombre entre 60 et 200 du nom
        const auto nom = juce::File (chemin).getFileNameWithoutExtension();
        juce::String nombre;

        for (int i = 0; i <= nom.length(); ++i)
        {
            if (i < nom.length() && juce::CharacterFunctions::isDigit (nom[i]))
            {
                nombre << nom[i];
                continue;
            }

            if (nombre.isNotEmpty())
            {
                const auto v = nombre.getIntValue();

                if (v >= 60 && v <= 200)
                    return (double) v;

                nombre.clear();
            }
        }

        return 0.0;
    }

    int mesuresDeLaBoucle (const ReglagesSampler& r, double secondes, double tempo, const Mesure& mesure)
    {
        if (r.mesuresDeBoucle > 0)
            return r.mesuresDeBoucle;

        const double noiresParMesure = mesure.numerateur * 4.0 / juce::jmax (1, mesure.denominateur);

        // Tempo écrit dans le nom du fichier (« Jungle 170 », « loop_92bpm ») : il dit combien de temps dure la boucle
        if (const auto tempoDuNom = tempoDansLeNom (r.son); tempoDuNom > 0.0)
        {
            const double mesuresJouees = secondes * tempoDuNom / 60.0 / noiresParMesure;
            int meilleure = 1;

            for (int mesures : { 1, 2, 4, 8, 16 })
                if (std::abs (std::log2 (mesures / mesuresJouees)) < std::abs (std::log2 (meilleure / mesuresJouees)))
                    meilleure = mesures;

            return meilleure;
        }

        // Sinon, le tempo d'origine (à la noire) qu'aurait la boucle sur 1, 2, 4, 8 ou 16 mesures : on garde le plus proche
        int meilleure = 1;
        double ecart = 1.0e9;

        for (int mesures : { 1, 2, 4, 8, 16 })
        {
            const double tempoDOrigine = mesures * noiresParMesure * 60.0 / juce::jmax (0.01, secondes);
            const double e = std::abs (std::log2 (tempoDOrigine / tempo));

            if (e < ecart)
            {
                ecart = e;
                meilleure = mesures;
            }
        }

        return meilleure;
    }

    ModeSampler modePourLaCategorie (const juce::String& categorie)
    {
        if (categorie == "Boucles")
            return ModeSampler::boucle;

        if (categorie == "Textures")
            return ModeSampler::notes;

        return ModeSampler::unCoup;
    }

    juce::StringArray categoriesDuSampler()
    {
        return { "Voix", "Boucles", "Textures", "One-shots", "Mes sons" };
    }
}

} // namespace groovebox
