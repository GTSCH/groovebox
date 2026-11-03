#include "SonsDeSynthe.h"
#include "modele/Morceau.h"

namespace groovebox
{

namespace
{
    // Réglages communs à tous les sons : Surge repart d'un état connu (un seul oscillateur, rien de branché)
    const std::vector<ReglageSurge>& socle()
    {
        static const std::vector<ReglageSurge> s {
            { "A Osc 1 Type", "Classic" }, { "A Osc 1 Mute", "Off" }, { "A Osc 2 Mute", "On" }, { "A Osc 3 Mute", "On" },
            { "A Noise Mute", "On" }, { "A Ring Modulation 1x2 Mute", "On" }, { "A Ring Modulation 2x3 Mute", "On" },
            { "A Osc 1 Unison Voices", "1 voice" }, { "A Osc 2 Unison Voices", "1 voice" },
            { "A Filter 1 Type", "Off" }, { "A Filter 2 Type", "Off" }, { "A Waveshaper Type", "Off" },
            { "A Play Mode", "Poly" }, { "A Portamento", {}, 0.0 }, { "A Highpass", {}, 6.88 },
            { "A Amp EG Attack", {}, 0.0 }, { "A Amp EG Decay", {}, 250.0 }, { "A Amp EG Sustain", {}, 100.0 }, { "A Amp EG Release", {}, 30.0 },
            { "A Filter EG Attack", {}, 0.0 }, { "A Filter EG Decay", {}, 250.0 }, { "A Filter EG Sustain", {}, 0.0 }, { "A Filter EG Release", {}, 100.0 },
            { "A Filter 1 FEG Mod Amount", {}, 0.0 }, { "A Filter 1 Resonance", {}, 0.0 }, { "A Osc 1 Octave", "0" }, { "A Osc 2 Octave", "0" },
            { "A Osc 1 Shape", {}, 0.0 }, { "A Osc 1 Sub Mix", {}, 0.0 }, { "A Osc 2 Pitch", {}, 0.0 }, { "A Volume", {}, -6.0 },
            { "Global Volume", {}, -4.0 },      // équilibré avec la batterie dans la démo
            { "FX A1 FX Type", "Off" }, { "FX A2 FX Type", "Off" }, { "FX A3 FX Type", "Off" }, { "FX A4 FX Type", "Off" },
            { "FX S1 FX Type", "Off" }, { "FX S2 FX Type", "Off" }, { "FX S3 FX Type", "Off" }, { "FX S4 FX Type", "Off" },
            { "FX G1 FX Type", "Off" }, { "FX G2 FX Type", "Off" }, { "FX G3 FX Type", "Off" }, { "FX G4 FX Type", "Off" } };
        return s;
    }

    // Macros types, réutilisées d'un son à l'autre avec leurs propres plages
    MacroDeSynthe ouverture (double de, double a, float depart = 0.4f)    { return { "Ouverture", depart, { { "A Filter 1 Cutoff", de, a } } }; }
    MacroDeSynthe resonance (double a = 85.0, float depart = 0.2f)        { return { "Résonance"_fr, depart, { { "A Filter 1 Resonance", 0.0, a } } }; }
    MacroDeSynthe grain (double a = 18.0, float depart = 0.2f)            { return { "Grain", depart, { { "A Waveshaper Drive", -6.0, a } } }; }
    MacroDeSynthe enveloppe (double a = 48.0, float depart = 0.4f)        { return { "Enveloppe", depart, { { "A Filter 1 FEG Mod Amount", 0.0, a } } }; }
    MacroDeSynthe decroissance (double de, double a, float depart = 0.5f) { return { "Décroissance"_fr, depart, { { "A Filter EG Decay", de, a }, { "A Amp EG Decay", de * 2.0, a * 2.0 } } }; }
    MacroDeSynthe attaque (double a = 2000.0, float depart = 0.0f)        { return { "Attaque", depart, { { "A Amp EG Attack", 0.0, a } } }; }
    MacroDeSynthe relache (double de, double a, float depart = 0.3f)      { return { "Relâche"_fr, depart, { { "A Amp EG Release", de, a } } }; }
    MacroDeSynthe epaisseur (float depart = 0.3f)                         { return { "Épaisseur"_fr, depart, { { "A Osc 1 Unison Detune", 0.0, 40.0 }, { "A Osc 2 Pitch", 0.0, 0.25 } } }; }
    MacroDeSynthe forme (float depart = 0.5f)                             { return { "Forme", depart, { { "A Osc 1 Shape", -100.0, 100.0 } } }; }
    MacroDeSynthe sub (float depart = 0.3f)                               { return { "Sub", depart, { { "A Osc 1 Sub Mix", 0.0, 100.0 } } }; }
    MacroDeSynthe glisse (float depart = 0.0f)                            { return { "Glissé"_fr, depart, { { "A Portamento", 0.0, 250.0 } } }; }
    MacroDeSynthe volume (float depart = 0.8f)                            { return { "Volume", depart, { { "A Volume", -24.0, 0.0 } } }; }

    std::vector<SonDeSynthe> fabriquer()
    {
        std::vector<SonDeSynthe> s;

        auto ajouter = [&s] (juce::String nom, juce::String style, std::vector<ReglageSurge> propres, std::array<MacroDeSynthe, 8> macros)
        {
            auto reglages = socle();
            reglages.insert (reglages.end(), propres.begin(), propres.end());
            s.push_back ({ nom, style, reglages, macros });
        };

        // Dub : rond, profond, peu de grain
        ajouter ("Sub dub", "Dub", { { "A Osc 1 Type", "Sine" }, { "A Play Mode", "Mono" }, { "A Osc 1 Octave", "-1" },
                                     { "A Filter 1 Type", "LP 12 dB" }, { "A Amp EG Release", {}, 120.0 }, { "A Volume", {}, -3.0 } },
                 { ouverture (120.0, 1500.0, 0.5f), grain (12.0, 0.0f), sub (0.0f), decroissance (100.0, 1500.0, 0.8f),
                   attaque (80.0), relache (30.0, 600.0), glisse(), volume() });
        ajouter ("Basse dub ronde", "Dub", { { "A Osc 1 Shape", {}, -100.0 }, { "A Play Mode", "Mono" }, { "A Osc 1 Octave", "-1" },
                                             { "A Filter 1 Type", "LP 24 dB" }, { "A Filter EG Sustain", {}, 30.0 }, { "A Volume", {}, -4.0 } },
                 { ouverture (90.0, 1200.0, 0.3f), resonance (50.0, 0.1f), enveloppe (36.0, 0.3f), decroissance (100.0, 1200.0, 0.6f),
                   sub (0.6f), grain (12.0, 0.0f), glisse(), volume() });
        ajouter ("Skank", "Dub", { { "A Osc 1 Shape", {}, 0.0 }, { "A Filter 1 Type", "BP 12 dB" }, { "A Amp EG Sustain", {}, 0.0 }, { "A Amp EG Decay", {}, 180.0 } },
                 { ouverture (400.0, 3500.0, 0.5f), resonance (60.0, 0.3f), decroissance (40.0, 400.0, 0.3f), forme(),
                   epaisseur (0.2f), grain (10.0, 0.0f), relache (10.0, 300.0, 0.1f), volume() });

        // Drum & bass : reese et basses mouvantes
        ajouter ("Reese", "Drum & bass", { { "A Osc 1 Unison Voices", "3 voices" }, { "A Osc 2 Mute", "Off" }, { "A Play Mode", "Mono" },
                                           { "A Osc 1 Octave", "-1" }, { "A Osc 2 Octave", "-1" }, { "A Filter 1 Type", "LP OB-Xd 24 dB" } },
                 { ouverture (150.0, 4000.0, 0.35f), resonance (60.0, 0.15f), epaisseur (0.5f), grain (20.0, 0.3f),
                   forme (0.3f), sub (0.4f), glisse (0.2f), volume (0.75f) });
        ajouter ("Basse neuro", "Drum & bass", { { "A Osc 1 Unison Voices", "2 voices" }, { "A Play Mode", "Mono" }, { "A Osc 1 Octave", "-1" },
                                                 { "A Filter 1 Type", "LP Diode Ladder" }, { "A Waveshaper Type", "Single Fold" } },
                 { ouverture (200.0, 6000.0, 0.5f), resonance (70.0, 0.4f), grain (24.0, 0.5f), enveloppe (60.0, 0.5f),
                   decroissance (60.0, 800.0, 0.4f), epaisseur (0.4f), glisse (0.1f), volume (0.7f) });
        ajouter ("Sub liquide", "Drum & bass", { { "A Osc 1 Type", "Sine" }, { "A Play Mode", "Mono" }, { "A Osc 1 Octave", "-1" },
                                                 { "A Filter 1 Type", "LP 12 dB" }, { "A Waveshaper Type", "Soft" } },
                 { ouverture (100.0, 2000.0, 0.5f), grain (14.0, 0.1f), sub (0.0f), relache (30.0, 400.0, 0.2f),
                   attaque (60.0), decroissance (200.0, 2000.0, 0.9f), glisse (0.3f), volume() });

        // Acid : passe-bas résonant, enveloppe nerveuse, glissés
        ajouter ("Acid 303", "Acid", { { "A Osc 1 Shape", {}, -100.0 }, { "A Play Mode", "Mono (Fingered Portamento)" },
                                       { "A Filter 1 Type", "LP Diode Ladder" }, { "A Filter EG Sustain", {}, 0.0 }, { "A Waveshaper Type", "Soft" } },
                 { ouverture (120.0, 3000.0, 0.3f), resonance (90.0, 0.6f), enveloppe (60.0, 0.5f), decroissance (40.0, 600.0, 0.3f),
                   grain (18.0, 0.2f), glisse (0.3f), forme (0.0f), volume (0.75f) });
        ajouter ("Acid carré"_fr, "Acid", { { "A Osc 1 Shape", {}, 100.0 }, { "A Play Mode", "Mono (Fingered Portamento)" },
                                         { "A Filter 1 Type", "LP 24 dB" }, { "A Waveshaper Type", "Hard" } },
                 { ouverture (150.0, 3500.0, 0.3f), resonance (85.0, 0.5f), enveloppe (54.0, 0.5f), decroissance (40.0, 500.0, 0.3f),
                   grain (16.0, 0.3f), glisse (0.3f), forme (1.0f), volume (0.7f) });
        ajouter ("Acid hurlant", "Acid", { { "A Osc 1 Shape", {}, -100.0 }, { "A Play Mode", "Mono (Fingered Portamento)" },
                                           { "A Filter 1 Type", "LP Vintage Ladder" }, { "A Waveshaper Type", "Heavy Fuzz" } },
                 { ouverture (200.0, 5000.0, 0.4f), resonance (95.0, 0.7f), enveloppe (72.0, 0.6f), decroissance (50.0, 800.0, 0.4f),
                   grain (24.0, 0.5f), glisse (0.4f), forme (0.0f), volume (0.6f) });

        // Hard techno : rumble et basses saturées
        ajouter ("Rumble", "Hard techno", { { "A Osc 1 Type", "Sine" }, { "A Play Mode", "Mono" }, { "A Osc 1 Octave", "-2" },
                                            { "A Filter 1 Type", "LP 24 dB" }, { "A Waveshaper Type", "Fuzz Soft Clip" }, { "A Amp EG Sustain", {}, 40.0 } },
                 { ouverture (60.0, 600.0, 0.4f), grain (24.0, 0.6f), decroissance (100.0, 1500.0, 0.5f), enveloppe (24.0, 0.2f),
                   relache (30.0, 800.0, 0.3f), sub (0.0f), resonance (40.0, 0.1f), volume (0.7f) });
        ajouter ("Basse industrielle", "Hard techno", { { "A Osc 1 Shape", {}, 100.0 }, { "A Play Mode", "Mono" }, { "A Osc 1 Octave", "-1" },
                                                        { "A Filter 1 Type", "LP K35" }, { "A Waveshaper Type", "Hard" } },
                 { ouverture (150.0, 3000.0, 0.4f), resonance (70.0, 0.3f), grain (24.0, 0.6f), enveloppe (48.0, 0.4f),
                   decroissance (50.0, 600.0, 0.3f), sub (0.5f), forme (0.8f), volume (0.65f) });
        ajouter ("Stab hard", "Hard techno", { { "A Osc 1 Unison Voices", "5 voices" }, { "A Filter 1 Type", "LP OB-Xd 24 dB" },
                                               { "A Amp EG Sustain", {}, 0.0 }, { "A Waveshaper Type", "Asymmetric" } },
                 { ouverture (300.0, 6000.0, 0.5f), resonance (60.0, 0.2f), decroissance (60.0, 600.0, 0.3f), grain (20.0, 0.4f),
                   epaisseur (0.6f), enveloppe (36.0, 0.4f), relache (20.0, 400.0, 0.2f), volume (0.65f) });

        // Minimal : sec, court, précis
        ajouter ("Stab minimal", "Minimal", { { "A Osc 1 Shape", {}, 0.0 }, { "A Filter 1 Type", "BP 12 dB" }, { "A Amp EG Sustain", {}, 0.0 } },
                 { ouverture (300.0, 4000.0, 0.5f), resonance (50.0, 0.2f), decroissance (30.0, 300.0, 0.2f), forme(),
                   epaisseur (0.1f), enveloppe (24.0, 0.3f), relache (10.0, 200.0, 0.1f), volume() });
        ajouter ("Bip minimal", "Minimal", { { "A Osc 1 Type", "Sine" }, { "A Amp EG Sustain", {}, 0.0 }, { "A Amp EG Decay", {}, 80.0 } },
                 { decroissance (20.0, 300.0, 0.2f), grain (14.0, 0.0f), relache (10.0, 200.0, 0.1f), attaque (30.0),
                   ouverture (500.0, 8000.0, 0.8f), sub (0.0f), glisse(), volume() });
        ajouter ("Basse minimale", "Minimal", { { "A Osc 1 Shape", {}, 100.0 }, { "A Play Mode", "Mono" }, { "A Osc 1 Octave", "-1" },
                                                { "A Filter 1 Type", "LP 12 dB" }, { "A Amp EG Sustain", {}, 20.0 } },
                 { ouverture (100.0, 1500.0, 0.3f), resonance (40.0, 0.1f), decroissance (40.0, 500.0, 0.3f), enveloppe (30.0, 0.3f),
                   sub (0.5f), forme (1.0f), glisse(), volume() });

        // Electro : leads et arpèges
        ajouter ("Lead electro", "Electro", { { "A Osc 1 Shape", {}, 100.0 }, { "A Osc 2 Mute", "Off" }, { "A Filter 1 Type", "LP OB-Xd 12 dB" },
                                              { "A Play Mode", "Mono" }, { "A Amp EG Sustain", {}, 70.0 } },
                 { ouverture (500.0, 8000.0, 0.5f), resonance (60.0, 0.2f), forme (0.8f), epaisseur (0.3f),
                   glisse (0.1f), enveloppe (24.0, 0.3f), relache (30.0, 800.0, 0.3f), volume (0.7f) });
        ajouter ("Arpège electro"_fr, "Electro", { { "A Osc 1 Shape", {}, -100.0 }, { "A Filter 1 Type", "LP 24 dB" }, { "A Amp EG Sustain", {}, 0.0 },
                                                { "A Amp EG Decay", {}, 200.0 } },
                 { ouverture (300.0, 6000.0, 0.4f), resonance (60.0, 0.3f), decroissance (30.0, 500.0, 0.3f), enveloppe (36.0, 0.4f),
                   forme (0.0f), epaisseur (0.2f), relache (10.0, 400.0, 0.1f), volume (0.7f) });
        ajouter ("Brass electro", "Electro", { { "A Osc 1 Shape", {}, -100.0 }, { "A Osc 1 Unison Voices", "3 voices" }, { "A Filter 1 Type", "LP 24 dB" },
                                               { "A Filter EG Attack", {}, 60.0 }, { "A Amp EG Sustain", {}, 80.0 } },
                 { ouverture (300.0, 5000.0, 0.3f), enveloppe (48.0, 0.5f), attaque (300.0), epaisseur (0.4f),
                   resonance (40.0, 0.1f), relache (50.0, 1000.0, 0.3f), grain (12.0, 0.0f), volume (0.7f) });

        // Nappes : lentes, larges
        ajouter ("Nappe chaude", "Nappes", { { "A Osc 1 Shape", {}, -100.0 }, { "A Osc 1 Unison Voices", "4 voices" }, { "A Filter 1 Type", "LP 12 dB" },
                                             { "A Amp EG Attack", {}, 600.0 }, { "A Amp EG Release", {}, 1500.0 } },
                 { ouverture (300.0, 4000.0, 0.35f), attaque (3000.0, 0.3f), relache (200.0, 4000.0, 0.4f), epaisseur (0.5f),
                   resonance (40.0, 0.1f), forme (0.0f), grain (10.0, 0.0f), volume (0.7f) });
        ajouter ("Nappe de verre", "Nappes", { { "A Osc 1 Type", "Sine" }, { "A Osc 2 Mute", "Off" }, { "A Osc 2 Octave", "1" },
                                               { "A Amp EG Attack", {}, 400.0 }, { "A Amp EG Release", {}, 2000.0 }, { "A Filter 1 Type", "LP 12 dB" } },
                 { ouverture (800.0, 10000.0, 0.6f), attaque (3000.0, 0.2f), relache (300.0, 5000.0, 0.5f), epaisseur (0.3f),
                   grain (10.0, 0.0f), sub (0.0f), resonance (30.0, 0.0f), volume (0.7f) });
        ajouter ("Nappe sombre", "Nappes", { { "A Osc 1 Shape", {}, 100.0 }, { "A Osc 1 Unison Voices", "3 voices" }, { "A Filter 1 Type", "LP 24 dB" },
                                             { "A Amp EG Attack", {}, 900.0 }, { "A Amp EG Release", {}, 2500.0 } },
                 { ouverture (150.0, 2000.0, 0.3f), attaque (4000.0, 0.3f), relache (300.0, 5000.0, 0.5f), epaisseur (0.4f),
                   resonance (50.0, 0.2f), forme (0.5f), grain (12.0, 0.0f), volume (0.7f) });

        // Textures : souffles et bruits colorés
        ajouter ("Souffle", "Textures", { { "A Osc 1 Mute", "On" }, { "A Noise Mute", "Off" }, { "A Filter 1 Type", "BP 12 dB" },
                                          { "A Amp EG Attack", {}, 1200.0 }, { "A Amp EG Release", {}, 3000.0 } },
                 { ouverture (300.0, 8000.0, 0.4f), resonance (80.0, 0.4f), attaque (5000.0, 0.3f), relache (300.0, 6000.0, 0.5f),
                   { "Couleur", 0.5f, { { "A Noise Color", -100.0, 100.0 } } }, grain (10.0, 0.0f), sub (0.0f), volume (0.6f) });
        ajouter ("Vent métallique"_fr, "Textures", { { "A Osc 1 Type", "FM2" }, { "A Amp EG Attack", {}, 1500.0 }, { "A Amp EG Release", {}, 3000.0 },
                                                     { "A Filter 1 Type", "LP 12 dB" } },
                 { ouverture (400.0, 10000.0, 0.5f), attaque (5000.0, 0.3f), relache (300.0, 6000.0, 0.5f), grain (14.0, 0.1f),
                   resonance (50.0, 0.2f), epaisseur (0.2f), forme (0.5f), volume (0.6f) });
        ajouter ("Grondement", "Textures", { { "A Osc 1 Type", "S&H Noise" }, { "A Osc 1 Octave", "-2" }, { "A Filter 1 Type", "LP 24 dB" },
                                             { "A Amp EG Attack", {}, 800.0 }, { "A Amp EG Release", {}, 2500.0 } },
                 { ouverture (60.0, 1500.0, 0.3f), resonance (60.0, 0.3f), attaque (4000.0, 0.2f), relache (300.0, 5000.0, 0.4f),
                   grain (20.0, 0.3f), forme (0.5f), sub (0.0f), volume (0.6f) });

        return s;
    }
}

namespace sonsDeSynthe
{
    const std::vector<SonDeSynthe>& tous()
    {
        static const auto liste = fabriquer();
        return liste;
    }

    const SonDeSynthe* trouver (const juce::String& nom)
    {
        for (auto& s : tous())
            if (s.nom == nom)
                return &s;

        return nullptr;
    }

    juce::StringArray styles()
    {
        juce::StringArray liste;

        for (auto& s : tous())
            liste.addIfNotAlreadyThere (s.style);

        return liste;
    }

    juce::StringArray sonsDuStyle (const juce::String& style)
    {
        juce::StringArray liste;

        for (auto& s : tous())
            if (s.style == style)
                liste.add (s.nom);

        return liste;
    }

    juce::String sonParDefaut (int son)
    {
        switch ((SonProvisoire) son)
        {
            case SonProvisoire::basse:      return "Sub dub";
            case SonProvisoire::nappe:      return "Nappe chaude";
            case SonProvisoire::lead:       return "Lead electro";
            case SonProvisoire::arpege:     return "Arpège electro"_fr;
            case SonProvisoire::stab:       return "Stab minimal";
            case SonProvisoire::texture:    return "Souffle";
            default:                        return "Nappe chaude";
        }
    }
}

//==============================================================================
PiloteDeSurge::PiloteDeSurge (te::Plugin& p)  : plugin (p)
{
    parTracktion = surge::parametres (plugin);

    if (auto externe = dynamic_cast<te::ExternalPlugin*> (&plugin))
        if (auto instance = externe->getAudioPluginInstance())
            for (auto* juceParam : instance->getParameters())
                for (auto& [nom, _] : parTracktion)
                    if (juceParam->getName (100) == nom || juceParam->getName (100).endsWith (" " + nom))
                        parJuce[nom] = juceParam;
}

namespace
{
    /** Le nombre affiché par Surge, ramené à l'unité des sons de départ (les secondes en millisecondes). */
    double lireNombre (const juce::String& texte)
    {
        const auto t = texte.trim();

        if (t.startsWithIgnoreCase ("-inf"))
            return -1.0e9;

        const auto n = t.getDoubleValue();
        return t.endsWith (" s") ? n * 1000.0 : n;
    }

    /** Textes de chaque réglage sur 2001 positions, mis en cache une fois pour toutes (les mêmes pour toutes les instances). */
    const std::vector<juce::String>& echelle (juce::AudioProcessorParameter& p, const juce::String& nom)
    {
        static std::map<juce::String, std::vector<juce::String>> cache;

        if (auto trouve = cache.find (nom); trouve != cache.end())
            return trouve->second;

        std::vector<juce::String> textes;

        for (int k = 0; k <= 2000; ++k)
            textes.push_back (p.getText ((float) k / 2000.0f, 100));

        return cache[nom] = textes;
    }
}

std::optional<float> PiloteDeSurge::valeurPourTexte (const juce::String& parametre, const juce::String& texte)
{
    auto trouve = parJuce.find (parametre);

    if (trouve == parJuce.end())
        return std::nullopt;

    const auto& textes = echelle (*trouve->second, parametre);

    // Le milieu de la plage qui donne ce texte : la valeur la plus sûre
    int debut = -1, fin = -1;

    for (int k = 0; k < (int) textes.size(); ++k)
        if (textes[(size_t) k] == texte)
        {
            if (debut < 0)
                debut = k;

            fin = k;
        }
        else if (debut >= 0)
        {
            break;
        }

    if (debut < 0)
        return std::nullopt;

    return (float) (debut + fin) / 2.0f / 2000.0f;
}

std::optional<float> PiloteDeSurge::valeurPourNombre (const juce::String& parametre, double nombre)
{
    auto trouve = parJuce.find (parametre);

    if (trouve == parJuce.end())
        return std::nullopt;

    const auto& textes = echelle (*trouve->second, parametre);
    int meilleur = 0;
    double ecart = 1.0e18;

    for (int k = 0; k < (int) textes.size(); ++k)
        if (const auto e = std::abs (lireNombre (textes[(size_t) k]) - nombre); e < ecart)
        {
            ecart = e;
            meilleur = k;
        }

    return (float) meilleur / 2000.0f;
}

juce::String PiloteDeSurge::texteActuel (const juce::String& parametre)
{
    if (auto p = parTracktion.find (parametre); p != parTracktion.end())
        return p->second->getCurrentValueAsString();

    return {};
}

void PiloteDeSurge::regler (const juce::String& parametre, float valeur)
{
    if (auto p = parTracktion.find (parametre); p != parTracktion.end())
        p->second->setParameter (juce::jlimit (0.0f, 1.0f, valeur), juce::sendNotificationSync);
}

bool PiloteDeSurge::appliquerSon (const SonDeSynthe& son, const std::array<float, 8>& macros)
{
    bool toutTrouve = true;

    for (auto& r : son.reglages)
    {
        const auto v = r.texte.isNotEmpty() ? valeurPourTexte (r.parametre, r.texte) : valeurPourNombre (r.parametre, r.nombre);

        if (v)
            regler (r.parametre, *v);
        else
        {
            toutTrouve = false;
            juce::Logger::writeToLog ("Réglage de Surge introuvable : "_fr + r.parametre + " = " + (r.texte.isNotEmpty() ? r.texte : juce::String (r.nombre)));
        }
    }

    appliquerMacros (son, macros);
    return toutTrouve;
}

void PiloteDeSurge::appliquerMacros (const SonDeSynthe& son, const std::array<float, 8>& macros)
{
    for (int i = 0; i < 8; ++i)
        for (auto& l : son.macros[(size_t) i].liaisons)
        {
            const auto de = valeurPourNombre (l.parametre, l.de), a = valeurPourNombre (l.parametre, l.a);

            // Interpolation dans l'échelle de Surge : la fréquence de coupure varie alors comme on l'entend (en demi-tons)
            if (de && a)
                regler (l.parametre, *de + (*a - *de) * juce::jlimit (0.0f, 1.0f, macros[(size_t) i]));
        }
}

} // namespace groovebox
