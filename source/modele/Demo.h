#pragma once

#include "Morceau.h"

namespace groovebox
{

/** Morceau de démonstration en 4/4 à 120 BPM, en do mineur : de quoi jouer la Session avant l'arrivée des générateurs. */
Morceau morceauDeDemonstration();

/** Notes de la batterie provisoire. */
namespace notesBatterie
{
    constexpr int grosseCaisse = 36, rimshot = 37, caisseClaire = 38, clap = 39, charleston = 42, tom = 45, charlestonOuvert = 46;
}

} // namespace groovebox
