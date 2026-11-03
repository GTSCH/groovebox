#pragma once

#include "Prise.h"

namespace groovebox
{

/** Une section d'un déroulé : une scène jouée un certain temps, avec ses gestes et ses mouvements de réglages. */
struct Section
{
    juce::String nom;
    int scene = -1;                         ///< -1 : la section de départ, prise en pleine lecture (rien n'est lancé)
    std::optional<Enchainement> entree;     ///< Comment on entre dans la section ; vide : « suite », la scène continue
    bool aLaMain = false;                   ///< Ouverte par Shift + scène : les mélanges à la bande suivent dans les gestes
    bool ouverte = false;                   ///< Au rejeu, tourne en boucle jusqu'à ce qu'on passe à la suite (livraison 9c)
    double longueur = 0.0;                  ///< En temps du moteur
    double avance = 0.0;                    ///< Le lancement a été demandé tant de temps avant le début de la section
    juce::var etat;                         ///< Le morceau (JSON) au début de la section
    std::array<int, nombreDePistes> scenesDeDepart;   ///< Ce qui joue sur chaque piste au début de la section
    std::vector<EvenementDePrise> evenements;   ///< Gestes, en temps (et calage) comptés depuis le début de la section

    // D'où vient le son, pour l'écouter
    juce::String prise;                     ///< Dossier de la prise (son nom de fichier)
    double debutDansLaPrise = 0.0;          ///< Temps du moteur dans la prise

    Section()                               { scenesDeDepart.fill (-1); }

    /** Temps par mesure, d'après l'état de la section. */
    int tempsParMesure() const;
    double mesures() const                  { return longueur / juce::jmax (1, tempsParMesure()); }

    juce::var versJson() const;
    static Section depuisJson (const juce::var&);
};

/** Un déroulé : une suite ordonnée de sections, tirée d'une prise retouchée. Enregistré à côté du morceau. */
struct Deroule
{
    static constexpr int versionDuFormat = 1;

    juce::String nom;
    std::vector<Section> sections;

    double longueur() const;
    double debutDeLaSection (int i) const;

    juce::var versJson() const;
    static std::optional<Deroule> depuisJson (const juce::var&);
    bool enregistrer (const juce::File&) const;
    static std::optional<Deroule> charger (const juce::File&);
};

/** Retouche : découper une prise en sections, puis couper, supprimer, déplacer, répéter, lisser ou effacer une courbe. */
namespace retouche
{
    /** Une section par scène jouée (par lancement de scène, ou par Shift + scène), depuis le début de la prise jusqu'à sa fin. */
    Deroule decouper (const Prise&, const juce::String& dossierDeLaPrise);

    /** Coupe une section en deux à un temps compté depuis son début ; la seconde moitié est une « suite ». */
    bool couper (Deroule&, int section, double temps);
    bool supprimer (Deroule&, int section);
    bool deplacer (Deroule&, int section, int sens);     ///< sens -1 : vers le début ; +1 : vers la fin
    bool repeter (Deroule&, int section);                ///< La section est recopiée juste après elle

    /** Les réglages qui bougent dans une section, pour une piste (-1 : réglages du morceau entier). */
    juce::StringArray courbes (const Section&, int piste);

    /** Les points d'une courbe : (temps depuis le début de la section, valeur), en commençant par la valeur de départ. */
    std::vector<std::pair<double, double>> points (const Section&, const juce::String& chemin);

    /** Lisser : moyenne glissante sur un temps ; la dernière valeur ne bouge pas, la section suivante s'enchaîne sans saut. */
    void lisser (Section&, const juce::String& chemin);

    /** Effacer : le réglage garde sa valeur de départ pendant toute la section. */
    void effacer (Section&, const juce::String& chemin);

    /** Nom lisible d'un réglage, par exemple « Macro 4 » pour /pistes/2/macros/3. */
    juce::String nomDuReglage (const juce::String& chemin);

    /** Piste d'un chemin (/pistes/N/...), ou -1. */
    int pisteDuChemin (const juce::String& chemin);
}

} // namespace groovebox
