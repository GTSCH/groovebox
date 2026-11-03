#pragma once

#include "modele/Kit.h"

namespace groovebox
{

/** Fabrique par le calcul les sons des kits de départ et les enregistre en WAV (48 kHz, 24 bits, mono)
    dans le dossier Sons. Aucun son n'est téléchargé. */
namespace fabriqueDeSons
{
    /** Le son d'un pad (0 à 15) d'un kit de départ. */
    juce::AudioBuffer<float> fabriquer (const juce::String& kit, int pad, double frequenceEchantillonnage = 48000.0);

    /** Écrit les sons manquants des kits de départ ; renvoie le nombre de fichiers écrits. */
    int assurerKitsDeDepart (const juce::File& dossierSons);
}

} // namespace groovebox
