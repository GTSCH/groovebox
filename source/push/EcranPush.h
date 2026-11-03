#pragma once

#include "Surface.h"

struct libusb_context;
struct libusb_device_handle;

namespace groovebox
{

/** Image au format de l'écran du Push 2 : 160 lignes de 2048 octets (960 pixels BGR 5-6-5, puis 128 octets de bourrage),
    brouillées par le motif 0xFFE7F3E7 imposé par le Push. */
namespace formatEcran
{
    constexpr int octetsParLigne = 2048;
    constexpr int tailleImage = octetsParLigne * EtatSurface::hauteurEcran;
    constexpr std::array<juce::uint8, 16> entete { 0xFF, 0xCC, 0xAA, 0x88, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
    constexpr std::array<juce::uint8, 4> masque { 0xE7, 0xF3, 0xE7, 0xFF };

    juce::uint16 pixel (juce::Colour);
    void convertir (const juce::Image&, juce::uint8* destination);
}

/** L'écran du vrai Push, par USB direct. Un fil dédié envoie la dernière image 60 fois par seconde ;
    il cherche le Push toutes les 2 secondes tant qu'il n'est pas branché. */
class EcranPush  : public ClientSurface,
                   private juce::Thread
{
public:
    EcranPush();
    ~EcranPush() override;

    void afficher (const EtatSurface&, const juce::Image&) override;

    bool estConnecte() const                { return connecte; }

    /** Vrai si le Push est branché mais qu'une autre application (Ableton Live) le garde. */
    bool estPrisAilleurs() const            { return prisAilleurs; }

private:
    libusb_context* contexte = nullptr;
    libusb_device_handle* appareil = nullptr;
    std::atomic<bool> connecte { false }, prisAilleurs { false };

    juce::CriticalSection verrou;
    std::vector<juce::uint8> imagePrete, imageEnvoyee;
    bool nouvelleImage = false;

    void run() override;
    bool ouvrir();
    void fermer();
    bool envoyer (const std::vector<juce::uint8>&);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EcranPush)
};

} // namespace groovebox
