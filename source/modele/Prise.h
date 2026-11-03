#pragma once

#include "Morceau.h"

namespace groovebox
{

/** Un geste noté pendant une prise, au temps du moteur où il a été fait. */
struct EvenementDePrise
{
    enum class Genre
    {
        scene,              ///< Lancement d'une scène (piste -1) ou d'un clip (piste >= 0), avec son enchaînement
        arret,              ///< Arrêt d'une piste (piste >= 0) ou de tous les clips (piste -1)
        preparation,        ///< Shift + scène : enchaînement à la main préparé
        melange,            ///< Position de la bande pendant un enchaînement à la main
        filtreGlobal,       ///< Position de la bande en filtre global
        envoiMomentane,     ///< Bouton du haut tenu ou lâché
        note,               ///< Note jouée en direct (vélocité 0 : relâchée)
        reglages            ///< Changements du morceau (macros, générateurs, volumes, tempo, mute, solo...)
    };

    Genre genre = Genre::scene;
    double temps = 0.0;                     ///< Temps du moteur au moment du geste
    std::optional<double> cale;             ///< Lancements : temps du moteur où ils prennent effet
    int piste = -1, scene = -1;
    std::optional<Enchainement> enchainement;
    float valeur = 0.0f;                    ///< Bande (0 à 1), note, envoi tenu (1) ou lâché (0)
    int velocite = 0;
    juce::var changements;                  ///< Réglages : liste de [chemin, valeur] dans le JSON du morceau

    juce::var versJson() const;
    static std::optional<EvenementDePrise> depuisJson (const juce::var&);
};

/** Une prise : tout ce qui a été joué entre deux appuis sur Record, de quoi la réécouter et la rejouer. */
struct Prise
{
    static constexpr int versionDuFormat = 1;

    juce::String nom;
    juce::Time date;
    double debut = 0.0, fin = 0.0;          ///< Temps du moteur (début calé sur une mesure)
    Morceau morceauDeDepart;                ///< Le morceau tel qu'il était au début de la prise
    std::array<int, nombreDePistes> scenesDeDepart;   ///< Scène qui jouait sur chaque piste au début (-1 : rien)
    double debutDuSon = 0.0;                ///< Instant de la ligne du temps (secondes) du premier échantillon des fichiers son
    bool avecMicros = false;
    std::vector<EvenementDePrise> evenements;

    Prise()                                 { scenesDeDepart.fill (-1); }

    /** Durée en secondes, au tempo de départ (pour l'affichage). */
    double dureeEnSecondes() const;

    juce::var versJson() const;
    static std::optional<Prise> depuisJson (const juce::var&);

    bool enregistrer (const juce::File&) const;
    static std::optional<Prise> charger (const juce::File&);
};

/** Écarts entre deux JSON du morceau, pour noter les réglages sans recopier tout le morceau à chaque cran d'encodeur. */
namespace ecarts
{
    /** Liste de [chemin, nouvelle valeur] ; les chemins sont du genre « pistes/2/macros/3 ». */
    juce::Array<juce::var> entre (const juce::var& avant, const juce::var& apres);

    /** Applique des écarts : renvoie le JSON modifié. */
    juce::var appliquer (const juce::var& json, const juce::var& liste);
}

} // namespace groovebox
