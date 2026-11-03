#pragma once

#include "moteur/Moteur.h"

namespace groovebox
{

/** Onglet « Carte son » : choix de la carte, sons de test, état du moteur. */
class PageCarteSon  : public juce::Component,
                           private juce::Timer,
                           private juce::ChangeListener,
                           private juce::AudioIODeviceType::Listener
{
public:
    PageCarteSon (te::Engine&, Moteur&);
    ~PageCarteSon() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    te::Engine& engine;
    Moteur& moteur;

    juce::Label titre, etat, aideCasque;
    juce::Label libelleSortie, libelleFrequence, libelleTampon;
    juce::ComboBox choixSortie, choixFrequence, choixTampon;
    juce::TextButton boutonBip { "Son de test (sorties 1-2)" };
    juce::TextButton boutonCasque { "Test casque (sorties 3-4)" };

    te::LevelMeasurer::Client mesureNiveau;
    float niveauAffiche = 0.0f;
    juce::Rectangle<int> zoneNiveau;

    void mettreAJourBoutons();
    void mettreAJourChoixCarte();
    void changerCarte (const std::function<void (juce::AudioDeviceManager::AudioDeviceSetup&)>&);
    void changeListenerCallback (juce::ChangeBroadcaster*) override       { mettreAJourChoixCarte(); }
    void audioDeviceListChanged() override                              { mettreAJourChoixCarte(); }
    void timerCallback() override;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PageCarteSon)
};

} // namespace groovebox
