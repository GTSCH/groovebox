#include "Morceau.h"

namespace groovebox
{

namespace
{
    juce::var mesureVersJson (const Mesure& m)
    {
        auto objet = new juce::DynamicObject();
        objet->setProperty ("numerateur", m.numerateur);
        objet->setProperty ("denominateur", m.denominateur);

        juce::Array<juce::var> groupement;

        for (auto g : m.groupement)
            groupement.add (g);

        objet->setProperty ("groupement", groupement);
        return juce::var (objet);
    }

    std::optional<Mesure> mesureDepuisJson (const juce::var& v)
    {
        if (! v.isObject())
            return std::nullopt;

        Mesure m { (int) v.getProperty ("numerateur", 4), (int) v.getProperty ("denominateur", 4), {} };

        if (auto groupement = v.getProperty ("groupement", {}).getArray())
            for (auto& g : *groupement)
                m.groupement.push_back ((int) g);

        if (! m.estValide())
            m = Mesure::standard (m.numerateur, m.denominateur);

        if (! m.estValide())
            return std::nullopt;

        return m;
    }
}

juce::var Morceau::versJson() const
{
    auto racine = new juce::DynamicObject();
    racine->setProperty ("format", "groovebox-morceau");
    racine->setProperty ("version", versionDuFormat);
    racine->setProperty ("nom", nom);
    racine->setProperty ("tempo", tempo);
    racine->setProperty ("mesure", mesureVersJson (mesure));

    auto c = new juce::DynamicObject();
    c->setProperty ("actif", clic.actif);
    c->setProperty ("volume", clic.volume);
    c->setProperty ("subdivisions", clic.subdivisions);
    c->setProperty ("unClicParGroupe", clic.unClicParGroupe);
    c->setProperty ("decompte", clic.decompte);
    c->setProperty ("ecouteSurMac", clic.ecouteSurMac);
    racine->setProperty ("clic", juce::var (c));

    racine->setProperty ("musiqueDansCasqueDb", musiqueDansCasqueDb);
    racine->setProperty ("volumeGeneralDb", volumeGeneralDb);

    juce::Array<juce::var> listeScenes;

    for (auto& s : scenes)
    {
        auto objet = new juce::DynamicObject();
        objet->setProperty ("nom", s.nom);
        objet->setProperty ("mesure", s.mesure ? mesureVersJson (*s.mesure) : juce::var());
        objet->setProperty ("tempo", s.tempo ? juce::var (*s.tempo) : juce::var());
        listeScenes.add (juce::var (objet));
    }

    racine->setProperty ("scenes", listeScenes);
    return juce::var (racine);
}

Morceau Morceau::depuisJson (const juce::var& v)
{
    Morceau m;
    m.nom = v.getProperty ("nom", m.nom).toString();
    m.tempo = juce::jlimit (tempoMin, tempoMax, (double) v.getProperty ("tempo", m.tempo));

    if (auto mesure = mesureDepuisJson (v.getProperty ("mesure", {})))
        m.mesure = *mesure;

    const auto c = v.getProperty ("clic", {});
    m.clic.actif = c.getProperty ("actif", m.clic.actif);
    m.clic.volume = juce::jlimit (0.0f, 1.0f, (float) c.getProperty ("volume", m.clic.volume));
    m.clic.subdivisions = c.getProperty ("subdivisions", m.clic.subdivisions);
    m.clic.unClicParGroupe = c.getProperty ("unClicParGroupe", m.clic.unClicParGroupe);
    m.clic.decompte = juce::jlimit (0, 2, (int) c.getProperty ("decompte", m.clic.decompte));
    m.clic.ecouteSurMac = c.getProperty ("ecouteSurMac", m.clic.ecouteSurMac);

    m.musiqueDansCasqueDb = juce::jlimit (-60.0f, 6.0f, (float) v.getProperty ("musiqueDansCasqueDb", m.musiqueDansCasqueDb));
    m.volumeGeneralDb = juce::jlimit (-60.0f, 6.0f, (float) v.getProperty ("volumeGeneralDb", m.volumeGeneralDb));

    if (auto liste = v.getProperty ("scenes", {}).getArray())
    {
        for (auto& s : *liste)
        {
            Scene scene;
            scene.nom = s.getProperty ("nom", {}).toString();
            scene.mesure = mesureDepuisJson (s.getProperty ("mesure", {}));

            if (const auto t = s.getProperty ("tempo", {}); ! t.isVoid())
                scene.tempo = (double) t;

            m.scenes.push_back (scene);
        }
    }

    return m;
}

bool Morceau::enregistrer (const juce::File& fichier) const
{
    // Écriture dans un fichier à côté, puis remplacement : un plantage ne laisse jamais un fichier à moitié écrit
    juce::TemporaryFile temporaire (fichier);

    if (! temporaire.getFile().replaceWithText (juce::JSON::toString (versJson())))
        return false;

    return temporaire.overwriteTargetFileWithTemporary();
}

std::optional<Morceau> Morceau::charger (const juce::File& fichier)
{
    if (! fichier.existsAsFile())
        return std::nullopt;

    const auto v = juce::JSON::parse (fichier);

    if (v.getProperty ("format", {}).toString() != "groovebox-morceau")
        return std::nullopt;

    return depuisJson (v);
}

} // namespace groovebox
