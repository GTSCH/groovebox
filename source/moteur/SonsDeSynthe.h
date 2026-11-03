#pragma once

#include "Surge.h"

namespace groovebox
{

/** Un réglage de Surge : un texte pour les choix (« LP 24 dB »), un nombre pour le reste, dans l'unité affichée
    par Surge (Hz, dB, %, demi-tons ; les temps en millisecondes). */
struct ReglageSurge
{
    juce::String parametre;
    juce::String texte;
    double nombre = 0.0;
};

/** Une macro relie un encodeur à un ou plusieurs réglages de Surge, de « de » (macro à 0) à « a » (macro à 1). */
struct LiaisonDeMacro
{
    juce::String parametre;
    double de = 0.0, a = 1.0;
};

struct MacroDeSynthe
{
    juce::String nom;
    float depart = 0.5f;
    std::vector<LiaisonDeMacro> liaisons;
};

/** Un son de départ : un style, des réglages de base, 8 macros. */
struct SonDeSynthe
{
    juce::String nom, style;
    std::vector<ReglageSurge> reglages;
    std::array<MacroDeSynthe, 8> macros;
};

namespace sonsDeSynthe
{
    const std::vector<SonDeSynthe>& tous();
    const SonDeSynthe* trouver (const juce::String& nom);
    juce::StringArray styles();
    juce::StringArray sonsDuStyle (const juce::String& style);

    /** Son proposé par défaut pour un son provisoire de piste (basse, nappe...). */
    juce::String sonParDefaut (int sonProvisoire);
}

/** Applique sons et macros à une instance de Surge, en trouvant pour chaque réglage la valeur interne qui donne le texte ou le nombre voulu. */
class PiloteDeSurge
{
public:
    explicit PiloteDeSurge (te::Plugin&);

    bool appliquerSon (const SonDeSynthe&, const std::array<float, 8>& macros);
    void appliquerMacros (const SonDeSynthe&, const std::array<float, 8>& macros);

    /** Valeur interne (0 à 1) d'un réglage pour un texte ou un nombre ; vide si le réglage n'existe pas. */
    std::optional<float> valeurPourTexte (const juce::String& parametre, const juce::String& texte);
    std::optional<float> valeurPourNombre (const juce::String& parametre, double nombre);

    /** Texte affiché par Surge pour un réglage, tel qu'il est maintenant. */
    juce::String texteActuel (const juce::String& parametre);

private:
    te::Plugin& plugin;
    std::map<juce::String, te::AutomatableParameter*> parTracktion;
    std::map<juce::String, juce::AudioProcessorParameter*> parJuce;

    void regler (const juce::String& parametre, float valeur);
};

} // namespace groovebox
