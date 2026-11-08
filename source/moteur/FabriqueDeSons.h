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

    /** Sons de départ du sampler : boucles de batterie à plusieurs tempos, textures et sons en do (note 60),
        sons rigolos, et voix dites par la synthèse vocale du Mac (si elle répond). Seuls les fichiers manquants sont écrits. */
    int assurerSonsDuSampler (const juce::File& dossierSons, bool avecVoix = true);

    /** Les boucles de départ : nom de fichier, tempo d'origine, mesure, nombre de mesures. */
    struct BoucleDeDepart
    {
        juce::String nom;
        double tempo;
        int numerateur, denominateur, mesures;
    };

    const std::vector<BoucleDeDepart>& bouclesDeDepart();
}

} // namespace groovebox
