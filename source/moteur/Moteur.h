#pragma once

#include "BipPlugin.h"
#include "ClicPlugin.h"
#include "InstrumentProvisoire.h"
#include "BatteriePlugin.h"
#include "SonsDeSynthe.h"
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

/** État d'une case de la Session, pour l'affichage. */
struct EtatCase
{
    bool clip = false;
    bool joue = false;
    bool departEnAttente = false;   ///< Démarre au prochain calage
    bool arretEnAttente = false;    ///< S'arrête au prochain calage
};

/** Le moteur audio : horloge, mesure, clic, 8 pistes (chacune avec ses voix A et B), clips, scènes et sons de test.
    Le clic ne va que sur les sorties 3-4 ; sans elles, il se tait, sauf si l'écoute sur le Mac est demandée. */
class Moteur  : private juce::ChangeListener,
                private juce::Timer
{
public:
    /** Mesures d'avance au début de la ligne du temps, où se joue le décompte. La mesure 1 commence après. */
    static constexpr int mesuresDePreparation = 2;

    /** Sans banque de sons fournie, le moteur prend le dossier Sons habituel et y fabrique les kits de départ manquants. */
    Moteur (te::Engine&, Morceau morceauDeDepart = {}, BanqueDeSons* banque = nullptr);
    ~Moteur() override;

    //==============================================================================
    const Morceau& getMorceau() const           { return morceau; }

    /** Modifie le morceau et l'applique au son. La mesure ne change pas pendant la lecture.
        Les modifications portant le même « geste » (un encodeur qu'on tourne) à moins d'une seconde d'écart
        ne font qu'une seule étape d'annulation. */
    void modifier (const std::function<void (Morceau&)>&, const juce::String& geste = {});

    /** Remplace tout le morceau (ouverture d'un autre morceau) : arrêt, puis reconstruction. Vide l'historique. */
    void chargerMorceau (const Morceau&);

    bool annuler();
    bool retablir();
    bool peutAnnuler() const                    { return ! historique.empty(); }
    bool peutRetablir() const                   { return ! futur.empty(); }

    //==============================================================================
    /** Lance un clip au prochain calage (la piste quitte son clip en cours au même instant).
        À l'arrêt, la lecture démarre, avec le décompte, et le clip part à la mesure 1. */
    void lancerClip (int piste, int scene);
    void lancerScene (int scene);
    void arreterPiste (int piste);
    void arreterTousLesClips();

    EtatCase etatCase (int piste, int scene) const;

    /** Scène du clip qui joue sur la piste, ou -1. */
    int sceneJouee (int piste) const;

    /** Traite ce qui attend le démarrage de la lecture. Appelé tout seul ; les essais l'appellent à la main. */
    void avancer();

    /** Temps du moteur (depuis le début de la ligne du temps) où démarre la mesure 1. */
    double tempsDeLaMesure1() const;

    /** Joue une note en direct sur la piste (pads du Push en mode Note), sans passer par un clip. */
    void jouerNote (int piste, int note, int velocite);
    void relacherNote (int piste, int note);

    /** Position, en temps du moteur, dans le clip qui joue sur la piste (pour la lumière du pas à pas). */
    std::optional<double> positionDansLeClip (int piste) const;

    BanqueDeSons& getBanque()                   { return *banque; }

    /** Vrai si la piste joue avec Surge XT (sinon : son provisoire ou batterie). */
    bool joueAvecSurge (int piste) const;

    /** Pilote du Surge de la voix A d'une piste, pour les essais. */
    PiloteDeSurge* piloteSurge (int piste) const        { return pistesMoteur[(size_t) piste].pilotesSurge[0].get(); }

    /** Débuts des notes telles que le moteur les joue (swing compris), pour les essais. */
    std::vector<double> debutsJoues (int piste, int scene) const;

    /** Instrument d'une voix (0 : A, 1 : B), pour les essais. */
    te::AudioTrack* voix (int piste, int laquelle) const   { return pistesMoteur[(size_t) piste].voix[(size_t) laquelle]; }

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
    std::unique_ptr<BanqueDeSons> banquePropre;
    BanqueDeSons* banque = nullptr;
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

    struct PisteMoteur
    {
        te::FolderTrack* dossier = nullptr;
        std::array<te::AudioTrack*, 2> voix {};
        std::array<InstrumentProvisoire*, 2> instruments {};
        std::array<BatteriePlugin*, 2> batteries {};
        std::array<te::Plugin::Ptr, 2> surges;                      ///< Surge XT, s'il est installé (pistes de synthé)
        std::array<std::unique_ptr<PiloteDeSurge>, 2> pilotesSurge;
        te::AuxSendPlugin* envoiCasque = nullptr;
        te::VolumeAndPanPlugin* volume = nullptr;
    };

    std::array<PisteMoteur, nombreDePistes> pistesMoteur;

    // Démarrage : la lecture part quand la position demandée est prise en compte, avec les clips déjà calés
    bool demarrageEnAttente = false;
    double instantDuDemarrage = 0.0, tempsDuDepart = 0.0;
    std::vector<std::pair<int, int>> lancementsAuDemarrage;

    // Annulation
    std::vector<Morceau> historique, futur;
    juce::String dernierGeste;
    double instantDuDernierGeste = 0.0;

    void appliquerDifferences (const Morceau& ancien);
    void appliquerPistes (const Morceau& ancien, bool tout);
    void appliquerCases (const Morceau& ancien, bool tout);
    void appliquerCase (int piste, int scene);
    void assurerCases();
    te::MidiClip* clipDeLaCase (int piste, int scene) const;
    double longueurDuCalage() const;
    std::optional<te::MonotonicBeat> prochainCalage (std::optional<double> tempsVise = std::nullopt) const;
    void lancerMaintenant (int piste, int scene, te::MonotonicBeat);
    void arreterMaintenant (int piste, std::optional<te::MonotonicBeat>);
    void timerCallback() override       { avancer(); }

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
