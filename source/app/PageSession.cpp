#include "PageSession.h"
#include "Style.h"
#include "push/Palette.h"

namespace groovebox
{

PageSession::PageSession (Moteur& m, ActionsMorceaux a)  : moteur (m), actions (std::move (a))
{
    nomDuMorceau.setFont (juce::FontOptions (30.0f, juce::Font::bold));
    nomDuMorceau.setColour (juce::Label::textColourId, couleurTexte);
    addAndMakeVisible (nomDuMorceau);

    infos.setFont (juce::FontOptions (16.0f));
    infos.setColour (juce::Label::textColourId, couleurDiscret);
    addAndMakeVisible (infos);

    boutonNouveau.onClick = [this] { if (actions.nouveau) actions.nouveau(); };
    boutonDemo.onClick = [this] { if (actions.demo) actions.demo(); };
    boutonRenommer.onClick = [this] { renommer(); };
    boutonSimulateur.onClick = [this] { if (actions.ouvrirSimulateur) actions.ouvrirSimulateur(); };
    boutonOuvrir.onClick = [this]
    {
        juce::PopupMenu menu;
        const auto noms = actions.noms ? actions.noms() : juce::StringArray();

        for (int i = 0; i < noms.size(); ++i)
            menu.addItem (i + 1, noms[i], true, noms[i] == moteur.getMorceau().nom);

        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&boutonOuvrir), [this, noms] (int choix)
        {
            if (choix > 0 && actions.ouvrir)
                actions.ouvrir (noms[choix - 1]);
        });
    };

    for (auto* b : { &boutonNouveau, &boutonOuvrir, &boutonRenommer, &boutonDemo, &boutonSimulateur })
    {
        b->setWantsKeyboardFocus (false);
        addAndMakeVisible (*b);
    }

    startTimerHz (20);
}

void PageSession::renommer()
{
    auto fenetre = std::make_shared<juce::AlertWindow> ("Renommer le morceau", "Nouveau nom du morceau :", juce::MessageBoxIconType::NoIcon);
    fenetre->addTextEditor ("nom", moteur.getMorceau().nom);
    fenetre->addButton ("Renommer", 1, juce::KeyPress (juce::KeyPress::returnKey));
    fenetre->addButton ("Annuler", 0, juce::KeyPress (juce::KeyPress::escapeKey));

    fenetre->enterModalState (true, juce::ModalCallbackFunction::create ([this, fenetre] (int resultat)
    {
        const auto nom = fenetre->getTextEditorContents ("nom").trim();

        if (resultat != 1 || nom.isEmpty() || nom == moteur.getMorceau().nom)
            return;

        if (actions.noms && actions.noms().contains (nom))
        {
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Nom pris",
                                                    "Un autre morceau porte déjà ce nom."_fr);
            return;
        }

        moteur.modifier ([nom] (Morceau& m) { m.nom = nom; });
    }));
}

void PageSession::timerCallback()
{
    const auto& m = moteur.getMorceau();
    nomDuMorceau.setText (m.nom, juce::dontSendNotification);
    infos.setText (m.mesure.nom() + " " + m.mesure.nomGroupement() + ", " + juce::String (juce::roundToInt (m.tempo)) + " BPM. Lancements : "
                       + nomDuCalage (m.calage).toLowerCase() + ". Le jeu se fait au Push ou dans le simulateur.",
                   juce::dontSendNotification);
    repaint (zoneGrille);
}

void PageSession::paint (juce::Graphics& g)
{
    g.fillAll (couleurFond);

    const auto& m = moteur.getMorceau();
    const int lignes = (int) m.scenes.size();
    const int largeurNoms = 150;
    const float colonne = (float) (zoneGrille.getWidth() - largeurNoms) / (float) nombreDePistes;
    const float hauteur = juce::jmin (34.0f, (float) zoneGrille.getHeight() / (float) (lignes + 1));
    const auto p = moteur.getPosition();

    // En-têtes : les pistes, dans leur couleur
    for (int piste = 0; piste < nombreDePistes; ++piste)
    {
        const auto& pp = m.pistes[(size_t) piste];
        auto zone = juce::Rectangle<float> ((float) zoneGrille.getX() + largeurNoms + piste * colonne, (float) zoneGrille.getY(), colonne, hauteur).reduced (2.0f);
        g.setColour (pp.mute ? couleurPanneau : palette::couleurDePiste (piste).withAlpha (0.85f));
        g.fillRoundedRectangle (zone, 4.0f);
        g.setColour (pp.mute ? couleurDiscret : couleurFond);
        g.setFont (juce::FontOptions (15.0f, juce::Font::bold));
        g.drawFittedText (pp.nom + (pp.solo ? " (solo)" : "") + (pp.mute ? juce::String (" (coupée)"_fr) : juce::String()),
                          zone.toNearestInt(), juce::Justification::centred, 1, 0.6f);
    }

    for (int s = 0; s < lignes; ++s)
    {
        const float y = (float) zoneGrille.getY() + (s + 1) * hauteur;
        g.setColour (couleurTexte);
        g.setFont (juce::FontOptions (15.0f));
        g.drawFittedText (juce::String (s + 1) + ". " + m.scenes[(size_t) s].nom,
                          juce::Rectangle<float> ((float) zoneGrille.getX(), y, (float) largeurNoms - 6.0f, hauteur).toNearestInt(),
                          juce::Justification::centredLeft, 1, 0.7f);

        for (int piste = 0; piste < nombreDePistes; ++piste)
        {
            auto zone = juce::Rectangle<float> ((float) zoneGrille.getX() + largeurNoms + piste * colonne, y, colonne, hauteur).reduced (2.0f);
            const auto etat = moteur.etatCase (piste, s);
            const auto couleur = palette::couleurDePiste (piste);

            if (! etat.clip)
            {
                g.setColour (couleurPanneau.withAlpha (0.5f));
                g.drawRoundedRectangle (zone, 4.0f, 1.0f);
                continue;
            }

            const bool clignote = std::fmod (p.fraction * 2.0, 1.0) < 0.5;
            const float force = etat.joue ? 0.95f : (etat.departEnAttente && clignote) ? 0.75f : 0.35f;
            g.setColour (couleur.withMultipliedBrightness (force));
            g.fillRoundedRectangle (zone, 4.0f);

            if (etat.joue)
            {
                g.setColour (couleurTexte);
                g.drawRoundedRectangle (zone, 4.0f, 2.0f);
            }

            const auto* clip = m.clip (piste, s);
            g.setColour (etat.joue ? couleurFond : couleurTexte);
            g.setFont (juce::FontOptions (14.0f));
            g.drawFittedText (clip->nom + "  " + juce::String (clip->longueurMesures), zone.reduced (4.0f, 0.0f).toNearestInt(),
                              juce::Justification::centredLeft, 1, 0.6f);
        }
    }
}

void PageSession::resized()
{
    auto zone = getLocalBounds().reduced (20, 14);
    auto haut = zone.removeFromTop (44);
    boutonSimulateur.setBounds (haut.removeFromRight (180).reduced (0, 4));
    haut.removeFromRight (8);

    for (auto* b : { &boutonDemo, &boutonRenommer, &boutonOuvrir, &boutonNouveau })
    {
        b->setBounds (haut.removeFromRight (100).reduced (0, 4));
        haut.removeFromRight (6);
    }

    nomDuMorceau.setBounds (haut);
    infos.setBounds (zone.removeFromTop (26));
    zone.removeFromTop (10);
    zoneGrille = zone;
}

} // namespace groovebox
