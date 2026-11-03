#pragma once

#include "push/Surface.h"

namespace groovebox
{

/** Le Push dessiné sur le Mac : mêmes pads, boutons, encodeurs, bande tactile et écran que le vrai.
    Souris : clic sur un pad (plus haut dans le pad, plus fort), clic sur un bouton, glisser ou molette sur un encodeur. Touche Majuscule : Shift. */
class Simulateur  : public juce::Component,
                    public ClientSurface
{
public:
    explicit Simulateur (Surface&);
    ~Simulateur() override;

    void afficher (const EtatSurface&, const juce::Image&) override;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    static constexpr float largeurDuPlan = 1600.0f, hauteurDuPlan = 1300.0f;

    struct Element
    {
        enum class Genre { pad, bouton, encodeur, bande, ecran };
        Genre genre;
        juce::Rectangle<float> zone;    ///< Dans le plan de 1600 sur 1300
        int numero;
        juce::String etiquette;
    };

    static const std::vector<Element>& plan();

private:
    Surface& surface;
    EtatSurface etat;
    juce::Image ecran;

    const Element* elementAppuye = nullptr;
    float dernierY = 0.0f, reste = 0.0f;
    int positionBande = 8192;
    std::array<bool, nombreEncodeurs> encodeursTouches {};

    juce::AffineTransform versEcran() const;
    const Element* elementEn (juce::Point<float>) const;
    void envoyer (Evenement);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Simulateur)
};

/** La fenêtre à part qui contient le simulateur. */
class FenetreSimulateur  : public juce::DocumentWindow
{
public:
    explicit FenetreSimulateur (Surface&);
    void closeButtonPressed() override      { setVisible (false); }

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FenetreSimulateur)
};

} // namespace groovebox
