#include "FenetrePrincipale.h"
#include "Style.h"

namespace groovebox
{

FenetrePrincipale::FenetrePrincipale (te::Engine& engine, Moteur& moteur)
{
    pageJeu = new PageJeu (moteur);
    onglets.addTab ("Jeu", couleurFond, pageJeu, true);
    onglets.addTab ("Carte son", couleurFond, new PageCarteSon (engine, moteur), true);
    onglets.setTabBarDepth (40);
    onglets.setOutline (0);
    onglets.getTabbedButtonBar().setWantsKeyboardFocus (false);
    addAndMakeVisible (onglets);

    setWantsKeyboardFocus (true);
    setSize (820, 660);
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
