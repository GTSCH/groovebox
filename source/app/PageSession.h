#pragma once

#include "moteur/Moteur.h"

namespace groovebox
{

/** Ce que l'onglet Session peut demander à l'application : changer de morceau. */
struct ActionsMorceaux
{
    std::function<juce::StringArray()> noms;
    std::function<void (const juce::String&)> ouvrir;
    std::function<void()> nouveau;
    std::function<void()> demo;
    std::function<void()> demo2;
    std::function<void()> ouvrirSimulateur;
    std::function<void (const juce::StringArray&)> deposerDesSons;    ///< Fichiers son glissés sur la fenêtre
};

/** Onglet « Session » : la grille des clips telle qu'elle joue, pour voir et dépanner (le jeu se fait au Push). */
class PageSession  : public juce::Component,
                     private juce::Timer
{
public:
    PageSession (Moteur&, ActionsMorceaux);

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    Moteur& moteur;
    ActionsMorceaux actions;

    juce::Label nomDuMorceau, infos;
    juce::TextButton boutonNouveau { "Nouveau" }, boutonOuvrir { juce::String::fromUTF8 ("Ouvrir…") },
                     boutonRenommer { juce::String::fromUTF8 ("Renommer…") }, boutonDemo { juce::String::fromUTF8 ("Démo") }, boutonDemo2 { juce::String::fromUTF8 ("Démo 2") },
                     boutonSimulateur { "Simulateur du Push" };
    juce::Rectangle<int> zoneGrille;

    void renommer();
    void timerCallback() override;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PageSession)
};

} // namespace groovebox
