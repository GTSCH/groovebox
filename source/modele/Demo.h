#pragma once

#include "Deroule.h"

namespace groovebox
{

/** Morceau de démonstration en 4/4 à 120 BPM, en do mineur : de quoi jouer la Session avant l'arrivée des générateurs. */
Morceau morceauDeDemonstration();

/** Démo 2 : en 6/8 (3+3), à 120 à la noire (la mesure dure 1,5 s), en ré mineur, lourde et puissante.
    Six scènes : Intro, Approche (les toms arrivent), Batterie, Construction, Sommet, Fin. */
Morceau morceauDeDemonstration2();

/** Le déroulé de la Démo 2 : le morceau d'un bout à l'autre (36 mesures, 54 secondes),
    avec ses enchaînements, ses montées et des réglages qui s'ouvrent peu à peu. */
Deroule derouleDeLaDemo2 (const Morceau&);

/** Notes de la batterie provisoire. */
namespace notesBatterie
{
    constexpr int grosseCaisse = 36, rimshot = 37, caisseClaire = 38, clap = 39, charleston = 42, tom = 45, charlestonOuvert = 46;
}

} // namespace groovebox
