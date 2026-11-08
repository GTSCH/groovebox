#include "BanqueDeSons.h"

namespace groovebox
{

BanqueDeSons::BanqueDeSons (juce::File r)  : racine (std::move (r))
{
    racine.createDirectory();
    formats.registerBasicFormats();
}

juce::File BanqueDeSons::racineParDefaut()
{
    return juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("groovebox").getChildFile ("Sons");
}

bool BanqueDeSons::estUnDossierDeNiveaux (const juce::File& dossier)
{
    if (! dossier.isDirectory())
        return false;

    const auto fichiers = dossier.findChildFiles (juce::File::findFilesAndDirectories, false, "*", juce::File::FollowSymlinks::no);
    int niveaux = 0;

    for (auto& f : fichiers)
    {
        if (f.getFileName().startsWithChar ('.'))
            continue;

        if (f.isDirectory() || ! estUnSon (f) || ! f.getFileNameWithoutExtension().containsOnly ("0123456789"))
            return false;

        ++niveaux;
    }

    return niveaux > 0;
}

std::vector<const Echantillon*> BanqueDeSons::chargerNiveaux (const juce::String& chemin)
{
    std::vector<const Echantillon*> niveaux;
    const auto dossier = racine.getChildFile (chemin);

    if (! estUnDossierDeNiveaux (dossier))
    {
        if (auto* e = charger (chemin))
            niveaux.push_back (e);

        return niveaux;
    }

    auto fichiers = dossier.findChildFiles (juce::File::findFiles, false);
    std::sort (fichiers.begin(), fichiers.end(), [] (const juce::File& a, const juce::File& b)
               { return a.getFileNameWithoutExtension().getIntValue() < b.getFileNameWithoutExtension().getIntValue(); });

    for (auto& f : fichiers)
        if (estUnSon (f))
            if (auto* e = charger (f.getRelativePathFrom (racine)))
                niveaux.push_back (e);

    return niveaux;
}

const Echantillon* BanqueDeSons::charger (const juce::String& chemin)
{
    if (chemin.isEmpty())
        return nullptr;

    if (estUnDossierDeNiveaux (racine.getChildFile (chemin)))
    {
        const auto niveaux = chargerNiveaux (chemin);
        return niveaux.empty() ? nullptr : niveaux.back();
    }

    const juce::ScopedLock sl (verrou);

    if (auto existant = charges.find (chemin); existant != charges.end())
        return existant->second.get();

    std::unique_ptr<juce::AudioFormatReader> lecteur (formats.createReaderFor (racine.getChildFile (chemin)));

    if (lecteur == nullptr || lecteur->lengthInSamples <= 0)
    {
        juce::Logger::writeToLog ("Son introuvable ou illisible : "_fr + chemin);
        return nullptr;
    }

    // Au plus 60 secondes et 2 canaux (voix et boucles du sampler)
    const auto longueur = (int) juce::jmin (lecteur->lengthInSamples, (juce::int64) (lecteur->sampleRate * 60.0));
    auto e = std::make_unique<Echantillon>();
    e->audio.setSize ((int) juce::jmin (2u, lecteur->numChannels), longueur);
    lecteur->read (&e->audio, 0, longueur, 0, true, e->audio.getNumChannels() > 1);
    e->frequence = lecteur->sampleRate;
    e->chemin = chemin;

    auto* resultat = e.get();
    charges[chemin] = std::move (e);
    return resultat;
}

juce::StringArray BanqueDeSons::categories() const
{
    juce::StringArray liste;

    for (auto& d : racine.findChildFiles (juce::File::findDirectories, false))
        liste.add (d.getFileName());

    liste.sortNatural();
    return liste;
}

juce::StringArray BanqueDeSons::sons (const juce::String& categorie) const
{
    juce::StringArray liste;

    std::map<juce::String, bool> dossiersDeNiveaux;

    for (auto& f : racine.getChildFile (categorie).findChildFiles (juce::File::findFiles, true, "*.wav;*.aif;*.aiff;*.flac"))
    {
        const auto parent = f.getParentDirectory();
        auto [dejaVu, nouveau] = dossiersDeNiveaux.try_emplace (parent.getFullPathName(), false);

        if (nouveau)
            dejaVu->second = estUnDossierDeNiveaux (parent);

        if (dejaVu->second)
            liste.addIfNotAlreadyThere (f.getParentDirectory().getRelativePathFrom (racine));
        else
            liste.add (f.getRelativePathFrom (racine));
    }

    liste.sortNatural();
    return liste;
}

bool BanqueDeSons::estUnSon (const juce::File& f)
{
    return f.hasFileExtension ("wav;aif;aiff;mp3;flac;m4a;caf;ogg");
}

juce::StringArray BanqueDeSons::importer (const juce::StringArray& fichiers)
{
    juce::StringArray chemins;
    auto dossier = racine.getChildFile ("Mes sons");
    dossier.createDirectory();

    for (auto& nom : fichiers)
    {
        const juce::File source (nom);

        if (! source.existsAsFile() || ! estUnSon (source))
            continue;

        // Fichier déjà dans Sons : on le prend tel quel
        if (source.isAChildOf (racine))
        {
            chemins.add (source.getRelativePathFrom (racine));
            continue;
        }

        std::unique_ptr<juce::AudioFormatReader> lecteur (formats.createReaderFor (source));

        if (lecteur == nullptr)
            continue;

        lecteur.reset();
        auto cible = dossier.getChildFile (source.getFileName());

        if (cible.existsAsFile())
            cible = cible.getNonexistentSibling (true);

        if (source.copyFileTo (cible))
            chemins.add (cible.getRelativePathFrom (racine));
    }

    return chemins;
}

} // namespace groovebox
