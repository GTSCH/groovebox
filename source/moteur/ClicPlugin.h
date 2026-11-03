#pragma once

#include "modele/Mesure.h"

namespace groovebox
{
namespace te = tracktion;

/** Le clic du batteur : un petit instrument joué par des notes MIDI, une note par rôle de temps.
    Les notes viennent d'un clip MIDI d'une mesure, en boucle, calé sur l'horloge du moteur. */
class ClicPlugin  : public te::Plugin
{
public:
    static const char* xmlTypeName;

    static int noteDe (TypeDeTemps);
    static std::optional<TypeDeTemps> typeDeNote (int note);

    explicit ClicPlugin (te::PluginCreationInfo);
    ~ClicPlugin() override;

    juce::String getName() const override                       { return "Clic"; }
    juce::String getPluginType() override                       { return xmlTypeName; }
    juce::String getShortName (int) override                    { return getName(); }
    juce::String getSelectableDescription() override            { return getName(); }
    bool producesAudioWhenNoAudioInput() override               { return true; }
    bool isSynth() override                                     { return true; }
    bool takesMidiInput() override                              { return true; }
    int getNumOutputChannelsGivenInputs (int) override          { return 2; }

    void initialise (const te::PluginInitialisationInfo&) override;
    void deinitialise() override {}
    void applyToBuffer (const te::PluginRenderContext&) override;

    std::atomic<bool> actif { true };
    std::atomic<float> volume { 0.8f };

    /** Niveau de crête de chaque son de clic, volume à 1 (sert aussi aux essais). */
    static float niveauDe (TypeDeTemps);
    static double hauteurDe (TypeDeTemps);

private:
    struct Voix
    {
        int restant = 0;
        double phase = 0.0, increment = 0.0;
        float amplitude = 0.0f, decroissance = 1.0f;
    };

    std::array<Voix, 4> voix;
    size_t prochaineVoix = 0;
    double frequenceEchantillonnage = 48000.0;
    float gain = 1.0f;

    void declencher (TypeDeTemps, float velocite);
    void rendre (juce::AudioBuffer<float>&, int debut, int nombre, float& gainCourant, float pasGain, float cible);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ClicPlugin)
};

} // namespace groovebox
