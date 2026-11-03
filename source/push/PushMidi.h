#pragma once

#include "Surface.h"

namespace groovebox
{

/** Pads, boutons, encodeurs et LED du vrai Push, par son port MIDI « User ».
    À la connexion : mode User, palette, bande tactile, puis toutes les LED. À la fermeture : LED éteintes et retour au mode Live. */
class PushMidi  : public ClientSurface,
                  private juce::MidiInputCallback,
                  private juce::Timer
{
public:
    explicit PushMidi (Surface&);
    ~PushMidi() override;

    void afficher (const EtatSurface&, const juce::Image&) override;

    bool estConnecte() const            { return sortie != nullptr && entree != nullptr; }
    juce::String getNomDuPort() const   { return nomDuPort; }

    static bool estUnPortPush (const juce::String& nom);

private:
    Surface& surface;
    std::unique_ptr<juce::MidiInput> entree;
    std::unique_ptr<juce::MidiOutput> sortie;
    juce::String nomDuPort;
    EtatSurface dernierEnvoye;
    bool etatEnvoye = false;
    std::shared_ptr<std::atomic<bool>> vivant = std::make_shared<std::atomic<bool>> (true);

    void timerCallback() override;
    void chercher();
    void fermer();
    void handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage&) override;
    void initialiser();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PushMidi)
};

} // namespace groovebox
