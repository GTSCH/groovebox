#include "Reglages.h"

namespace groovebox
{

Reglages::Reglages (juce::File d)
    : te::PropertyStorage ("groovebox"), dossier (std::move (d))
{
}

juce::File Reglages::dossierParDefaut()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
             .getChildFile ("Application Support")
             .getChildFile ("groovebox");
}

juce::File Reglages::getAppPrefsFolder()
{
    if (! dossier.isDirectory())
        dossier.createDirectory();

    return dossier;
}

void appliquerReglagesDeDepart (te::Engine& engine)
{
    auto& fichier = engine.getPropertyStorage().getPropertiesFile();

    if (fichier.getBoolValue ("reglagesDeDepartAppliques"))
        return;

    auto& cartes = engine.getDeviceManager().deviceManager;
    auto reglage = cartes.getAudioDeviceSetup();
    reglage.sampleRate = 48000.0;
    reglage.bufferSize = 128;

    const auto erreur = cartes.setAudioDeviceSetup (reglage, true);

    if (erreur.isNotEmpty())
        juce::Logger::writeToLog ("Réglages de départ impossibles : "_fr + erreur);

    fichier.setValue ("reglagesDeDepartAppliques", true);
    fichier.saveIfNeeded();
}

} // namespace groovebox
