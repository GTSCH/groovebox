#include "Simulateur.h"
#include "Style.h"

namespace groovebox
{

namespace
{
    using Genre = Simulateur::Element::Genre;

    const juce::Colour couleurCorps { 0xff1e2026 }, couleurEteinte { 0xff2b2e36 }, couleurBord { 0xff3a3e48 };

    juce::Rectangle<float> r (float x1, float y1, float x2, float y2)
    {
        return { x1, y1, x2 - x1, y2 - y1 };
    }
}

const std::vector<Simulateur::Element>& Simulateur::plan()
{
    // Positions relevées sur le schéma « MIDI Mapping » du manuel d'Ableton
    static const std::vector<Element> elements = []
    {
        std::vector<Element> e;
        auto bouton = [&e] (juce::Rectangle<float> zone, Bouton b, const juce::String& etiquette)
        {
            e.push_back ({ Genre::bouton, zone, (int) b, etiquette });
        };

        // Encodeurs : Tempo, Swing, les 8 de l'écran, Master
        auto encodeur = [&e] (float x, int numero, const juce::String& etiquette)
        {
            e.push_back ({ Genre::encodeur, { x - 30.0f, 60.0f, 60.0f, 60.0f }, numero, etiquette });
        };

        encodeur (100.0f, (int) Encodeur::tempo, "Tempo");
        encodeur (220.0f, (int) Encodeur::swing, "Swing");

        for (int i = 0; i < 8; ++i)
            encodeur (365.0f + 118.0f * (float) i, i, juce::String (i + 1));

        encodeur (1470.0f, (int) Encodeur::master, "Master");

        // Rangées autour de l'écran
        bouton (r (55, 148, 158, 190), Bouton::tapTempo, "Tap Tempo");
        bouton (r (162, 148, 265, 190), Bouton::metronome, "Metronome");

        for (int i = 0; i < 8; ++i)
        {
            const float x = 312.0f + 118.0f * (float) i;
            e.push_back ({ Genre::bouton, { x, 148.0f, 105.0f, 42.0f }, premierBoutonDuHaut + i, {} });
            e.push_back ({ Genre::bouton, { x, 445.0f, 105.0f, 42.0f }, premierBoutonDuBas + i, {} });
        }

        bouton (r (1390, 148, 1466, 190), Bouton::setup, "Setup");
        bouton (r (1469, 148, 1545, 190), Bouton::user, "User");

        e.push_back ({ Genre::ecran, r (305, 233, 1250, 398), 0, {} });

        bouton (r (55, 240, 132, 314), Bouton::deleteBouton, "Delete");
        bouton (r (55, 318, 132, 392), Bouton::undo, "Undo");
        bouton (r (1288, 240, 1365, 314), Bouton::addDevice, "Add Device");
        bouton (r (1288, 318, 1365, 392), Bouton::addTrack, "Add Track");
        bouton (r (1390, 240, 1466, 314), Bouton::device, "Device");
        bouton (r (1469, 240, 1545, 314), Bouton::mix, "Mix");
        bouton (r (1390, 318, 1466, 392), Bouton::browse, "Browse");
        bouton (r (1469, 318, 1545, 392), Bouton::clip, "Clip");

        bouton (r (55, 445, 123, 488), Bouton::mute, "Mute");
        bouton (r (126, 445, 194, 488), Bouton::solo, "Solo");
        bouton (r (197, 445, 265, 488), Bouton::stopClip, "Stop Clip");
        bouton (r (1288, 445, 1365, 488), Bouton::master, "Master");

        bouton (r (1432, 445, 1503, 488), Bouton::haut, "Haut");
        bouton (r (1390, 492, 1446, 552), Bouton::gauche, "Gauche");
        bouton (r (1489, 492, 1545, 552), Bouton::droite, "Droite");
        bouton (r (1432, 556, 1503, 598), Bouton::bas, "Bas");

        // Pads : 8 sur 8, ligne 0 en bas
        for (int ligne = 0; ligne < 8; ++ligne)
            for (int colonne = 0; colonne < 8; ++colonne)
                e.push_back ({ Genre::pad, { 312.0f + 118.0f * (float) colonne, 522.0f + 89.6f * (float) (7 - ligne), 105.0f, 77.0f },
                               ligne * 8 + colonne, {} });

        e.push_back ({ Genre::bande, r (178, 522, 265, 1225), 0, "Bande" });

        bouton (r (55, 522, 132, 595), Bouton::convert, "Convert");
        bouton (r (55, 598, 132, 668), Bouton::doubleLoop, "Double Loop");
        bouton (r (55, 671, 132, 745), Bouton::quantize, "Quantize");
        bouton (r (55, 760, 132, 833), Bouton::duplicate, "Duplicate");
        bouton (r (55, 836, 132, 912), Bouton::nouveau, "New");
        bouton (r (55, 925, 132, 993), Bouton::fixedLength, "Fixed Length");
        bouton (r (55, 996, 132, 1063), Bouton::automate, "Automate");
        bouton (r (55, 1066, 132, 1135), Bouton::record, "Record");
        bouton (r (55, 1150, 132, 1225), Bouton::play, "Play");

        for (int k = 0; k < 8; ++k)
            e.push_back ({ Genre::bouton, { 1288.0f, 522.0f + 89.6f * (float) k, 77.0f, 77.0f }, 43 - k, {} });

        bouton (r (1390, 790, 1466, 868), Bouton::repeat, "Repeat");
        bouton (r (1469, 790, 1545, 868), Bouton::accent, "Accent");
        bouton (r (1390, 880, 1466, 922), Bouton::scale, "Scale");
        bouton (r (1469, 880, 1545, 922), Bouton::layout, "Layout");
        bouton (r (1390, 925, 1466, 1000), Bouton::note, "Note");
        bouton (r (1469, 925, 1545, 1000), Bouton::session, "Session");

        bouton (r (1432, 1015, 1503, 1058), Bouton::octaveHaut, "Octave +");
        bouton (r (1390, 1062, 1446, 1120), Bouton::pageGauche, "Page <");
        bouton (r (1489, 1062, 1545, 1120), Bouton::pageDroite, "Page >");
        bouton (r (1432, 1123, 1503, 1165), Bouton::octaveBas, "Octave -");

        bouton (r (1390, 1180, 1466, 1225), Bouton::shift, "Shift");
        bouton (r (1469, 1180, 1545, 1225), Bouton::select, "Select");
        return e;
    }();

    return elements;
}

Simulateur::Simulateur (Surface& s)  : surface (s)
{
    surface.ajouterClient (this);
    setSize (1104, 897);
}

Simulateur::~Simulateur()
{
    surface.retirerClient (this);
}

void Simulateur::afficher (const EtatSurface& nouvelEtat, const juce::Image& image)
{
    etat = nouvelEtat;
    ecran = image;

    if (isShowing())
        repaint();
}

juce::AffineTransform Simulateur::versEcran() const
{
    const float echelle = juce::jmin ((float) getWidth() / largeurDuPlan, (float) getHeight() / hauteurDuPlan);
    return juce::AffineTransform::scale (echelle);
}

const Simulateur::Element* Simulateur::elementEn (juce::Point<float> p) const
{
    const auto dansLePlan = p.transformedBy (versEcran().inverted());

    for (auto& e : plan())
        if (e.genre != Genre::ecran && e.zone.contains (dansLePlan))
            return &e;

    return nullptr;
}

void Simulateur::envoyer (Evenement e)
{
    surface.recevoir (e);
}

//==============================================================================
void Simulateur::paint (juce::Graphics& g)
{
    g.fillAll (couleurFond);
    g.addTransform (versEcran());

    g.setColour (couleurCorps);
    g.fillRoundedRectangle (r (20, 20, 1580, 1280), 24.0f);

    for (auto& e : plan())
    {
        switch (e.genre)
        {
            case Genre::pad:
            {
                const auto index = etat.pads[(size_t) e.numero];
                g.setColour (index == palette::noir ? couleurEteinte : palette::couleurDeLIndex (index));
                g.fillRoundedRectangle (e.zone, 8.0f);
                break;
            }

            case Genre::bouton:
            {
                const auto index = etat.boutons[(size_t) e.numero];
                juce::Colour couleur = couleurEteinte;

                if (boutonAUneLedCouleur (e.numero))
                    couleur = index == palette::noir ? couleurEteinte : palette::couleurDeLIndex (index);
                else
                    couleur = couleurEteinte.interpolatedWith (juce::Colours::white, palette::luminositeBlanche (index) * 0.85f);

                g.setColour (couleur);
                g.fillRoundedRectangle (e.zone, 6.0f);
                g.setColour (couleurBord);
                g.drawRoundedRectangle (e.zone, 6.0f, 2.0f);

                if (e.etiquette.isNotEmpty())
                {
                    g.setColour (couleur.getPerceivedBrightness() > 0.6f ? couleurCorps : couleurTexte);
                    g.setFont (juce::FontOptions (19.0f));
                    g.drawFittedText (e.etiquette, e.zone.reduced (4.0f).toNearestInt(), juce::Justification::centred, 2);
                }
                break;
            }

            case Genre::encodeur:
            {
                const bool touche = encodeursTouches[(size_t) e.numero];
                g.setColour (touche ? couleurActif : couleurEteinte);
                g.fillEllipse (e.zone);
                g.setColour (couleurBord);
                g.drawEllipse (e.zone, 3.0f);
                g.setColour (couleurDiscret);
                g.setFont (juce::FontOptions (19.0f));
                g.drawText (e.etiquette, e.zone.translated (0.0f, 38.0f).withHeight (22.0f).expanded (20.0f, 0.0f), juce::Justification::centred);
                break;
            }

            case Genre::bande:
            {
                g.setColour (couleurEteinte);
                g.fillRoundedRectangle (e.zone, 8.0f);
                const float y = e.zone.getBottom() - e.zone.getHeight() * (float) positionBande / 16383.0f;
                g.setColour (couleurTexte);
                g.fillRect (e.zone.getX() + 8.0f, y - 3.0f, e.zone.getWidth() - 16.0f, 6.0f);
                break;
            }

            case Genre::ecran:
                g.setColour (juce::Colours::black);
                g.fillRect (e.zone.expanded (6.0f));

                if (ecran.isValid())
                    g.drawImage (ecran, e.zone, juce::RectanglePlacement::stretchToFit);
                break;
        }
    }
}

//==============================================================================
void Simulateur::mouseDown (const juce::MouseEvent& m)
{
    elementAppuye = elementEn (m.position);

    if (elementAppuye == nullptr)
        return;

    const auto& e = *elementAppuye;
    const auto dansLePlan = m.position.transformedBy (versEcran().inverted());
    dernierY = m.position.y;
    reste = 0.0f;

    switch (e.genre)
    {
        case Genre::pad:
        {
            // Plus haut dans le pad, plus fort
            const auto hauteur = 1.0f - (dansLePlan.y - e.zone.getY()) / e.zone.getHeight();
            envoyer ({ Evenement::Type::pad, e.numero, juce::jlimit (1, 127, juce::roundToInt (20.0f + 107.0f * hauteur)), true });
            break;
        }

        case Genre::bouton:
            envoyer ({ Evenement::Type::bouton, e.numero, 1, true });
            break;

        case Genre::encodeur:
            encodeursTouches[(size_t) e.numero] = true;
            envoyer ({ Evenement::Type::encodeurTouche, e.numero, 127, true });
            break;

        case Genre::bande:
            envoyer ({ Evenement::Type::bandeTouchee, 0, 127, true });
            mouseDrag (m);
            break;

        case Genre::ecran:
            break;
    }

    repaint();
}

void Simulateur::mouseDrag (const juce::MouseEvent& m)
{
    if (elementAppuye == nullptr)
        return;

    if (elementAppuye->genre == Genre::encodeur)
    {
        // Glisser vers le haut tourne à droite : un pas tous les 2 pixels
        reste += (dernierY - m.position.y) / 2.0f;
        dernierY = m.position.y;
        const int pas = (int) reste;

        if (pas != 0)
        {
            reste -= (float) pas;
            envoyer ({ Evenement::Type::encodeurTourne, elementAppuye->numero, juce::jlimit (-63, 63, pas), false });
        }
    }
    else if (elementAppuye->genre == Genre::bande)
    {
        const auto dansLePlan = m.position.transformedBy (versEcran().inverted());
        const auto& zone = elementAppuye->zone;
        positionBande = juce::jlimit (0, 16383, juce::roundToInt (16383.0f * (zone.getBottom() - dansLePlan.y) / zone.getHeight()));
        envoyer ({ Evenement::Type::bandeDeplacee, 0, positionBande, false });
        repaint();
    }
}

void Simulateur::mouseUp (const juce::MouseEvent&)
{
    if (elementAppuye == nullptr)
        return;

    const auto& e = *elementAppuye;

    switch (e.genre)
    {
        case Genre::pad:        envoyer ({ Evenement::Type::pad, e.numero, 0, false }); break;
        case Genre::bouton:     envoyer ({ Evenement::Type::bouton, e.numero, 0, false }); break;
        case Genre::bande:
            // Comme le vrai Push : la bande revient au centre quand on la lâche
            positionBande = 8192;
            envoyer ({ Evenement::Type::bandeDeplacee, 0, positionBande, false });
            envoyer ({ Evenement::Type::bandeTouchee, 0, 0, false });
            break;
        case Genre::encodeur:
            encodeursTouches[(size_t) e.numero] = false;
            envoyer ({ Evenement::Type::encodeurTouche, e.numero, 0, false });
            break;
        case Genre::ecran:      break;
    }

    elementAppuye = nullptr;
    repaint();
}

void Simulateur::mouseWheelMove (const juce::MouseEvent& m, const juce::MouseWheelDetails& molette)
{
    auto e = elementEn (m.position);

    if (e == nullptr || e->genre != Genre::encodeur)
        return;

    reste += molette.deltaY * 40.0f;
    const int pas = (int) reste;

    if (pas != 0)
    {
        reste -= (float) pas;
        envoyer ({ Evenement::Type::encodeurTourne, e->numero, juce::jlimit (-63, 63, pas), false });
    }
}

//==============================================================================
FenetreSimulateur::FenetreSimulateur (Surface& surface)
    : juce::DocumentWindow ("Simulateur du Push", couleurFond, juce::DocumentWindow::closeButton | juce::DocumentWindow::minimiseButton)
{
    setUsingNativeTitleBar (true);
    setContentOwned (new Simulateur (surface), true);
    setResizable (true, true);
    getConstrainer()->setFixedAspectRatio (Simulateur::largeurDuPlan / Simulateur::hauteurDuPlan);
    setResizeLimits (552, 448, 1840, 1495);
    centreWithSize (getWidth(), getHeight());
}

} // namespace groovebox
