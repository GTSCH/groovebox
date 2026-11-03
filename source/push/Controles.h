#pragma once

#include "Texte.h"

namespace groovebox
{

/** Boutons du Push 2, par leur numéro de contrôleur MIDI (manuel MIDI et écran d'Ableton). */
enum class Bouton : int
{
    tapTempo = 3, metronome = 9, master = 28, stopClip = 29, setup = 30, layout = 31,
    convert = 35, scene8 = 36, scene7 = 37, scene6 = 38, scene5 = 39, scene4 = 40, scene3 = 41, scene2 = 42, scene1 = 43,
    gauche = 44, droite = 45, haut = 46, bas = 47, select = 48, shift = 49, note = 50, session = 51,
    addDevice = 52, addTrack = 53, octaveBas = 54, octaveHaut = 55, repeat = 56, accent = 57, scale = 58, user = 59,
    mute = 60, solo = 61, pageGauche = 62, pageDroite = 63,
    play = 85, record = 86, nouveau = 87, duplicate = 88, automate = 89, fixedLength = 90,
    device = 110, browse = 111, mix = 112, clip = 113, quantize = 116, doubleLoop = 117, deleteBouton = 118, undo = 119
};

/** Les rangées de 8 boutons au-dessus (102 à 109) et au-dessous (20 à 27) de l'écran. */
constexpr int premierBoutonDuHaut = 102, premierBoutonDuBas = 20;

/** Encodeurs : 0 à 7 sous l'écran, puis Master, Swing et Tempo. */
enum class Encodeur : int { piste1 = 0, piste8 = 7, master = 8, swing = 9, tempo = 10 };
constexpr int nombreEncodeurs = 11;

struct Evenement
{
    enum class Type { pad, bouton, encodeurTourne, encodeurTouche, bandeTouchee, bandeDeplacee };

    Type type = Type::bouton;
    int numero = 0;         ///< Pad : 0 à 63 (0 en bas à gauche) ; bouton : numéro de contrôleur ; encodeur : 0 à 10
    int valeur = 0;         ///< Pad : vélocité (0 au relâché) ; bouton : 1 appuyé, 0 relâché ; encodeur : pas (+ à droite) ; bande : 0 à 16383
    bool appuye = false;    ///< Pad, bouton, toucher : vrai à l'appui

    static Evenement pad (int ligne, int colonne, int velocite);
    int ligne() const       { return numero / 8; }      ///< Pads : 0 en bas
    int colonne() const     { return numero % 8; }

    bool operator== (const Evenement&) const = default;
};

/** Lit un message venu du port User du Push. */
std::optional<Evenement> lireMessage (const juce::MidiMessage&);

/** Message MIDI qu'enverrait le Push pour cet événement (sert au simulateur et aux essais). */
juce::MidiMessage messageDe (const Evenement&);

/** Nom d'un bouton ou d'un encodeur, en français, pour l'écran. */
juce::String nomDuBouton (int numero);
juce::String nomDeLEncodeur (int numero);
bool boutonAUneLedCouleur (int numero);

/** Note MIDI d'un pad (36 en bas à gauche, 99 en haut à droite). */
inline int noteDuPad (int numero)           { return 36 + numero; }

/** Commandes système exclusives du Push (seulement celles documentées par Ableton). */
namespace sysex
{
    juce::MidiMessage modeMidi (int mode);                  ///< 0 Live, 1 User
    juce::MidiMessage entreeDePalette (int index, juce::Colour, int blanc);
    juce::MidiMessage reappliquerPalette();
    juce::MidiMessage configurationBande (int drapeaux);
}

} // namespace groovebox
