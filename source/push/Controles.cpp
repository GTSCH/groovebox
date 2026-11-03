#include "Controles.h"

namespace groovebox
{

Evenement Evenement::pad (int ligne, int colonne, int velocite)
{
    return { Type::pad, ligne * 8 + colonne, velocite, velocite > 0 };
}

std::optional<Evenement> lireMessage (const juce::MidiMessage& m)
{
    if (m.isNoteOnOrOff())
    {
        const int note = m.getNoteNumber();
        const int velocite = m.isNoteOn() ? m.getVelocity() : 0;

        if (note >= 36 && note <= 99)
            return Evenement { Evenement::Type::pad, note - 36, velocite, velocite > 0 };

        if (note >= 0 && note < nombreEncodeurs)
            return Evenement { Evenement::Type::encodeurTouche, note, velocite, velocite > 0 };

        if (note == 12)
            return Evenement { Evenement::Type::bandeTouchee, 0, velocite, velocite > 0 };

        return std::nullopt;
    }

    if (m.isController())
    {
        const int numero = m.getControllerNumber();
        const int valeur = m.getControllerValue();

        // Encodeurs : valeur relative sur 7 bits, en complément à deux
        int encodeur = -1;

        if (numero >= 71 && numero <= 78)   encodeur = numero - 71;
        else if (numero == 79)              encodeur = (int) Encodeur::master;
        else if (numero == 15)              encodeur = (int) Encodeur::swing;
        else if (numero == 14)              encodeur = (int) Encodeur::tempo;

        if (encodeur >= 0)
            return Evenement { Evenement::Type::encodeurTourne, encodeur, valeur < 64 ? valeur : valeur - 128, false };

        return Evenement { Evenement::Type::bouton, numero, valeur > 0 ? 1 : 0, valeur > 0 };
    }

    if (m.isPitchWheel())
        return Evenement { Evenement::Type::bandeDeplacee, 0, m.getPitchWheelValue(), false };

    return std::nullopt;
}

juce::MidiMessage messageDe (const Evenement& e)
{
    switch (e.type)
    {
        case Evenement::Type::pad:
            return e.valeur > 0 ? juce::MidiMessage::noteOn (1, noteDuPad (e.numero), (juce::uint8) e.valeur)
                                : juce::MidiMessage::noteOff (1, noteDuPad (e.numero), (juce::uint8) 0);

        case Evenement::Type::bouton:
            return juce::MidiMessage::controllerEvent (1, e.numero, e.appuye ? 127 : 0);

        case Evenement::Type::encodeurTourne:
        {
            const int numero = e.numero < 8 ? 71 + e.numero
                             : e.numero == (int) Encodeur::master ? 79
                             : e.numero == (int) Encodeur::swing ? 15 : 14;
            return juce::MidiMessage::controllerEvent (1, numero, (e.valeur + 128) % 128);
        }

        case Evenement::Type::encodeurTouche:
            return juce::MidiMessage::noteOn (1, e.numero, (juce::uint8) (e.appuye ? 127 : 0));

        case Evenement::Type::bandeTouchee:
            return juce::MidiMessage::noteOn (1, 12, (juce::uint8) (e.appuye ? 127 : 0));

        case Evenement::Type::bandeDeplacee:
            return juce::MidiMessage::pitchWheel (1, e.valeur);
    }

    return {};
}

juce::String nomDuBouton (int n)
{
    if (n >= premierBoutonDuHaut && n < premierBoutonDuHaut + 8)
        return "Bouton du haut " + juce::String (n - premierBoutonDuHaut + 1);

    if (n >= premierBoutonDuBas && n < premierBoutonDuBas + 8)
        return "Bouton du bas " + juce::String (n - premierBoutonDuBas + 1);

    if (n >= 36 && n <= 43)
        return "Scène "_fr + juce::String (44 - n);

    switch ((Bouton) n)
    {
        case Bouton::tapTempo:      return "Tap Tempo";
        case Bouton::metronome:     return "Metronome";
        case Bouton::master:        return "Master";
        case Bouton::stopClip:      return "Stop Clip";
        case Bouton::setup:         return "Setup";
        case Bouton::layout:        return "Layout";
        case Bouton::convert:       return "Convert";
        case Bouton::gauche:        return "Gauche";
        case Bouton::droite:        return "Droite";
        case Bouton::haut:          return "Haut";
        case Bouton::bas:           return "Bas";
        case Bouton::select:        return "Select";
        case Bouton::shift:         return "Shift";
        case Bouton::note:          return "Note";
        case Bouton::session:       return "Session";
        case Bouton::addDevice:     return "Add Device";
        case Bouton::addTrack:      return "Add Track";
        case Bouton::octaveBas:     return "Octave bas";
        case Bouton::octaveHaut:    return "Octave haut";
        case Bouton::repeat:        return "Repeat";
        case Bouton::accent:        return "Accent";
        case Bouton::scale:         return "Scale";
        case Bouton::user:          return "User";
        case Bouton::mute:          return "Mute";
        case Bouton::solo:          return "Solo";
        case Bouton::pageGauche:    return "Page gauche";
        case Bouton::pageDroite:    return "Page droite";
        case Bouton::play:          return "Play";
        case Bouton::record:        return "Record";
        case Bouton::nouveau:       return "New";
        case Bouton::duplicate:     return "Duplicate";
        case Bouton::automate:      return "Automate";
        case Bouton::fixedLength:   return "Fixed Length";
        case Bouton::device:        return "Device";
        case Bouton::browse:        return "Browse";
        case Bouton::mix:           return "Mix";
        case Bouton::clip:          return "Clip";
        case Bouton::quantize:      return "Quantize";
        case Bouton::doubleLoop:    return "Double Loop";
        case Bouton::deleteBouton:  return "Delete";
        case Bouton::undo:          return "Undo";
        default:                    break;
    }

    return "Contrôle "_fr + juce::String (n);
}

juce::String nomDeLEncodeur (int n)
{
    if (n >= 0 && n < 8)
        return "Encodeur " + juce::String (n + 1);

    switch ((Encodeur) n)
    {
        case Encodeur::master:  return "Encodeur Master";
        case Encodeur::swing:   return "Encodeur Swing";
        case Encodeur::tempo:   return "Encodeur Tempo";
        default:                break;
    }

    return {};
}

bool boutonAUneLedCouleur (int n)
{
    return (n >= premierBoutonDuHaut && n < premierBoutonDuHaut + 8)
        || (n >= premierBoutonDuBas && n < premierBoutonDuBas + 8)
        || (n >= 36 && n <= 43)
        || n == (int) Bouton::mute || n == (int) Bouton::solo || n == (int) Bouton::stopClip
        || n == (int) Bouton::automate || n == (int) Bouton::record || n == (int) Bouton::play;
}

namespace sysex
{
    namespace
    {
        juce::MidiMessage commande (std::initializer_list<juce::uint8> arguments)
        {
            std::vector<juce::uint8> octets { 0x00, 0x21, 0x1D, 0x01, 0x01 };
            octets.insert (octets.end(), arguments);
            return juce::MidiMessage::createSysExMessage (octets.data(), (int) octets.size());
        }

        juce::uint8 bas7 (int v)    { return (juce::uint8) (v & 0x7f); }
        juce::uint8 haut1 (int v)   { return (juce::uint8) ((v >> 7) & 0x01); }
    }

    juce::MidiMessage modeMidi (int mode)               { return commande ({ 0x0A, (juce::uint8) mode }); }
    juce::MidiMessage reappliquerPalette()              { return commande ({ 0x05 }); }
    juce::MidiMessage configurationBande (int drapeaux) { return commande ({ 0x17, (juce::uint8) (drapeaux & 0x7f) }); }

    juce::MidiMessage entreeDePalette (int index, juce::Colour c, int blanc)
    {
        return commande ({ 0x03, (juce::uint8) index,
                           bas7 (c.getRed()), haut1 (c.getRed()),
                           bas7 (c.getGreen()), haut1 (c.getGreen()),
                           bas7 (c.getBlue()), haut1 (c.getBlue()),
                           bas7 (blanc), haut1 (blanc) });
    }
}

} // namespace groovebox
