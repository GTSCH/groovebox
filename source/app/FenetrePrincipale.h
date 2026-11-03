#pragma once

#include "PageJeu.h"
#include "PageCarteSon.h"

namespace groovebox
{

/** La fenêtre : un onglet « Jeu » et un onglet « Carte son ». Barre d'espace : lecture et arrêt ; T : Tap Tempo. */
class FenetrePrincipale  : public juce::Component
{
public:
    FenetrePrincipale (te::Engine&, Moteur&);

    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    juce::TabbedComponent onglets { juce::TabbedButtonBar::TabsAtTop };
    PageJeu* pageJeu = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FenetrePrincipale)
};

} // namespace groovebox
