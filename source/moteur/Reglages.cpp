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

void reglerLesEntrees (juce::AudioDeviceManager& cartes, juce::AudioDeviceManager::AudioDeviceSetup& reglage)
{
    bool aSesEntrees = false;

    if (auto* type = cartes.getCurrentDeviceTypeObject())
        aSesEntrees = reglage.outputDeviceName.isNotEmpty() && type->getDeviceNames (true).contains (reglage.outputDeviceName);

    reglage.useDefaultInputChannels = false;
    reglage.inputChannels.clear();

    if (aSesEntrees)
    {
        reglage.inputDeviceName = reglage.outputDeviceName;
        reglage.inputChannels.setRange (0, 2, true);
    }
    else
    {
        reglage.inputDeviceName = {};
    }
}

void ouvrirLesEntreesDeLaCarte (te::Engine& engine)
{
    auto& cartes = engine.getDeviceManager().deviceManager;
    const auto avant = cartes.getAudioDeviceSetup();
    auto reglage = avant;
    reglerLesEntrees (cartes, reglage);

    if (reglage.inputDeviceName == avant.inputDeviceName && reglage.inputChannels == avant.inputChannels)
        return;

    const auto erreur = cartes.setAudioDeviceSetup (reglage, true);

    if (erreur.isNotEmpty())
        juce::Logger::writeToLog ("Entrées de la carte son impossibles à ouvrir : "_fr + erreur);
    else
        juce::Logger::writeToLog (reglage.inputDeviceName.isEmpty() ? juce::String ("Aucune entrée : la carte de sortie n'en a pas")
                                                                    : "Entrées 1-2 ouvertes sur "_fr + reglage.inputDeviceName);
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
