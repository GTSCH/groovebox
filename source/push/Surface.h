#pragma once

#include "Controles.h"
#include "Palette.h"

namespace groovebox
{

/** Ce que montre la surface de contrôle : couleur de chaque pad, LED de chaque bouton, image de l'écran.
    Les index sont ceux de la palette du Push ; le simulateur les traduit avec la même palette. */
struct EtatSurface
{
    static constexpr int largeurEcran = 960, hauteurEcran = 160;

    std::array<int, 64> pads {};        ///< Index de palette, 0 en bas à gauche
    std::array<int, 128> boutons {};    ///< Index de palette par numéro de contrôleur
    std::array<int, 128> animations {}; ///< Canal d'animation du Push (0 : aucune)
    bool bandeSansRetour = false;       ///< Bande tactile en curseur (enchaînement à la main) : elle reste où on la lâche

    /** Réglages de la bande tactile : filtre global (point, retour au centre) ou curseur de mélange (barre depuis le bas, sans retour). */
    static constexpr int configurationBandeFiltre = 0b1101000, configurationBandeCurseur = 0b0000000;

    /** Messages MIDI pour passer de l'état « avant » à cet état (seules les différences sont envoyées). */
    std::vector<juce::MidiMessage> messagesDepuis (const EtatSurface& avant) const;
    std::vector<juce::MidiMessage> tousLesMessages() const;

    bool operator== (const EtatSurface&) const = default;
};

/** Ce qui écoute ou montre la surface : le vrai Push et le simulateur. */
class ClientSurface
{
public:
    virtual ~ClientSurface() = default;
    virtual void afficher (const EtatSurface&, const juce::Image& ecran) = 0;
};

/** Point de rencontre : les événements viennent du Push ou du simulateur, l'état repart vers les deux. */
class Surface
{
public:
    Surface();

    void ajouterClient (ClientSurface*);
    void retirerClient (ClientSurface*);

    /** Appelé par le Push ou le simulateur, sur le fil principal. */
    void recevoir (const Evenement&);
    std::function<void (const Evenement&)> surEvenement;

    /** Envoie un nouvel état et une nouvelle image d'écran à tous les clients. */
    void publier (const EtatSurface&, const juce::Image& ecran);

    const EtatSurface& getEtat() const          { return etat; }
    const juce::Image& getEcran() const         { return ecran; }

    /** Contrôles appuyés en ce moment, d'où qu'ils viennent (Shift, pads...). */
    bool estAppuye (Bouton b) const             { return boutonsAppuyes[(size_t) b]; }

private:
    EtatSurface etat;
    juce::Image ecran;
    std::vector<ClientSurface*> clients;
    std::array<bool, 128> boutonsAppuyes {};
};

} // namespace groovebox
