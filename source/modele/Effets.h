#pragma once

#include "Texte.h"

namespace groovebox
{

/** Les effets d'insertion : 3 emplacements par piste. */
enum class TypeEffet { filtre, saturation, lofi, compresseur, echo, chorus, distorsion, punch, grave };
constexpr int nombreDeTypesDEffet = 9;
juce::String nomDeLEffet (TypeEffet);

/** Un effet d'insertion et ses réglages (0 à 1 ; le filtre est neutre à 0,5). */
struct Effet
{
    TypeEffet type = TypeEffet::filtre;
    std::array<float, 4> reglages { 0.5f, 0.0f, 0.5f, 0.5f };
    bool actif = true;

    static Effet parDefaut (TypeEffet);
    static juce::StringArray nomsDesReglages (TypeEffet);

    juce::var versJson() const;
    static std::optional<Effet> depuisJson (const juce::var&);

    bool operator== (const Effet&) const = default;
};

/** Le delay dub, effet de départ partagé. */
struct DelayDub
{
    static constexpr std::array<double, 6> divisions { 0.75, 1.0, 0.5, 2.0 / 3.0, 1.0 / 3.0, 2.0 };   ///< En noires
    static juce::StringArray nomsDesDivisions();

    int division = 0;           ///< Croche pointée par défaut
    float retour = 0.45f;       ///< 0 à 1,2 : au-delà de 1, l'écho s'emballe (sans jamais dépasser la protection)
    float filtre = 0.35f;       ///< Comme le filtre d'insertion : 0 passe-bas, 0,5 neutre, 1 passe-haut
    float saturation = 0.2f;

    bool operator== (const DelayDub&) const = default;
};

/** La réverbération, effet de départ partagé. */
struct Reverberation
{
    float taille = 0.5f, duree = 0.5f, couleur = 0.5f;
    bool operator== (const Reverberation&) const = default;
};

} // namespace groovebox
