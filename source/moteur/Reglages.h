#pragma once

#include "Texte.h"

namespace groovebox
{
namespace te = tracktion;

/** Réglages de l'application (carte son, préférences), rangés dans un dossier choisi.
    Par défaut : ~/Library/Application Support/groovebox. */
class Reglages  : public te::PropertyStorage
{
public:
    explicit Reglages (juce::File dossier = dossierParDefaut());

    static juce::File dossierParDefaut();

    juce::File getAppPrefsFolder() override;
    juce::String getApplicationVersion() override       { return ProjectInfo::versionString; }

private:
    juce::File dossier;
};

/** Au tout premier lancement : 48 kHz et tampon de 128 échantillons. Ensuite, on garde le choix de l'utilisateur. */
void appliquerReglagesDeDepart (te::Engine&);

/** Entrées 1-2 (micros de la batterie) : ouvertes seulement si la carte de sortie a ses propres entrées (la Scarlett).
    Sinon, aucune entrée : mélanger deux appareils (haut-parleurs et micro du Mac) risquerait des coupures. */
void reglerLesEntrees (juce::AudioDeviceManager&, juce::AudioDeviceManager::AudioDeviceSetup&);
void ouvrirLesEntreesDeLaCarte (te::Engine&);

} // namespace groovebox
