#pragma once

#include "modele/Morceau.h"

namespace groovebox
{
namespace te = tracktion;

/** Son provisoire d'une piste, en attendant les vraies machines : une batterie fabriquée par le calcul
    (notes 36 à 46) ou un petit synthé réglé selon la piste (basse, nappe, lead...). Aucun fichier de son. */
class InstrumentProvisoire  : public te::Plugin
{
public:
    static const char* xmlTypeName;

    explicit InstrumentProvisoire (te::PluginCreationInfo);
    ~InstrumentProvisoire() override;

    juce::String getName() const override                       { return "Instrument provisoire"; }
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

    std::atomic<int> son { (int) SonProvisoire::nappe };

private:
    struct Voix
    {
        bool active = false, relachee = false;
        int note = 0;
        float velocite = 1.0f;
        double phase = 0.0, phase2 = 0.0, frequence = 440.0;
        double age = 0.0;               ///< Secondes depuis l'attaque
        float enveloppe = 0.0f, niveauAuRelache = 0.0f;
        double ageAuRelache = 0.0;
        float v1 = 0.0f, v2 = 0.0f;     ///< Mémoire du filtre
        juce::uint32 bruit = 12345;
    };

    std::array<Voix, 12> voix;
    double frequenceEchantillonnage = 48000.0;

    void noteOn (int note, float velocite);
    void noteOff (int note);
    void rendre (juce::AudioBuffer<float>&, int debut, int nombre);
    float echantillonBatterie (Voix&);
    float echantillonSynthe (Voix&, int reglage);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (InstrumentProvisoire)
};

} // namespace groovebox
