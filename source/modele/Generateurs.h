#pragma once

#include "Mesure.h"

namespace groovebox
{

struct Note;

/** Les trois moteurs de rythme. */
enum class MoteurRythmique { euclidien, grooves, maison };
juce::String nomDuMoteur (MoteurRythmique);

/** Une voix du moteur euclidien : coups répartis le plus régulièrement possible dans la mesure. */
struct VoixEuclidienne
{
    int coups = 0;
    int rotation = 0;
    int accents = 0;            ///< Coups accentués, répartis eux aussi régulièrement parmi les coups
    float probabilite = 1.0f;   ///< Chance que chaque coup sonne

    bool operator== (const VoixEuclidienne&) const = default;
};

/** La recette d'un clip de batterie généré : mêmes réglages et même graine, toujours le même rythme. */
struct Generateur
{
    static constexpr int nombreDeVoixEuclidiennes = 4;          ///< Grosse caisse, caisse claire, charleston, clap
    static constexpr std::array<int, 4> notesEuclidiennes { 36, 38, 42, 39 };
    static constexpr std::array<int, 3> notesDesCartes { 36, 38, 42 };

    MoteurRythmique moteur = MoteurRythmique::euclidien;
    juce::uint32 graine = 1;
    std::array<VoixEuclidienne, nombreDeVoixEuclidiennes> voix;

    // Cartes (grooves et maison) : position, densité par voix, chaos
    float x = 0.5f, y = 0.5f;
    std::array<float, 3> densites { 0.6f, 0.45f, 0.6f };
    float chaos = 0.0f;

    // Commun aux trois moteurs
    float humanisation = 0.0f;      ///< 0 à 1 : vélocité et léger retard, jamais en avance
    int roulement = 0;              ///< 0 : aucun ; sinon un roulement de caisse claire toutes les 2, 4 ou 8 mesures

    /** Réglages de départ qui sonnent tout de suite, adaptés à la mesure. */
    static Generateur parDefaut (MoteurRythmique, const Mesure&);

    juce::var versJson() const;
    static Generateur depuisJson (const juce::var&);

    bool operator== (const Generateur&) const = default;
};

/** Réglages de « dupliquer et varier ». */
struct Variation
{
    float quantite = 0.25f;     ///< Part des pas modifiés
    float densite = 0.0f;       ///< -1 moins de coups, +1 plus de coups
    int rotation = 0;           ///< En double-croches
    bool inversion = false;     ///< Rétrograde : le rythme à l'envers

    bool operator== (const Variation&) const = default;
};

namespace generateurs
{
    /** Pas de double-croche en temps du moteur, et nombre de ces pas par mesure. */
    double pas (const Mesure&);
    int pasParMesure (const Mesure&);

    /** Motif euclidien : coups répartis sur n pas, décalés de « rotation » pas vers la droite. */
    std::vector<bool> euclide (int coups, int n, int rotation = 0);

    /** La carte de grooves n'existe qu'en 4/4 (elle vient de Grids). */
    bool carteDeGroovesDisponible (const Mesure&);

    /** Niveau (0 à 255) de chaque voix (grosse caisse, caisse claire, charleston) à un pas, sur l'une des deux cartes. */
    std::array<int, 3> niveauGrooves (int pas32, float x, float y);
    std::array<int, 3> niveauMaison (const Mesure&, int pas, float x, float y);

    /** Les notes du clip, sur toute sa longueur. */
    std::vector<Note> generer (const Generateur&, const Mesure&, int longueurMesures);

    /** Variation de notes déjà figées (rotation, inversion, densité, quantité), déterministe pour une graine. */
    std::vector<Note> varier (const std::vector<Note>&, const Variation&, juce::uint32 graine, const Mesure&, int longueurMesures);
}

} // namespace groovebox
