#pragma once

#include "PageJeu.h"
#include "PageCarteSon.h"
#include "PageSession.h"
#include "PagePrises.h"
#include "PageRetouche.h"

namespace groovebox
{

/** La fenêtre : onglets « Session », « Jeu », « Prises », « Retouche » et « Carte son ». Barre d'espace : lecture et arrêt ; T : Tap Tempo. */
class FenetrePrincipale  : public juce::Component
{
public:
    FenetrePrincipale (te::Engine&, Moteur&, ActionsMorceaux, std::function<juce::String()> etatDuPush);

    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    juce::TabbedComponent onglets { juce::TabbedButtonBar::TabsAtTop };
    PageJeu* pageJeu = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FenetrePrincipale)
};

} // namespace groovebox
