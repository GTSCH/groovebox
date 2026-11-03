#include "FenetrePrincipale.h"
#include "Style.h"

namespace groovebox
{

FenetrePrincipale::FenetrePrincipale (te::Engine& engine, Moteur& moteur,
                                      ActionsMorceaux actions, std::function<juce::String()> etatDuPush)
{
    pageJeu = new PageJeu (moteur);
    pageJeu->ouvrirSimulateur = actions.ouvrirSimulateur;
    pageJeu->etatDuPush = std::move (etatDuPush);
    onglets.addTab ("Session", couleurFond, new PageSession (moteur, std::move (actions)), true);
    onglets.addTab ("Jeu", couleurFond, pageJeu, true);
    onglets.addTab ("Prises", couleurFond, new PagePrises (engine, moteur), true);
    onglets.addTab ("Retouche", couleurFond, new PageRetouche (engine, moteur), true);
    onglets.addTab ("Carte son", couleurFond, new PageCarteSon (engine, moteur), true);
    onglets.setTabBarDepth (40);
    onglets.setOutline (0);
    onglets.getTabbedButtonBar().setWantsKeyboardFocus (false);
    addAndMakeVisible (onglets);

    setWantsKeyboardFocus (true);
    setSize (980, 680);
}

void FenetrePrincipale::resized()
{
    onglets.setBounds (getLocalBounds());
}

bool FenetrePrincipale::keyPressed (const juce::KeyPress& touche)
{
    if (touche == juce::KeyPress::spaceKey)
    {
        pageJeu->lectureOuArret();
        return true;
    }

    if (touche.getTextCharacter() == 't' || touche.getTextCharacter() == 'T')
    {
        pageJeu->taperLeTempo();
        return true;
    }

    return false;
}

} // namespace groovebox
