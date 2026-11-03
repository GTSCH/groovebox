#pragma once

#include "Texte.h"

namespace groovebox
{
namespace te = tracktion;

/** Surge XT, le synthé hébergé : on le cherche dans les dossiers de plugins du Mac (format VST3),
    puis on le règle par ses paramètres nommés, sans jamais ouvrir sa fenêtre. */
namespace surge
{
    /** Pour les essais : sans Surge, les moteurs se créent bien plus vite (son provisoire). Autorisé par défaut. */
    void autoriser (bool);

    /** Description du plugin, si Surge XT est installé (et autorisé). Cherché une fois, puis gardé. */
    std::optional<juce::PluginDescription> trouver (te::Engine&);

    /** Où on l'a cherché, pour les messages. */
    juce::StringArray dossiersCherches();

    /** Crée une instance de Surge XT pour un Edit ; nul si Surge manque ou ne se charge pas. */
    te::Plugin::Ptr creer (te::Edit&);

    /** Paramètres d'une instance, par nom (« A Filter 1 Cutoff »...). */
    std::map<juce::String, te::AutomatableParameter*> parametres (te::Plugin&);
}

} // namespace groovebox
