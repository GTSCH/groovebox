#pragma once

#include "Mesure.h"
#include "Gamme.h"

namespace groovebox
{

struct Note;

/** Styles du générateur mélodique : chacun a ses règles (notes permises, rythme, accords) et ses réglages de départ. */
enum class StyleMelodique { basseDub, basseDnb, acid, hardTechno, stabsMinimal, arpegeElectro, nappe };
juce::String nomDuStyle (StyleMelodique);

/** La recette d'un clip mélodique généré : toujours dans la gamme du morceau, mêmes réglages et même graine, même phrase. */
struct GenerateurMelodique
{
    StyleMelodique style = StyleMelodique::basseDub;
    juce::uint32 graine = 1;
    float densite = 0.4f;           ///< Part des pas qui reçoivent une note
    int registre = 1;               ///< Octave de la fondamentale (1 : grave, 3 : médium)
    float mouvement = 0.3f;         ///< 0 : notes voisines ; 1 : grands sauts
    float longueur = 0.6f;          ///< Durée des notes, jusqu'à la note suivante
    float accents = 0.3f;
    float repetition = 0.7f;        ///< 0 : phrase longue ; 1 : motif court répété
    float glisses = 0.0f;           ///< Part des notes liées à la suivante (glissé sur un son monophonique)

    static GenerateurMelodique parDefaut (StyleMelodique);

    juce::var versJson() const;
    static GenerateurMelodique depuisJson (const juce::var&);

    bool operator== (const GenerateurMelodique&) const = default;
};

namespace melodies
{
    std::vector<Note> generer (const GenerateurMelodique&, const Mesure&, const Gamme&, int longueurMesures);

    /** Style qui convient à un son de Surge, selon son style (« Dub », « Acid »...). */
    StyleMelodique styleDuSon (const juce::String& styleDuSon, bool basse);
}

} // namespace groovebox
