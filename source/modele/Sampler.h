#pragma once

#include "Mesure.h"

namespace groovebox
{

/** Comment le sampler joue son son. */
enum class ModeSampler
{
    unCoup,         ///< Le son part en entier, transposé par la note ; le relâché ne l'arrête pas
    notes,          ///< Joué en gamme sur les pads ; le relâché l'éteint avec la relâche
    decoupe,        ///< 16 tranches égales, une par pad (notes 36 à 51), comme une batterie
    boucle          ///< Boucle étirée au tempo du morceau, sans changer sa hauteur
};

constexpr int nombreDeModesSampler = 4;
juce::String nomDuModeSampler (ModeSampler);

/** Réglages du sampler d'une piste : le son (dans le dossier Sons) et ses 8 réglages. */
struct ReglagesSampler
{
    static constexpr int nombreDeTranches = 16, premiereNote = 36, noteDeBase = 60;

    juce::String son;               ///< Chemin relatif au dossier Sons, par exemple « Boucles/Breakbeat 100.wav »
    ModeSampler mode = ModeSampler::unCoup;
    float debut = 0.0f;             ///< 0 à 1 : début de la partie jouée, dans le son
    float longueur = 1.0f;          ///< 0 à 1 : longueur de la partie jouée, en part de ce qui reste après le début
    float hauteur = 0.0f;           ///< Demi-tons, de -24 à +24
    float filtre = 0.0f;            ///< -1 passe-bas, 0 neutre, +1 passe-haut
    float attaque = 0.0f;           ///< 0 à 1 (0 : immédiate, 1 : 2 secondes)
    float relache = 0.2f;           ///< 0 à 1 (0 : 5 ms, 1 : 4 secondes)
    bool envers = false;
    float volumeDb = 0.0f;
    int mesuresDeBoucle = 0;        ///< Boucle : sa longueur en mesures ; 0 : devinée d'après sa durée et le tempo

    /** Début et fin de la partie jouée, en part du son (0 à 1). */
    std::pair<double, double> partieJouee() const;

    /** Attaque et relâche en secondes. */
    double attaqueEnSecondes() const;
    double relacheEnSecondes() const;

    juce::var versJson() const;
    static ReglagesSampler depuisJson (const juce::var&);

    bool operator== (const ReglagesSampler&) const = default;
};

namespace sampler
{
    /** Tempo écrit dans le nom d'un son (« Jungle 170 » : 170), ou 0. */
    double tempoDansLeNom (const juce::String& chemin);

    /** Longueur d'une boucle en mesures : celle choisie ; sinon d'après le tempo écrit dans son nom ; sinon 1, 2, 4, 8
        ou 16 selon ce qui donne le tempo d'origine le plus proche du tempo du morceau (4 s à 120 en 4/4 : 2 mesures). */
    int mesuresDeLaBoucle (const ReglagesSampler&, double secondesDeLaPartie, double tempo, const Mesure&);

    /** Mode proposé pour un son selon son dossier : Boucles en boucle, Voix et One-shots en un coup, Textures en notes. */
    ModeSampler modePourLaCategorie (const juce::String& categorie);

    /** Dossiers créés pour le sampler dans Sons. */
    juce::StringArray categoriesDuSampler();
}

} // namespace groovebox
