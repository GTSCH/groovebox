#pragma once

#include "Surface.h"
#include "moteur/Moteur.h"
#include "modele/TapTempo.h"

namespace groovebox
{

/** Ce que fait chaque contrôle du Push, et ce que le Push montre : LED et écran.
    Page de l'étape 3 : les réglages du clic sur les encodeurs 1 à 6. */
class Pilote  : private juce::Timer
{
public:
    Pilote (Moteur&, Surface&);
    ~Pilote() override;

    void traiter (const Evenement&);

    /** Calcule l'état des LED et dessine l'écran, puis les envoie au Push et au simulateur. */
    void rafraichir();

    EtatSurface calculerEtat() const;
    void dessinerEcran (juce::Image&) const;

    /** Message affiché un instant sur l'écran quand on touche un contrôle (mode d'essai). */
    struct Annonce
    {
        juce::String titre, valeur;
        double fin = 0.0;
    };

    const Annonce& getAnnonce() const       { return annonce; }

private:
    struct Reglage
    {
        juce::String nom;
        std::function<juce::String()> valeur;
        std::function<float()> proportion;
        std::function<void (int pas, bool fin)> tourner;
    };

    Moteur& moteur;
    Surface& surface;
    TapTempo tapTempo;
    std::array<int, 64> velocites {};
    std::array<bool, nombreEncodeurs> touches {};
    std::array<int, nombreEncodeurs> crans {};
    std::array<Reglage, 8> page;
    Annonce annonce;
    juce::Image ecran { juce::Image::RGB, EtatSurface::largeurEcran, EtatSurface::hauteurEcran, true };

    bool shift() const;
    void annoncer (const juce::String& titre, const juce::String& valeur);
    int crantsDiscrets (int encodeur, int pas);
    void tournerEncodeur (int encodeur, int pas);
    void appuyer (int bouton);
    void timerCallback() override       { rafraichir(); }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Pilote)
};

} // namespace groovebox
