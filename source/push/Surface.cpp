#include "Surface.h"

namespace groovebox
{

namespace
{
    juce::MidiMessage messagePourPad (int pad, int index)
    {
        return juce::MidiMessage::noteOn (1, noteDuPad (pad), (juce::uint8) index);
    }

    juce::MidiMessage messagePourBouton (int numero, int index, int animation)
    {
        // Le canal porte l'animation (0 : fixe), d'où le canal MIDI 1 + animation
        return juce::MidiMessage::controllerEvent (1 + animation, numero, index);
    }
}

std::vector<juce::MidiMessage> EtatSurface::messagesDepuis (const EtatSurface& avant) const
{
    std::vector<juce::MidiMessage> messages;

    for (int i = 0; i < 64; ++i)
        if (pads[(size_t) i] != avant.pads[(size_t) i])
            messages.push_back (messagePourPad (i, pads[(size_t) i]));

    for (int i = 0; i < 128; ++i)
        if (boutons[(size_t) i] != avant.boutons[(size_t) i] || animations[(size_t) i] != avant.animations[(size_t) i])
            messages.push_back (messagePourBouton (i, boutons[(size_t) i], animations[(size_t) i]));

    return messages;
}

std::vector<juce::MidiMessage> EtatSurface::tousLesMessages() const
{
    EtatSurface vide;
    vide.pads.fill (-1);
    vide.boutons.fill (-1);
    return messagesDepuis (vide);
}

Surface::Surface()
    : ecran (juce::Image::RGB, EtatSurface::largeurEcran, EtatSurface::hauteurEcran, true)
{
}

void Surface::ajouterClient (ClientSurface* c)
{
    clients.push_back (c);
    c->afficher (etat, ecran);
}

void Surface::retirerClient (ClientSurface* c)
{
    clients.erase (std::remove (clients.begin(), clients.end(), c), clients.end());
}

void Surface::recevoir (const Evenement& e)
{
    if (e.type == Evenement::Type::bouton && e.numero >= 0 && e.numero < 128)
        boutonsAppuyes[(size_t) e.numero] = e.appuye;

    if (surEvenement)
        surEvenement (e);
}

void Surface::publier (const EtatSurface& nouveau, const juce::Image& nouvelEcran)
{
    etat = nouveau;
    ecran = nouvelEcran;

    for (auto* c : clients)
        c->afficher (etat, ecran);
}

} // namespace groovebox
