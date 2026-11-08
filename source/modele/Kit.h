#pragma once

#include "Texte.h"

namespace groovebox
{

/** Un pad de la batterie : un son (fichier WAV du dossier Sons) et ses 4 réglages. */
struct PadBatterie
{
    juce::String son;               ///< Chemin relatif au dossier Sons, par exemple « Grosses caisses/Analogique.wav »
    float hauteur = 0.0f;           ///< Demi-tons, de -24 à +24
    float decroissance = 1.0f;      ///< 1 : le son entier ; vers 0 : de plus en plus court
    float saturation = 0.0f;        ///< 0 à 1
    float couleur = 0.0f;           ///< -1 sombre, 0 neutre, +1 brillant
    float volumeDb = 0.0f;
    int etouffement = 0;            ///< Groupe d'étouffement (0 : aucun) ; un coup coupe les autres pads du même groupe

    bool operator== (const PadBatterie&) const = default;
};

/** Le kit d'une piste batterie : 16 pads (notes 36 à 51) et 2 réglages d'ensemble. */
struct Kit
{
    static constexpr int nombreDePads = 16, premiereNote = 36;

    juce::String nom;
    std::array<PadBatterie, nombreDePads> pads;
    float compression = 0.0f;       ///< 0 à 1
    float largeur = 0.5f;           ///< 0 (tout au centre) à 1 (pads écartés)

    juce::var versJson() const;
    static Kit depuisJson (const juce::var&);

    bool operator== (const Kit&) const = default;
};

/** Les kits de départ, fabriqués par le logiciel au premier lancement (voir FabriqueDeSons). */
namespace kitsDeDepart
{
    /** Nom de chaque pad, dans l'ordre des notes 36 à 51. */
    const std::array<juce::String, Kit::nombreDePads>& nomsDesPads();

    /** Dossier (catégorie) de chaque pad dans Sons. */
    const std::array<juce::String, Kit::nombreDePads>& categoriesDesPads();

    juce::StringArray noms();               ///< Analogique, Minimal, Dub, Hard techno
    Kit kit (const juce::String& nom);

    /** Nom à afficher pour un pad : l'instrument d'un kit acoustique, sinon le nom du pad. */
    juce::String nomDuPad (const Kit&, int pad);
}

/** Les kits acoustiques (AVL Drumkits, licence CC BY-SA 3.0), installés dans Sons/Batterie acoustique par
    scripts/installer-batterie-acoustique.sh. Chaque instrument est un dossier de 1 à 5 niveaux de frappe (1.wav, le plus doux). */
namespace kitsAcoustiques
{
    inline const juce::String dossier { "Batterie acoustique" };

    juce::StringArray noms();               ///< Black Pearl, Blonde Bop, Blonde Bop fagots, Red Zeppelin, Cajón
    const std::array<juce::String, Kit::nombreDePads>& instruments (const juce::String& kit);
    Kit kit (const juce::String& nom);

    /** Kit utilisé par la batterie guide quand il est installé. */
    inline const juce::String kitDeLaGuide { "Black Pearl" };
}

} // namespace groovebox
