#pragma once

#include "Mesure.h"

namespace groovebox
{

struct ReglagesClic
{
    bool actif = true;
    float volume = 0.8f;              ///< 0 à 1
    bool subdivisions = false;
    bool unClicParGroupe = false;
    int decompte = 1;                 ///< 0, 1 ou 2 mesures avant le départ
    bool ecouteSurMac = false;        ///< Essai sans carte son : clic sur les sorties 1-2. Ignoré dès que la carte a des sorties 3-4.

    bool operator== (const ReglagesClic&) const = default;
};

/** Une scène : rangée de clips lancés ensemble (étape 4). Mesure et tempo propres prévus pour plus tard, inactifs. */
struct Scene
{
    juce::String nom;
    std::optional<Mesure> mesure;
    std::optional<double> tempo;

    bool operator== (const Scene&) const = default;
};

/** Le morceau : tout ce qui se sauvegarde, en JSON lisible. */
struct Morceau
{
    static constexpr double tempoMin = 60.0, tempoMax = 200.0;
    static constexpr int versionDuFormat = 1;

    juce::String nom { "Sans titre" };
    double tempo = 120.0;             ///< BPM, toujours à la noire
    Mesure mesure;
    ReglagesClic clic;
    float musiqueDansCasqueDb = 0.0f; ///< Niveau de la musique dans le casque du batteur
    std::vector<Scene> scenes;

    juce::var versJson() const;
    static Morceau depuisJson (const juce::var&);

    bool enregistrer (const juce::File&) const;
    static std::optional<Morceau> charger (const juce::File&);

    bool operator== (const Morceau&) const = default;
};

} // namespace groovebox
