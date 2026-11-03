#pragma once

#include "Texte.h"

namespace groovebox
{

/** Les façons de passer d'un clip à l'autre (ou d'une scène à l'autre). */
enum class TypeEnchainement { coupe, fondu, echangeDesBasses, filtre, queueDEcho };
constexpr int nombreDeTypesDEnchainement = 5;
juce::String nomDeLEnchainement (TypeEnchainement);

/** Un enchaînement et ses réglages. */
struct Enchainement
{
    static constexpr std::array<int, 4> dureesPossibles { 1, 2, 4, 8 };

    TypeEnchainement type = TypeEnchainement::coupe;
    int mesures = 4;                ///< Durée : 1, 2, 4 ou 8 mesures (sans effet sur la coupe)
    bool batterieNette = true;      ///< Les pistes batterie basculent net sur la mesure
    float intensite = 0.7f;         ///< Profondeur du filtre, quantité d'écho
    bool montee = false;            ///< Filtre et réverbération qui montent sur la dernière mesure (livraison 8b)

    /** Vrai si cette piste bascule net (coupe, ou batterie avec « batterie nette »). */
    bool coupeNette (bool batterie) const     { return type == TypeEnchainement::coupe || (batterie && batterieNette); }

    juce::var versJson() const;
    static std::optional<Enchainement> depuisJson (const juce::var&);

    bool operator== (const Enchainement&) const = default;
};

/** Ce que fait une voix à un moment de l'enchaînement. */
struct FormeDeVoix
{
    float gain = 1.0f;          ///< Volume de la voix (0 à 1)
    float passeBas = 1.0f;      ///< 1 : ouvert ; 0 : fermé (60 Hz)
    float passeHaut = 0.0f;     ///< 0 : ouvert ; 1 : fermé (2,5 kHz)
    float sansGraves = 0.0f;    ///< 1 : graves coupés (sous 180 Hz)
    float envoi = 0.0f;         ///< Queue d'écho : part de la voix envoyée au delay et à la réverbération

    bool estNeutre() const      { return passeBas > 0.999f && passeHaut < 0.001f && sansGraves < 0.001f; }
};

/** Forme d'une voix, sortante ou entrante, à l'avancée p (0 au départ, 1 à la fin). */
FormeDeVoix formeDeLaVoix (TypeEnchainement, bool entrante, float p, float intensite);

} // namespace groovebox
