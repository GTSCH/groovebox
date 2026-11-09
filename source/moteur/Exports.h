#pragma once

#include "Moteur.h"
#include "modele/Deroule.h"

namespace groovebox
{

/** Exports en WAV 48 kHz 24 bits, calculés plus vite que la lecture, sans carte son (une carte simulée à 4 sorties).
    Chaque rendu rejoue le morceau dans un moteur à part : un déroulé du début à la fin, ou une scène sur des mesures choisies. */
namespace exports
{
    enum class Rendu { complet, sansGuide, sansGuideAvecClic, pistes };
    juce::String nomDuRendu (Rendu, bool avecGuide);

    struct Demande
    {
        Morceau morceau;
        std::optional<Deroule> deroule;     ///< Sinon : la scène, sur « mesures » mesures
        int scene = 0;
        int mesures = 8;
        std::vector<Rendu> rendus { Rendu::sansGuideAvecClic };
        juce::File dossier;                 ///< Où écrire les fichiers
        juce::String nom;                   ///< Début du nom des fichiers, par exemple « Déroulé Concert »

        juce::var versJson() const;
        static Demande depuisJson (const juce::var&);
    };

    struct Resultat
    {
        juce::StringArray fichiers;
        juce::StringArray remarques;        ///< Ce qui n'a pas été fait, et pourquoi
        double secondesDeMusique = 0.0, secondesDeCalcul = 0.0;
    };

    /** Fil principal. La progression reçoit l'avancée (0 à 1) et ce qui se calcule. Un fichier du même nom est remplacé. */
    Resultat exporter (te::Engine&, const Demande&, BanqueDeSons* = nullptr,
                       std::function<void (float, const juce::String&)> progression = {});

    /** Le morceau tel que le joue un rendu : les changements s'appliquent aussi à l'état de chaque section du déroulé. */
    void modifierPartout (Morceau&, std::optional<Deroule>&, const std::function<void (Morceau&)>&);

    /** Copie d'une prise de répétition : sortie.wav (et micros.wav s'il y en a) ; renvoie les fichiers écrits. */
    juce::StringArray copierLaPrise (const juce::File& dossierDeLaPrise, const juce::File& dossier, const juce::String& nom);
}

} // namespace groovebox
