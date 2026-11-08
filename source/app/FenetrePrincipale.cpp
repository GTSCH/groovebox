#include "FenetrePrincipale.h"
#include "Style.h"

namespace groovebox
{

FenetrePrincipale::FenetrePrincipale (te::Engine& engine, Moteur& moteur,
                                      ActionsMorceaux actions, std::function<juce::String()> etatDuPush)
{
    deposerDesSons = actions.deposerDesSons;
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

bool FenetrePrincipale::isInterestedInFileDrag (const juce::StringArray& fichiers)
{
    for (auto& f : fichiers)
        if (BanqueDeSons::estUnSon (juce::File (f)))
            return true;

    return false;
}

void FenetrePrincipale::filesDropped (const juce::StringArray& fichiers, int, int)
{
    survol = false;
    repaint();

    if (deposerDesSons)
        deposerDesSons (fichiers);
}

void FenetrePrincipale::paintOverChildren (juce::Graphics& g)
{
    if (! survol)
        return;

    auto zone = getLocalBounds().reduced (24).toFloat();
    g.setColour (juce::Colours::black.withAlpha (0.6f));
    g.fillRoundedRectangle (zone, 12.0f);
    g.setColour (juce::Colours::white);
    g.drawRoundedRectangle (zone, 12.0f, 3.0f);
    g.setFont (juce::FontOptions (28.0f, juce::Font::bold));
    g.drawFittedText ("Lâcher ici : le son va dans Sons, « Mes sons »,\net dans le sampler de la piste choisie au Push"_fr,
                      zone.reduced (30.0f).toNearestInt(), juce::Justification::centred, 3);
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
