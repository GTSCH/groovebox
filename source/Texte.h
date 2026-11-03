#pragma once

#include <JuceHeader.h>

namespace groovebox
{

/** Texte accentué écrit dans le code : "Décompte"_fr. Sans cela, JUCE lirait les accents de travers. */
inline juce::String operator""_fr (const char* texte, size_t)
{
    return juce::String::fromUTF8 (texte);
}

} // namespace groovebox
