#include "ClicPlugin.h"

namespace groovebox
{

namespace
{
    struct SonDeClic
    {
        int note;
        double frequence;
        float niveau;
    };

    // Premier temps aigu et fort, début de groupe moyen, autres temps doux, subdivisions très douces
    constexpr std::array<SonDeClic, 4> sons {{
        { 84, 2000.0, 1.00f },   // premier
        { 79, 1500.0, 0.70f },   // groupe
        { 76, 1000.0, 0.45f },   // autre
        { 72,  750.0, 0.22f }    // subdivision
    }};

    constexpr double dureeDuClic = 0.080;       // secondes, le son est alors retombé à presque rien
    constexpr double constanteDeDecroissance = 0.010;
}

const char* ClicPlugin::xmlTypeName = "groovebox_clic";

int ClicPlugin::noteDe (TypeDeTemps t)               { return sons[(size_t) t].note; }
float ClicPlugin::niveauDe (TypeDeTemps t)           { return sons[(size_t) t].niveau; }
double ClicPlugin::hauteurDe (TypeDeTemps t)         { return sons[(size_t) t].frequence; }

std::optional<TypeDeTemps> ClicPlugin::typeDeNote (int note)
{
    for (size_t i = 0; i < sons.size(); ++i)
        if (sons[i].note == note)
            return (TypeDeTemps) i;

    return std::nullopt;
}

ClicPlugin::ClicPlugin (te::PluginCreationInfo info)  : te::Plugin (info) {}

ClicPlugin::~ClicPlugin()
{
    notifyListenersOfDeletion();
}

void ClicPlugin::initialise (const te::PluginInitialisationInfo& info)
{
    frequenceEchantillonnage = info.sampleRate;

    for (auto& v : voix)
        v.restant = 0;
}

void ClicPlugin::declencher (TypeDeTemps type, float velocite)
{
    auto& son = sons[(size_t) type];
    auto& v = voix[prochaineVoix];
    prochaineVoix = (prochaineVoix + 1) % voix.size();

    v.restant = (int) (dureeDuClic * frequenceEchantillonnage);
    v.phase = 0.0;
    v.increment = juce::MathConstants<double>::twoPi * son.frequence / frequenceEchantillonnage;
    v.amplitude = son.niveau * velocite;
    v.decroissance = (float) std::exp (-1.0 / (constanteDeDecroissance * frequenceEchantillonnage));
}

void ClicPlugin::rendre (juce::AudioBuffer<float>& sortie, int debut, int nombre, float& gainCourant, float pasGain, float cible)
{
    const auto vol = volume.load();

    for (int i = debut; i < debut + nombre; ++i)
    {
        gainCourant = gainCourant < cible ? juce::jmin (cible, gainCourant + pasGain) : juce::jmax (cible, gainCourant - pasGain);

        float valeur = 0.0f;

        for (auto& v : voix)
        {
            if (v.restant <= 0)
                continue;

            valeur += (float) std::sin (v.phase) * v.amplitude;
            v.phase += v.increment;
            v.amplitude *= v.decroissance;
            --v.restant;
        }

        valeur *= vol * gainCourant;

        for (int canal = 0; canal < sortie.getNumChannels(); ++canal)
            sortie.addSample (canal, i, valeur);
    }
}

void ClicPlugin::applyToBuffer (const te::PluginRenderContext& fc)
{
    if (fc.destBuffer == nullptr)
        return;

    auto& sortie = *fc.destBuffer;
    const int debut = fc.bufferStartSample;
    const int n = fc.bufferNumSamples;

    // Marche et arrêt du clic en 5 ms, sans clic parasite
    const float cible = actif.load() ? 1.0f : 0.0f;
    const float pasGain = (float) (1.0 / (frequenceEchantillonnage * 0.005));
    int position = 0;

    if (fc.bufferForMidiMessages != nullptr)
    {
        for (auto& m : *fc.bufferForMidiMessages)
        {
            if (! m.isNoteOn())
                continue;

            const auto type = typeDeNote (m.getNoteNumber());

            if (! type)
                continue;

            const int instant = juce::jlimit (0, n, juce::roundToInt (m.getTimeStamp() * frequenceEchantillonnage));

            if (instant > position)
            {
                rendre (sortie, debut + position, instant - position, gain, pasGain, cible);
                position = instant;
            }

            declencher (*type, m.getFloatVelocity());
        }
    }

    if (position < n)
        rendre (sortie, debut + position, n - position, gain, pasGain, cible);
}

} // namespace groovebox
