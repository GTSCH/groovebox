#pragma once

#include "Texte.h"

namespace groovebox
{

/** Un son chargé en mémoire, prêt à être lu par le fil audio. Jamais modifié après le chargement. */
struct Echantillon
{
    juce::AudioBuffer<float> audio;     ///< 1 ou 2 canaux
    double frequence = 48000.0;
    juce::String chemin;
};

/** Le dossier Sons (Musique/groovebox/Sons), classé par catégories, et les sons déjà chargés.
    Les sons restent en mémoire jusqu'à la fin : le fil audio peut les lire sans risque. */
class BanqueDeSons
{
public:
    explicit BanqueDeSons (juce::File racine = racineParDefaut());

    static juce::File racineParDefaut();
    const juce::File& getRacine() const     { return racine; }

    /** Charge un son (chemin relatif au dossier Sons) ; nul si le fichier manque ou est illisible. Fil principal. */
    const Echantillon* charger (const juce::String& chemin);

    juce::StringArray categories() const;
    juce::StringArray sons (const juce::String& categorie) const;     ///< Chemins relatifs, triés

    /** Copie des fichiers son (WAV, AIFF, MP3, FLAC...) dans le dossier « Mes sons » ; renvoie leurs chemins relatifs.
        Un nom déjà pris reçoit un numéro ; un fichier illisible est laissé de côté. */
    juce::StringArray importer (const juce::StringArray& fichiers);
    static bool estUnSon (const juce::File&);

private:
    juce::File racine;
    juce::AudioFormatManager formats;
    std::map<juce::String, std::unique_ptr<Echantillon>> charges;
    juce::CriticalSection verrou;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BanqueDeSons)
};

} // namespace groovebox
