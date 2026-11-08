#pragma once

#include "Mesure.h"

namespace groovebox
{

struct Note;

/** Les quatre moteurs de rythme ; « batteur » joue des grooves pensés comme un batteur (batterie guide). */
enum class MoteurRythmique { euclidien, grooves, maison, batteur };
constexpr int nombreDeMoteursRythmiques = 4;
juce::String nomDuMoteur (MoteurRythmique);

/** Les grooves du moteur batteur. Chacun a sa mesure : rock, breakbeat, jungle et dub en 4/4, shuffle en 6/8 ou 12/8,
    valse en 3/4 ; « groupé » suit le groupement de n'importe quelle mesure (7/8 en 2+2+3, 9/8 en 2+2+2+3...). */
enum class StyleDeBatteur { rock, breakbeat, jungle, dub, shuffle, valse, groupe };
constexpr int nombreDeStylesDeBatteur = 7;
juce::String nomDuStyleDeBatteur (StyleDeBatteur);

/** Cymbale qui marque le temps dans les grooves du batteur. */
enum class CymbaleDeBatteur { charleston, charlestonOuvert, ride, cloche };
constexpr int nombreDeCymbalesDeBatteur = 4;
juce::String nomDeLaCymbale (CymbaleDeBatteur);

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

    // Batteur : le groove, l'espace laissé au vrai batteur, les notes fantômes, la cymbale, le crash
    StyleDeBatteur style = StyleDeBatteur::rock;
    float espace = 0.0f;            ///< 0 : le groove complet ; 1 : grosse caisse sur le premier temps et contretemps seulement
    float fantomes = 0.5f;          ///< 0 à 1 : notes fantômes de caisse claire
    CymbaleDeBatteur cymbale = CymbaleDeBatteur::charleston;
    int crash = 4;                  ///< Crash au début du clip puis toutes les 4, 8 ou 16 mesures ; 0 : aucun

    // Commun aux quatre moteurs
    float humanisation = 0.0f;      ///< 0 à 1 : vélocité et léger retard, jamais en avance
    int roulement = 0;              ///< 0 : aucun ; sinon un roulement de caisse claire (des toms pour le batteur) toutes les 2, 4 ou 8 mesures

    /** Nom d'un clip généré : le moteur, ou le style du batteur. */
    juce::String nomDuClip() const;

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

    /** Styles du batteur jouables dans une mesure ; le premier est celui par défaut. */
    std::vector<StyleDeBatteur> stylesPourLaMesure (const Mesure&);

    /** Style réellement joué : celui choisi s'il va avec la mesure, sinon le premier qui va. */
    StyleDeBatteur styleJoue (StyleDeBatteur, const Mesure&);

    /** Les notes du clip, sur toute sa longueur. */
    std::vector<Note> generer (const Generateur&, const Mesure&, int longueurMesures);

    /** Variation de notes déjà figées (rotation, inversion, densité, quantité), déterministe pour une graine. */
    std::vector<Note> varier (const std::vector<Note>&, const Variation&, juce::uint32 graine, const Mesure&, int longueurMesures);
}

} // namespace groovebox
