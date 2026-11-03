#pragma once

#include "Texte.h"

namespace groovebox
{
namespace te = tracktion;

/** Base des effets maison : stéréo, réglages lissés, passage sans clic entre actif et sauté. */
class EffetMaison  : public te::Plugin
{
public:
    explicit EffetMaison (te::PluginCreationInfo info)  : te::Plugin (info) {}

    juce::String getShortName (int) override                    { return getName(); }
    juce::String getSelectableDescription() override            { return getName(); }
    int getNumOutputChannelsGivenInputs (int) override          { return 2; }
    void deinitialise() override {}
    void initialise (const te::PluginInitialisationInfo&) override;
    void applyToBuffer (const te::PluginRenderContext&) override;

    std::atomic<bool> actif { true };

protected:
    double frequence = 48000.0;
    float melangeDuPassage = 1.0f;      ///< 1 : effet ; 0 : signal d'origine (glisse en 10 ms)

    virtual void preparer() {}
    virtual void traiter (float& gauche, float& droite) = 0;

    /** Lissage d'un réglage vers sa cible (environ 20 ms). */
    float lisser (float& courant, float cible) const
    {
        courant += (cible - courant) * (float) (1.0 - std::exp (-1.0 / (0.02 * frequence)));
        return courant;
    }
};

/** Filtre à un bouton : passe-bas sous 0,5, neutre à 0,5, passe-haut au-dessus ; résonance. */
class FiltrePlugin  : public EffetMaison
{
public:
    static const char* xmlTypeName;
    explicit FiltrePlugin (te::PluginCreationInfo info)  : EffetMaison (info) {}
    ~FiltrePlugin() override                                    { notifyListenersOfDeletion(); }
    juce::String getName() const override                       { return "Filtre"; }
    juce::String getPluginType() override                       { return xmlTypeName; }

    std::atomic<float> position { 0.5f }, resonance { 0.2f };

    /** Fréquences de coupure (Hz) du passe-bas et du passe-haut pour une position (pour les essais). */
    static std::pair<double, double> coupures (float position);

private:
    float positionLissee = 0.5f, resonanceLissee = 0.2f;
    std::array<float, 2> bas1 {}, bas2 {}, haut1 {}, haut2 {};
    void traiter (float& g, float& d) override;
};

/** Saturation : quantité, couleur (sombre ou brillante), mélange. */
class SaturationPlugin  : public EffetMaison
{
public:
    static const char* xmlTypeName;
    explicit SaturationPlugin (te::PluginCreationInfo info)  : EffetMaison (info) {}
    ~SaturationPlugin() override                                { notifyListenersOfDeletion(); }
    juce::String getName() const override                       { return "Saturation"; }
    juce::String getPluginType() override                       { return xmlTypeName; }

    std::atomic<float> quantite { 0.3f }, couleur { 0.5f }, melange { 1.0f };

private:
    float quantiteLissee = 0.3f, couleurLissee = 0.5f, melangeLisse = 1.0f;
    std::array<float, 2> memoire {};
    void traiter (float& g, float& d) override;
};

/** Distorsion lourde (hardcore, gabber, industriel) : gain jusqu'à +40 dB, dureté (de la saturation douce
    à l'écrêtage dur puis au repliement), tonalité, mélange. Le niveau de sortie reste à peu près constant. */
class DistorsionPlugin  : public EffetMaison
{
public:
    static const char* xmlTypeName;
    explicit DistorsionPlugin (te::PluginCreationInfo info)  : EffetMaison (info) {}
    ~DistorsionPlugin() override                                { notifyListenersOfDeletion(); }
    juce::String getName() const override                       { return "Distorsion"; }
    juce::String getPluginType() override                       { return xmlTypeName; }

    std::atomic<float> gain { 0.5f }, durete { 0.4f }, tonalite { 0.6f }, melange { 1.0f };

    /** La forme de la distorsion, pour une dureté donnée (sert aussi aux essais). */
    static float formeDeLaDistorsion (float x, float durete);

private:
    float gainLisse = 0.5f, dureteLissee = 0.4f, tonaliteLissee = 0.6f, melangeLisse = 1.0f;
    std::array<float, 2> passeBas {}, bloqueur {}, precedent {};
    void traiter (float& g, float& d) override;
};

/** Punch : renforce ou adoucit l'attaque des coups, allonge ou raccourcit leur maintien,
    puis « Puissance » pousse le tout dans un écrêtage doux pour un son plus dense et plus fort. */
class PunchPlugin  : public EffetMaison
{
public:
    static const char* xmlTypeName;
    explicit PunchPlugin (te::PluginCreationInfo info)  : EffetMaison (info) {}
    ~PunchPlugin() override                                     { notifyListenersOfDeletion(); }
    juce::String getName() const override                       { return "Punch"; }
    juce::String getPluginType() override                       { return xmlTypeName; }

    std::atomic<float> attaque { 0.7f }, maintien { 0.5f }, puissance { 0.3f };

private:
    float rapide = 0.0f, lente = 0.0f;
    void traiter (float& g, float& d) override;
};

/** Grave : isole les graves sous une fréquence choisie, les renforce et les sature.
    Les harmoniques ajoutées font entendre le poids du son même sur de petites enceintes. */
class GravePlugin  : public EffetMaison
{
public:
    static const char* xmlTypeName;
    explicit GravePlugin (te::PluginCreationInfo info)  : EffetMaison (info) {}
    ~GravePlugin() override                                     { notifyListenersOfDeletion(); }
    juce::String getName() const override                       { return "Grave"; }
    juce::String getPluginType() override                       { return xmlTypeName; }

    std::atomic<float> quantite { 0.5f }, frequenceDuGrave { 0.4f }, saturation { 0.4f };

private:
    std::array<float, 2> bas1 {}, bas2 {};
    float quantiteLissee = 0.5f;
    void traiter (float& g, float& d) override;
};

/** Delay dub : temps calé sur le tempo, retour jusqu'à 120 %, filtre et saturation dans la boucle.
    La saturation de la boucle borne toujours l'écho : il s'emballe sans jamais exploser. Sortie : l'écho seul (effet de départ). */
class DelayDubPlugin  : public EffetMaison
{
public:
    static const char* xmlTypeName;
    explicit DelayDubPlugin (te::PluginCreationInfo info)  : EffetMaison (info) {}
    ~DelayDubPlugin() override                                  { notifyListenersOfDeletion(); }
    juce::String getName() const override                       { return "Delay dub"; }
    juce::String getPluginType() override                       { return xmlTypeName; }

    std::atomic<float> tempo { 120.0f }, noires { 0.75f }, retour { 0.45f }, filtre { 0.35f }, saturation { 0.2f };

    /** Temps de l'écho en secondes pour un tempo et une division (en noires). */
    static double tempsDeLEcho (double tempo, double noires)    { return 60.0 / tempo * noires; }

private:
    std::vector<float> memoireG, memoireD;
    int ecriture = 0;
    float tempsLisse = 0.375f, retourLisse = 0.45f;
    std::array<float, 2> bas {}, haut {};
    void preparer() override;
    void traiter (float& g, float& d) override;
};

/** Sortie : compresseur léger (quantité) puis limiteur à anticipation, plafond à -1 dB ; une dernière butée garantit le plafond. */
class LimiteurPlugin  : public EffetMaison
{
public:
    static const char* xmlTypeName;
    static constexpr float plafond = 0.891f;    // -1 dB
    explicit LimiteurPlugin (te::PluginCreationInfo info)  : EffetMaison (info) {}
    ~LimiteurPlugin() override                                  { notifyListenersOfDeletion(); }
    juce::String getName() const override                       { return "Limiteur"; }
    juce::String getPluginType() override                       { return xmlTypeName; }

    std::atomic<float> compression { 0.3f };

private:
    std::vector<float> retardG, retardD, gains;
    int position = 0, anticipation = 64;
    float enveloppe = 0.0f, gainDuLimiteur = 1.0f;
    void preparer() override;
    void traiter (float& g, float& d) override;
};

} // namespace groovebox
