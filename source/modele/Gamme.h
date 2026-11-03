#pragma once

#include "Texte.h"

namespace groovebox
{

/** Gamme et fondamentale du morceau : les pads du Push ne jouent que ces notes. */
struct Gamme
{
    enum class Mode { majeur, mineur, dorien, phrygien, mixolydien, mineurHarmonique, pentatoniqueMineure };

    int fondamentale = 0;           ///< 0 = do, 1 = do dièse... 11 = si
    Mode mode = Mode::mineur;

    static juce::StringArray nomsDesModes();
    static juce::String nomDeLaNote (int demiTon);     ///< « Do », « Do♯ »...
    juce::String nom() const;                         ///< « Do mineur »

    /** Intervalles du mode, en demi-tons depuis la fondamentale. */
    const std::vector<int>& intervalles() const;
    int nombreDeDegres() const                      { return (int) intervalles().size(); }

    /** Note MIDI du degré d (0 = fondamentale) ; les degrés au-delà de la gamme montent d'octave. octaveDeBase : 0 = do 0 (MIDI 12). */
    int note (int degre, int octaveDeBase) const;

    bool contient (int noteMidi) const;
    bool estFondamentale (int noteMidi) const       { return ((noteMidi - fondamentale) % 12 + 12) % 12 == 0; }

    juce::var versJson() const;
    static Gamme depuisJson (const juce::var&);

    bool operator== (const Gamme&) const = default;
};

} // namespace groovebox
