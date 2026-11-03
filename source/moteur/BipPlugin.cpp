#include "BipPlugin.h"

namespace groovebox
{

const char* BipPlugin::xmlTypeName = "groovebox_bip";

BipPlugin::BipPlugin (te::PluginCreationInfo info)  : te::Plugin (info) {}

BipPlugin::~BipPlugin()
{
    notifyListenersOfDeletion();
}

void BipPlugin::initialise (const te::PluginInitialisationInfo& info)
{
    frequenceEchantillonnage = info.sampleRate;
}

void BipPlugin::applyToBuffer (const te::PluginRenderContext& fc)
{
    if (fc.destBuffer == nullptr)
        return;

    auto& sortie = *fc.destBuffer;
    const auto debut = fc.bufferStartSample;
    const auto n = fc.bufferNumSamples;

    // Bip de 250 ms toutes les 500 ms ; le gain glisse en 5 ms pour éviter les clics
    const auto periode = (juce::int64) (frequenceEchantillonnage * 0.5);
    const auto dureeBip = periode / 2;
    const auto pasGain = (float) (1.0 / (frequenceEchantillonnage * 0.005));
    const auto increment = juce::MathConstants<double>::twoPi * frequence.load() / frequenceEchantillonnage;
    const auto actifMaintenant = actif.load();
    const auto niveauMaintenant = niveau.load();

    for (int i = 0; i < n; ++i)
    {
        const bool dansLeBip = actifMaintenant && (position % periode) < dureeBip;
        const float cible = dansLeBip ? 1.0f : 0.0f;
        gain = gain < cible ? juce::jmin (cible, gain + pasGain) : juce::jmax (cible, gain - pasGain);

        const auto valeur = (float) std::sin (phase) * gain * niveauMaintenant;

        for (int canal = 0; canal < sortie.getNumChannels(); ++canal)
            sortie.addSample (canal, debut + i, valeur);

        phase += increment;

        if (phase >= juce::MathConstants<double>::twoPi)
            phase -= juce::MathConstants<double>::twoPi;

        ++position;
    }
}

} // namespace groovebox
