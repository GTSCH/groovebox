#pragma once

#include "Morceau.h"

namespace groovebox
{

/** Les morceaux sur le disque : un dossier par morceau dans Musique/groovebox/Morceaux,
    avec morceau.json et un sous-dossier « sauvegardes » (une copie toutes les 5 minutes de jeu, les 20 dernières).
    Un marqueur signale une fermeture brusque de l'application. */
class Bibliotheque
{
public:
    explicit Bibliotheque (juce::File racine = racineParDefaut());

    static juce::File racineParDefaut();
    const juce::File& getRacine() const                 { return racine; }

    juce::StringArray noms() const;
    bool existe (const juce::String& nom) const;
    juce::File dossierDe (const juce::String& nom) const;
    juce::String nomLibre (const juce::String& base) const;

    std::optional<Morceau> ouvrir (const juce::String& nom) const;

    /** Écrit morceau.json (sans jamais laisser un fichier à moitié écrit), et une copie de secours si la dernière a plus de 5 minutes. */
    bool enregistrer (const Morceau&);

    /** Renomme le dossier d'un morceau ; faux si le nouveau nom est déjà pris. */
    bool renommer (const juce::String& ancien, const juce::String& nouveau);

    juce::String dernierOuvert() const;
    void retenirDernier (const juce::String& nom);

    /** À l'ouverture de l'application : vrai si la séance précédente ne s'est pas fermée normalement. */
    bool marquerOuverture();
    void marquerFermeture();

    juce::StringArray copiesDeSecours (const juce::String& nom) const;

    static constexpr int nombreDeCopies = 20;
    static constexpr double intervalleDesCopies = 5.0 * 60.0;   // secondes

private:
    juce::File racine;
    std::map<juce::String, double> derniereCopie;

    static juce::String nettoyer (const juce::String& nom);
};

} // namespace groovebox
