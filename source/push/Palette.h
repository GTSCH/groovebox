#pragma once

#include "Texte.h"

namespace groovebox
{

/** Couleurs des LED. Les 8 couleurs de piste, en 4 intensités, occupent les entrées 64 à 95 de la palette du Push ;
    les autres entrées gardent la palette d'origine d'Ableton (dont les blancs 16 et 48 et les couleurs 122 à 127). */
namespace palette
{
    constexpr int noir = 0;
    constexpr int blancFaible = 16, blancMoyen = 48, blancFort = 127;      // LED blanches
    constexpr int grisFonce = 124, grisClair = 123, blanc = 122, bleu = 125, vert = 126, rouge = 127;
    constexpr int premiereCouleurDePiste = 64, intensites = 4;

    /** Couleur de la piste 0 à 7, reprise partout (pads, écran du Push, écran du Mac). */
    juce::Colour couleurDePiste (int piste);

    /** Index de palette d'une couleur de piste, intensité 0 (faible) à 3 (pleine). */
    int indexDePiste (int piste, int intensite);

    /** Couleur que montre une LED couleur pour cet index (sert au simulateur). */
    juce::Colour couleurDeLIndex (int index);

    /** Luminosité d'une LED blanche pour cet index, de 0 à 1. */
    float luminositeBlanche (int index);

    /** Entrées que groovebox programme dans le Push : (index, couleur). */
    std::vector<std::pair<int, juce::Colour>> entreesProgrammees();
}

} // namespace groovebox
