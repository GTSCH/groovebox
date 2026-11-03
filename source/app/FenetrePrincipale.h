#pragma once

#include "moteur/Moteur.h"

namespace groovebox
{

/** Fenêtre de l'étape 1 : choix de la carte son, sons de test, état du moteur. */
class FenetrePrincipale  : public juce::Component,
                           private juce::Timer,
                           private juce::ChangeListener,
                           private juce::AudioIODeviceType::Listener
{
public:
    FenetrePrincipale (te::Engine&, Moteur&);
    ~FenetrePrincipale() override;

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

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FenetrePrincipale)
};

} // namespace groovebox
