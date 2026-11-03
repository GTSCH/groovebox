#pragma once

#include "modele/Enchainements.h"

namespace groovebox
{
namespace te = tracktion;

/** Un trajet d'enchaînement pour une voix : entre deux instants de la ligne du temps (en secondes),
    l'avancée va de pDebut à pFin (de 0 à 1 à l'aller, vers 0 pour une annulation). */
struct TrajetDeVoix
{
    double debut = 0.0, fin = 0.0;
    float pDebut = 0.0f, pFin = 1.0f;
    TypeEnchainement type = TypeEnchainement::coupe;
    bool entrante = true;
    float intensite = 0.7f;

    float avancee (double instant) const;
    FormeDeVoix forme (double instant) const    { return formeDeLaVoix (type, entrante, avancee (instant), intensite); }
};

/** Dernier maillon d'une voix (A ou B) d'une piste, avant les effets de la piste : son volume et ses filtres d'enchaînement.
    Au repos, il laisse passer (voix qui joue) ou se tait (voix libre) sans rien calculer.
    Pendant un enchaînement, il suit son trajet à l'échantillon près, d'après la position de la lecture. */
class VoixPlugin  : public te::Plugin
{
public:
    static const char* xmlTypeName;
    explicit VoixPlugin (te::PluginCreationInfo info)  : te::Plugin (info) {}
    ~VoixPlugin() override                                      { notifyListenersOfDeletion(); }

    juce::String getName() const override                       { return "Voix"; }
    juce::String getPluginType() override                       { return xmlTypeName; }
    juce::String getShortName (int) override                    { return getName(); }
    juce::String getSelectableDescription() override            { return getName(); }
    int getNumOutputChannelsGivenInputs (int) override          { return 2; }
    void initialise (const te::PluginInitialisationInfo&) override;
    void deinitialise() override {}
    void applyToBuffer (const te::PluginRenderContext&) override;

    /** Fil principal. Le trajet précédent reste valable jusqu'au début du nouveau : rien ne saute. */
    void programmer (const TrajetDeVoix&);

    /** Fil principal : plus de trajet, la voix passe (1) ou se tait (0). */
    void reposer (float gain);

    /** Fréquences des filtres (Hz) pour une position, pour les essais. */
    static double coupureDuPasseBas (float position);
    static double coupureDuPasseHaut (float position);
    static constexpr double coupureDesGraves = 180.0;

private:
    struct Programme
    {
        std::optional<TrajetDeVoix> actuel, avant;
        float repos = 1.0f;
    };

    juce::SpinLock verrou;
    Programme programme, programmeAudio;
    double frequence = 48000.0;
    float gainLisse = 1.0f, gravesLisses = 0.0f, melangeDesFiltres = 0.0f;
    float basLisse = 1.0f, hautLisse = 0.0f;
    std::array<float, 2> bas1 {}, bas2 {}, haut1 {}, haut2 {}, gravesA1 {}, gravesA2 {}, gravesB1 {}, gravesB2 {};

    FormeDeVoix formeA (const Programme&, double instant) const;
    void viderLesFiltres();
};

} // namespace groovebox
