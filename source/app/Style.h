#pragma once

#include <JuceHeader.h>

namespace groovebox
{

// Couleurs communes aux onglets
inline const juce::Colour couleurFond     { 0xff15171c };
inline const juce::Colour couleurPanneau  { 0xff2a2d35 };
inline const juce::Colour couleurTexte    { 0xffe8e8ea };
inline const juce::Colour couleurDiscret  { 0xff9a9ca3 };
inline const juce::Colour couleurActif    { 0xff3fb27f };
inline const juce::Colour couleurPremier  { 0xffff9f43 };
inline const juce::Colour couleurGroupe   { 0xfff5d76e };

inline juce::String formaterLatence (int tampon, double frequence)
{
    return juce::String (1000.0 * tampon / frequence, 1).replaceCharacter ('.', ',') + " ms";
}

/** 48000 devient « 48 000 », à la française. */
inline juce::String formaterNombre (double valeur)
{
    auto texte = juce::String ((juce::int64) std::round (valeur));
    juce::String resultat;

    for (int i = 0; i < texte.length(); ++i)
    {
        if (i > 0 && (texte.length() - i) % 3 == 0)
            resultat << " ";

        resultat << texte[i];
    }

    return resultat;
}

} // namespace groovebox
