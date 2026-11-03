#include "Palette.h"

namespace groovebox::palette
{

namespace
{
    // Rouge corail, orange, jaune, vert, turquoise, bleu, violet, rose
    const std::array<juce::Colour, 8> couleurs {{
        juce::Colour (0xffff5a4f), juce::Colour (0xffff9f2e), juce::Colour (0xfff2d43a), juce::Colour (0xff4fd16b),
        juce::Colour (0xff2fd0c4), juce::Colour (0xff3d8bff), juce::Colour (0xffa56bff), juce::Colour (0xffff5fb4)
    }};

    constexpr std::array<float, intensites> facteurs { 0.10f, 0.28f, 0.6f, 1.0f };
}

juce::Colour couleurDePiste (int piste)
{
    return couleurs[(size_t) juce::jlimit (0, 7, piste)];
}

int indexDePiste (int piste, int intensite)
{
    return premiereCouleurDePiste + juce::jlimit (0, 7, piste) * intensites + juce::jlimit (0, intensites - 1, intensite);
}

std::vector<std::pair<int, juce::Colour>> entreesProgrammees()
{
    std::vector<std::pair<int, juce::Colour>> entrees;

    for (int p = 0; p < 8; ++p)
        for (int i = 0; i < intensites; ++i)
        {
            const auto c = couleurs[(size_t) p];
            entrees.push_back ({ indexDePiste (p, i), juce::Colour::fromFloatRGBA (c.getFloatRed() * facteurs[(size_t) i],
                                                                                   c.getFloatGreen() * facteurs[(size_t) i],
                                                                                   c.getFloatBlue() * facteurs[(size_t) i], 1.0f) });
        }

    return entrees;
}

juce::Colour couleurDeLIndex (int index)
{
    if (index >= premiereCouleurDePiste && index < premiereCouleurDePiste + 8 * intensites)
    {
        const int p = (index - premiereCouleurDePiste) / intensites;
        const int i = (index - premiereCouleurDePiste) % intensites;
        return couleurs[(size_t) p].withMultipliedBrightness (facteurs[(size_t) i]);
    }

    switch (index)
    {
        case noir:       return juce::Colours::black;
        case blanc:      return juce::Colour (204, 204, 204);
        case grisClair:  return juce::Colour (64, 64, 64);
        case grisFonce:  return juce::Colour (20, 20, 20);
        case bleu:       return juce::Colour (0, 0, 255);
        case vert:       return juce::Colour (0, 255, 0);
        case rouge:      return juce::Colour (255, 0, 0);
        default:         break;
    }

    return juce::Colour (128, 128, 128);
}

float luminositeBlanche (int index)
{
    if (index <= 0)
        return 0.0f;

    return juce::jlimit (0.0f, 1.0f, 0.15f + 0.85f * (float) index / 127.0f);
}

} // namespace groovebox::palette
