#pragma once

#include <JuceHeader.h>

namespace groovebox
{
namespace te = tracktion;

/** Son de test : un bip régulier (un quart de seconde sur deux), sans clic à l'attaque ni à la fin.
    Les réglages se changent depuis n'importe quel fil, sans bloquer le fil audio. */
class BipPlugin  : public te::Plugin
{
public:
    static const char* xmlTypeName;

    explicit BipPlugin (te::PluginCreationInfo);
    ~BipPlugin() override;

    juce::String getName() const override                       { return "Bip de test"; }
    juce::String getPluginType() override                       { return xmlTypeName; }
    juce::String getShortName (int) override                    { return getName(); }
    juce::String getSelectableDescription() override            { return getName(); }
    bool producesAudioWhenNoAudioInput() override               { return true; }
    bool isSynth() override                                     { return true; }
    int getNumOutputChannelsGivenInputs (int) override          { return 2; }

    void initialise (const te::PluginInitialisationInfo&) override;
    void deinitialise() override {}
    void applyToBuffer (const te::PluginRenderContext&) override;

    std::atomic<bool> actif { false };
    std::atomic<float> frequence { 440.0f };
    std::atomic<float> niveau { 0.25f };

private:
    double frequenceEchantillonnage = 48000.0;
    double phase = 0.0;
    juce::int64 position = 0;
    float gain = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BipPlugin)
};

} // namespace groovebox
