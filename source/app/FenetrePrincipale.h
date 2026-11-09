#pragma once

#include "PageJeu.h"
#include "PageCarteSon.h"
#include "PageSession.h"
#include "PagePrises.h"
#include "PageRetouche.h"
#include "PageExports.h"

namespace groovebox
{

/** La fenêtre : onglets « Session », « Jeu », « Prises », « Retouche », « Exports » et « Carte son ». Barre d'espace : lecture et arrêt ; T : Tap Tempo. */
class FenetrePrincipale  : public juce::Component,
                           public juce::FileDragAndDropTarget
{
public:
    FenetrePrincipale (te::Engine&, Moteur&, ActionsMorceaux, std::function<juce::String()> etatDuPush);

    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;
    void paintOverChildren (juce::Graphics&) override;

    // Glisser des sons sur la fenêtre : ils vont dans Sons, Mes sons, et le premier au sampler de la piste choisie
    bool isInterestedInFileDrag (const juce::StringArray&) override;
    void fileDragEnter (const juce::StringArray&, int, int) override   { survol = true; repaint(); }
    void fileDragExit (const juce::StringArray&) override              { survol = false; repaint(); }
    void filesDropped (const juce::StringArray&, int, int) override;

private:
    juce::TabbedComponent onglets { juce::TabbedButtonBar::TabsAtTop };
    PageJeu* pageJeu = nullptr;
    std::function<void (const juce::StringArray&)> deposerDesSons;
    bool survol = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FenetrePrincipale)
};

} // namespace groovebox
