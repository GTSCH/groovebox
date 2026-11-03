#include "EcranPush.h"
#include <libusb.h>

namespace groovebox
{

namespace formatEcran
{
    juce::uint16 pixel (juce::Colour c)
    {
        // Bits 15 à 11 : bleu, 10 à 5 : vert, 4 à 0 : rouge
        return (juce::uint16) (((c.getBlue() >> 3) << 11) | ((c.getGreen() >> 2) << 5) | (c.getRed() >> 3));
    }

    void convertir (const juce::Image& image, juce::uint8* destination)
    {
        const juce::Image::BitmapData pixels (image, juce::Image::BitmapData::readOnly);

        for (int y = 0; y < EtatSurface::hauteurEcran; ++y)
        {
            auto ligne = destination + y * octetsParLigne;

            for (int x = 0; x < EtatSurface::largeurEcran; ++x)
            {
                const auto valeur = pixel (pixels.getPixelColour (x, y));
                ligne[2 * x] = (juce::uint8) (valeur & 0xff);           // octet de poids faible d'abord
                ligne[2 * x + 1] = (juce::uint8) (valeur >> 8);
            }

            std::fill (ligne + 2 * EtatSurface::largeurEcran, ligne + octetsParLigne, (juce::uint8) 0);

            for (int i = 0; i < octetsParLigne; ++i)
                ligne[i] ^= masque[(size_t) (i % 4)];
        }
    }
}

namespace
{
    constexpr int vendeurAbleton = 0x2982, produitPush2 = 0x1967;
    constexpr unsigned char pointDeSortie = 0x01;
    constexpr int morceauUsb = 16384;
}

EcranPush::EcranPush()  : juce::Thread ("Écran du Push"_fr)
{
    imagePrete.resize (formatEcran::tailleImage, 0);
    imageEnvoyee.resize (formatEcran::tailleImage, 0);

    if (libusb_init (&contexte) < 0)
    {
        contexte = nullptr;
        juce::Logger::writeToLog ("libusb ne démarre pas : l'écran du Push restera éteint"_fr);
        return;
    }

    startThread (juce::Thread::Priority::normal);
}

EcranPush::~EcranPush()
{
    stopThread (2000);
    fermer();

    if (contexte != nullptr)
        libusb_exit (contexte);
}

void EcranPush::afficher (const EtatSurface&, const juce::Image& image)
{
    // Conversion sur le fil principal (environ 1 ms), puis le fil de l'écran prend la dernière image prête
    std::vector<juce::uint8> tampon ((size_t) formatEcran::tailleImage);
    formatEcran::convertir (image, tampon.data());

    const juce::ScopedLock sl (verrou);
    imagePrete.swap (tampon);
    nouvelleImage = true;
}

bool EcranPush::ouvrir()
{
    libusb_device** liste = nullptr;
    const auto nombre = libusb_get_device_list (contexte, &liste);
    bool trouve = false, ouvert = false;

    for (ssize_t i = 0; i < nombre && ! ouvert; ++i)
    {
        libusb_device_descriptor descripteur {};

        if (libusb_get_device_descriptor (liste[i], &descripteur) < 0
            || descripteur.idVendor != vendeurAbleton || descripteur.idProduct != produitPush2)
            continue;

        trouve = true;

        if (libusb_open (liste[i], &appareil) < 0)
        {
            appareil = nullptr;
            continue;
        }

        if (libusb_claim_interface (appareil, 0) < 0)
        {
            libusb_close (appareil);
            appareil = nullptr;
            continue;
        }

        ouvert = true;
    }

    if (liste != nullptr)
        libusb_free_device_list (liste, 1);

    prisAilleurs = trouve && ! ouvert;

    if (ouvert && ! connecte)
        juce::Logger::writeToLog ("Écran du Push ouvert"_fr);

    connecte = ouvert;
    return ouvert;
}

void EcranPush::fermer()
{
    if (appareil != nullptr)
    {
        libusb_release_interface (appareil, 0);
        libusb_close (appareil);
        appareil = nullptr;
        juce::Logger::writeToLog ("Écran du Push fermé"_fr);
    }

    connecte = false;
}

bool EcranPush::envoyer (const std::vector<juce::uint8>& image)
{
    int transferes = 0;
    auto entete = formatEcran::entete;

    if (libusb_bulk_transfer (appareil, pointDeSortie, entete.data(), (int) entete.size(), &transferes, 1000) != 0)
        return false;

    for (int debut = 0; debut < formatEcran::tailleImage; debut += morceauUsb)
    {
        auto donnees = const_cast<juce::uint8*> (image.data()) + debut;

        if (libusb_bulk_transfer (appareil, pointDeSortie, donnees, morceauUsb, &transferes, 1000) != 0)
            return false;
    }

    return true;
}

void EcranPush::run()
{
    auto prochaineRecherche = juce::Time::getMillisecondCounter();

    while (! threadShouldExit())
    {
        if (appareil == nullptr)
        {
            if (juce::Time::getMillisecondCounter() >= prochaineRecherche)
            {
                ouvrir();
                prochaineRecherche = juce::Time::getMillisecondCounter() + 2000;
            }

            if (appareil == nullptr)
            {
                wait (100);
                continue;
            }
        }

        const auto debut = juce::Time::getMillisecondCounterHiRes();

        {
            const juce::ScopedLock sl (verrou);

            if (nouvelleImage)
            {
                imageEnvoyee = imagePrete;
                nouvelleImage = false;
            }
        }

        // On renvoie l'image même inchangée : sans image pendant 2 s, le Push éteint son écran
        if (! envoyer (imageEnvoyee))
        {
            juce::Logger::writeToLog ("Écran du Push perdu (débranché ?)"_fr);
            fermer();
            prochaineRecherche = juce::Time::getMillisecondCounter() + 2000;
            continue;
        }

        const auto duree = juce::Time::getMillisecondCounterHiRes() - debut;
        wait (juce::jmax (1, (int) (1000.0 / 60.0 - duree)));
    }
}

} // namespace groovebox
