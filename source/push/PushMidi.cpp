#include "PushMidi.h"

namespace groovebox
{

PushMidi::PushMidi (Surface& s)  : surface (s)
{
    chercher();
    startTimer (2000);
}

PushMidi::~PushMidi()
{
    *vivant = false;
    stopTimer();
    fermer();
}

bool PushMidi::estUnPortPush (const juce::String& nom)
{
    return nom.containsIgnoreCase ("Push 2") && nom.containsIgnoreCase ("User");
}

void PushMidi::timerCallback()
{
    // Débranché ? Les ports disparaissent de la liste
    if (estConnecte())
    {
        bool toujoursLa = false;

        for (auto& d : juce::MidiInput::getAvailableDevices())
            toujoursLa = toujoursLa || d.name == nomDuPort;

        if (! toujoursLa)
        {
            juce::Logger::writeToLog ("Push débranché"_fr);
            entree.reset();
            sortie.reset();
        }

        return;
    }

    chercher();
}

void PushMidi::chercher()
{
    juce::MidiDeviceInfo infoEntree, infoSortie;

    for (auto& d : juce::MidiInput::getAvailableDevices())
        if (estUnPortPush (d.name))
            infoEntree = d;

    for (auto& d : juce::MidiOutput::getAvailableDevices())
        if (estUnPortPush (d.name))
            infoSortie = d;

    if (infoEntree.identifier.isEmpty() || infoSortie.identifier.isEmpty())
        return;

    entree = juce::MidiInput::openDevice (infoEntree.identifier, this);
    sortie = juce::MidiOutput::openDevice (infoSortie.identifier);

    if (entree == nullptr || sortie == nullptr)
    {
        entree.reset();
        sortie.reset();
        return;
    }

    nomDuPort = infoEntree.name;
    entree->start();
    juce::Logger::writeToLog ("Push branché : "_fr + nomDuPort);
    initialiser();
}

void PushMidi::initialiser()
{
    sortie->sendMessageNow (sysex::modeMidi (1));

    for (auto& [index, couleur] : palette::entreesProgrammees())
        sortie->sendMessageNow (sysex::entreeDePalette (index, couleur, 0));

    sortie->sendMessageNow (sysex::reappliquerPalette());

    etatEnvoye = false;
    afficher (surface.getEtat(), surface.getEcran());
}

void PushMidi::fermer()
{
    if (sortie != nullptr)
    {
        // On laisse le Push propre pour Ableton Live : LED éteintes, mode Live
        sortie->sendBlockOfMessagesNow ([]
        {
            juce::MidiBuffer messages;
            int position = 0;

            for (auto& m : EtatSurface {}.tousLesMessages())
                messages.addEvent (m, position++);

            return messages;
        }());

        sortie->sendMessageNow (sysex::modeMidi (0));
    }

    if (entree != nullptr)
        entree->stop();

    entree.reset();
    sortie.reset();
}

void PushMidi::afficher (const EtatSurface& etat, const juce::Image&)
{
    if (sortie == nullptr)
        return;

    const auto messages = etatEnvoye ? etat.messagesDepuis (dernierEnvoye) : etat.tousLesMessages();

    for (auto& m : messages)
        sortie->sendMessageNow (m);

    dernierEnvoye = etat;
    etatEnvoye = true;
}

void PushMidi::handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage& m)
{
    // Fil MIDI : on repasse sur le fil principal, sans jamais bloquer
    if (auto e = lireMessage (m))
        juce::MessageManager::callAsync ([this, vie = vivant, e = *e]
        {
            if (*vie)
                surface.recevoir (e);
        });
}

} // namespace groovebox
