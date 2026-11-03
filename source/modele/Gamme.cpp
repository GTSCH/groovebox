#include "Gamme.h"

namespace groovebox
{

namespace
{
    const juce::StringArray clesDesModes { "majeur", "mineur", "dorien", "phrygien", "mixolydien", "mineurHarmonique", "pentatoniqueMineure" };
}

juce::StringArray Gamme::nomsDesModes()
{
    return { "majeur", "mineur", "dorien", "phrygien", "mixolydien", "mineur harmonique", "pentatonique mineure" };
}

juce::String Gamme::nomDeLaNote (int demiTon)
{
    static const juce::StringArray noms { "Do", "Do#", "Ré"_fr, "Mi b", "Mi", "Fa", "Fa#", "Sol", "La b", "La", "Si b", "Si" };
    return noms[((demiTon % 12) + 12) % 12];
}

juce::String Gamme::nom() const
{
    return nomDeLaNote (fondamentale) + " " + nomsDesModes()[(int) mode];
}

const std::vector<int>& Gamme::intervalles() const
{
    static const std::vector<int> majeur { 0, 2, 4, 5, 7, 9, 11 }, mineur { 0, 2, 3, 5, 7, 8, 10 }, dorien { 0, 2, 3, 5, 7, 9, 10 },
                                  phrygien { 0, 1, 3, 5, 7, 8, 10 }, mixolydien { 0, 2, 4, 5, 7, 9, 10 }, harmonique { 0, 2, 3, 5, 7, 8, 11 },
                                  pentatonique { 0, 3, 5, 7, 10 };

    switch (mode)
    {
        case Mode::majeur:              return majeur;
        case Mode::mineur:              return mineur;
        case Mode::dorien:              return dorien;
        case Mode::phrygien:            return phrygien;
        case Mode::mixolydien:          return mixolydien;
        case Mode::mineurHarmonique:    return harmonique;
        case Mode::pentatoniqueMineure: return pentatonique;
    }

    return mineur;
}

int Gamme::note (int degre, int octaveDeBase) const
{
    const int n = nombreDeDegres();
    const int octave = (int) std::floor ((double) degre / n);
    const int dansLaGamme = degre - octave * n;
    return 12 * (octaveDeBase + 1 + octave) + fondamentale + intervalles()[(size_t) dansLaGamme];
}

bool Gamme::contient (int noteMidi) const
{
    const int ecart = ((noteMidi - fondamentale) % 12 + 12) % 12;
    const auto& i = intervalles();
    return std::find (i.begin(), i.end(), ecart) != i.end();
}

juce::var Gamme::versJson() const
{
    auto o = new juce::DynamicObject();
    o->setProperty ("fondamentale", fondamentale);
    o->setProperty ("mode", clesDesModes[(int) mode]);
    return juce::var (o);
}

Gamme Gamme::depuisJson (const juce::var& v)
{
    Gamme g;
    g.fondamentale = juce::jlimit (0, 11, (int) v.getProperty ("fondamentale", 0));

    if (const auto i = clesDesModes.indexOf (v.getProperty ("mode", {}).toString()); i >= 0)
        g.mode = (Mode) i;

    return g;
}

} // namespace groovebox
