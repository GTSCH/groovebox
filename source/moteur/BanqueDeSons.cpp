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

const Echantillon* BanqueDeSons::charger (const juce::String& chemin)
{
    if (chemin.isEmpty())
        return nullptr;

    const juce::ScopedLock sl (verrou);

    if (auto existant = charges.find (chemin); existant != charges.end())
        return existant->second.get();

    std::unique_ptr<juce::AudioFormatReader> lecteur (formats.createReaderFor (racine.getChildFile (chemin)));

    if (lecteur == nullptr || lecteur->lengthInSamples <= 0)
    {
        juce::Logger::writeToLog ("Son introuvable ou illisible : "_fr + chemin);
        return nullptr;
    }

    // Au plus 20 secondes et 2 canaux : une batterie n'a pas besoin de plus
    const auto longueur = (int) juce::jmin (lecteur->lengthInSamples, (juce::int64) (lecteur->sampleRate * 20.0));
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

    for (auto& f : racine.getChildFile (categorie).findChildFiles (juce::File::findFiles, true, "*.wav;*.aif;*.aiff;*.flac"))
        liste.add (f.getRelativePathFrom (racine));

    liste.sortNatural();
    return liste;
}

} // namespace groovebox
