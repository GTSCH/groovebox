#pragma once

#include "BipPlugin.h"
#include "ClicPlugin.h"
#include "modele/Morceau.h"

namespace groovebox
{

enum class Sortie
{
    principale, ///< Sorties 1-2 : la sono, jamais de clic
    casque      ///< Sorties 3-4 : le casque du batteur (musique + clic)
};

/** Où en est la lecture. Pendant le décompte, la mesure vaut 0 ou -1. */
struct PositionMusicale
{
    bool enLecture = false;
    int mesure = 1;            ///< Commence à 1
    int temps = 0;             ///< Commence à 0
    double fraction = 0.0;     ///< Avancée dans le temps en cours, de 0 à 1
};

/** Le moteur audio : horloge, mesure, clic et sons de test.
    Le clic ne va que sur les sorties 3-4 ; sans elles, il se tait, sauf si l'écoute sur le Mac est demandée. */
class Moteur  : private juce::ChangeListener
{
public:
    /** Mesures d'avance au début de la ligne du temps, où se joue le décompte. La mesure 1 commence après. */
    static constexpr int mesuresDePreparation = 2;

    Moteur (te::Engine&, Morceau morceauDeDepart = {});
    ~Moteur() override;

    //==============================================================================
    const Morceau& getMorceau() const           { return morceau; }

    /** Modifie le morceau et l'applique au son. La mesure ne change pas pendant la lecture. */
    void modifier (const std::function<void (Morceau&)>&);

    /** Appelé (fil principal) après chaque modification du morceau. */
    std::function<void()> surChangementDeMorceau;

    //==============================================================================
    /** Lance la lecture à la mesure 1, après le décompte. */
    void lancer();
    void arreter();
    bool enLecture() const;
    PositionMusicale getPosition() const;

    //==============================================================================
    void activerBip (Sortie, bool actif);
    bool bipActif (Sortie) const;
    void reglerNiveauBip (Sortie, float niveau);

    /** Vrai si la carte son choisie a des sorties 3-4. */
    bool sortieCasqueDisponible() const;

    /** Vrai si le clic part sur les sorties 1-2, pour un essai sans carte son. */
    bool ecouteSurMacEnCours() const;

    te::Edit& getEdit()                         { return *edit; }

    /** Appelé (fil principal) quand la carte son ou ses réglages changent. */
    std::function<void()> surChangementDeCarte;

private:
    te::Engine& engine;
    Morceau morceau;
    std::unique_ptr<te::Edit> edit;
    te::AudioTrack* pistePrincipale = nullptr;
    te::AudioTrack* pisteCasque = nullptr;
    BipPlugin* bipPrincipal = nullptr;
    BipPlugin* bipCasque = nullptr;
    ClicPlugin* clic = nullptr;
    te::AuxSendPlugin* envoiCasque = nullptr;
    te::AuxReturnPlugin* retourCasque = nullptr;
    te::MidiClip::Ptr clipDuClic;
    bool lectureVoulue = false;

    BipPlugin* bip (Sortie) const;
    te::WaveOutputDevice* trouverSortie (int premierCanal) const;
    void appliquerRoutage();
    void appliquerTempo();
    void appliquerMesure();
    void appliquerMotifDuClic();
    void appliquerReglagesDuClic();
    void ramenerTempoAUnSeul();
    te::TimePosition positionExacte() const;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Moteur)
};

} // namespace groovebox
