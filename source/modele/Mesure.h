#pragma once

#include "Texte.h"

namespace groovebox
{

/** Rôle d'un coup de clic dans la mesure. */
enum class TypeDeTemps
{
    premier,     ///< Premier temps de la mesure
    groupe,      ///< Début d'un groupe (temps fort du groupement)
    autre,       ///< Autre temps
    subdivision  ///< Subdivision (en option)
};

/** Un coup de clic : position dans la mesure, en temps du moteur (l'unité du bas de la mesure). */
struct CoupDeClic
{
    double position = 0.0;
    TypeDeTemps type = TypeDeTemps::autre;
};

/** Mesure et groupement des temps forts. En 7/8 2+2+3, il y a 7 temps (des croches) en 3 groupes. */
struct Mesure
{
    int numerateur = 4;
    int denominateur = 4;
    std::vector<int> groupement { 4 };

    /** Mesure avec son groupement habituel (7/8 en 2+2+3, 6/8 en 3+3...). */
    static Mesure standard (int numerateur, int denominateur);

    /** Mesures du menu rapide, dans l'ordre du cahier des charges. */
    static std::vector<Mesure> mesuresProposees();

    /** Groupements proposés pour une mesure ; le premier est celui par défaut. */
    static std::vector<std::vector<int>> groupementsProposes (int numerateur, int denominateur);

    /** Lit « 2+2+3 ». Renvoie rien si le texte n'est pas un groupement. */
    static std::optional<std::vector<int>> lireGroupement (const juce::String&);

    bool estValide() const;
    juce::String nom() const;                  ///< « 7/8 »
    juce::String nomGroupement() const;        ///< « 2+2+3 »
    static juce::String nomGroupement (const std::vector<int>&);

    /** Pas de grille par défaut des clips : 16 (double-croche) en x/2 et x/4, sinon le bas de la mesure. */
    int grilleParDefaut() const;

    /** Rôle de chaque temps de la mesure. */
    std::vector<TypeDeTemps> typesDesTemps() const;

    /** Coups de clic d'une mesure. En mode « un clic par groupe », seul le début de chaque groupe sonne. */
    std::vector<CoupDeClic> motifDeClic (bool unClicParGroupe, bool subdivisions) const;

    bool operator== (const Mesure&) const = default;
};

} // namespace groovebox
