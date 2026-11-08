#pragma once

#include "BipPlugin.h"
#include "ClicPlugin.h"
#include "InstrumentProvisoire.h"
#include "BatteriePlugin.h"
#include "SamplerPlugin.h"
#include "SonsDeSynthe.h"
#include "EffetsMaison.h"
#include "VoixPlugin.h"
#include "EnregistreurAudio.h"
#include "modele/Deroule.h"
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
    bool arretEnAttente = false;    ///< S'arrête au prochain calage (ou s'efface dans un enchaînement)
};

/** Un enchaînement en cours sur une piste, pour l'affichage. */
struct EtatEnchainement
{
    int sceneDe = -1, sceneVers = -1;
    TypeEnchainement type = TypeEnchainement::coupe;
    float avancee = 0.0f;           ///< 0 au départ, 1 à la fin
    bool enAttente = false;         ///< Pas encore commencé (attend le calage)
    bool annulation = false;        ///< Revient vers l'ancienne scène
    bool aLaMain = false;           ///< Mené à la bande tactile
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
    /** Lance un clip au prochain calage, avec l'enchaînement donné (sinon celui du morceau).
        En coupe, la piste quitte son clip en cours au même instant ; sinon, l'ancien et le nouveau jouent ensemble
        le temps de l'enchaînement. Relancer le clip qu'on quitte annule l'enchaînement en cours, à l'envers.
        À l'arrêt, la lecture démarre, avec le décompte, et le clip part à la mesure 1. */
    void lancerClip (int piste, int scene, std::optional<Enchainement> = std::nullopt);

    /** Lance une scène avec l'enchaînement donné, sinon celui retenu pour la scène, sinon celui du morceau.
        Une piste dont le clip ne change pas continue sans rien changer. */
    void lancerScene (int scene, std::optional<Enchainement> = std::nullopt);

    /** Enchaînement à la main : la scène part en silence au calage, sur les voix libres ; melangerALaMain la fait
        monter (0 : la scène en cours, 1 : la nouvelle). Arrivé à 1, la nouvelle prend la main ; revenu à 0, rien n'a changé.
        Relancer la scène préparée termine le mélange tout seul ; lancer une autre scène l'annule d'abord. */
    void preparerScene (int scene, std::optional<Enchainement> = std::nullopt);
    void melangerALaMain (float position);
    bool enchainementALaMain() const;
    float positionALaMain() const                       { return positionManuelle; }
    int sceneALaMain() const                            { return sceneManuelle; }

    /** Montée en cours (filtre et réverbération qui grimpent avant la bascule), pour l'affichage et les essais. */
    bool monteeEnCours() const                          { return montee.active; }

    /** Enchaînement en cours sur une piste, ou le plus récent sur l'ensemble des pistes. */
    std::optional<EtatEnchainement> enchainementDeLaPiste (int piste) const;
    std::optional<EtatEnchainement> enchainementEnCours() const;

    /** Voix (0 : A, 1 : B) qui porte la musique de la piste, une fois l'enchaînement en cours terminé. */
    int voixCourante (int piste) const                  { return voixCourantes[(size_t) piste]; }
    VoixPlugin* reglageDeVoix (int piste, int laquelle) const   { return pistesMoteur[(size_t) piste].reglagesDesVoix[(size_t) laquelle]; }
    void arreterPiste (int piste);
    void arreterTousLesClips();

    EtatCase etatCase (int piste, int scene) const;

    /** Scène du clip qui joue sur la piste, ou -1. */
    int sceneJouee (int piste) const;

    /** Traite ce qui attend le démarrage de la lecture et fait avancer les enchaînements (fin, envois de la queue d'écho).
        Appelé tout seul ; les essais l'appellent à la main. */
    void avancer();

    /** Temps du moteur (depuis le début de la ligne du temps) où démarre la mesure 1. */
    double tempsDeLaMesure1() const;

    /** Joue une note en direct sur la piste (pads du Push en mode Note), sans passer par un clip. */
    void jouerNote (int piste, int note, int velocite);
    void relacherNote (int piste, int note);

    /** Position, en temps du moteur, dans le clip qui joue sur la piste (pour la lumière du pas à pas). */
    std::optional<double> positionDansLeClip (int piste) const;

    BanqueDeSons& getBanque()                   { return *banque; }

    /** Sampler : durée de la boucle au tempo du morceau ; attendre que les boucles soient étirées (essais). */
    double secondesDeLaBoucle (int piste) const;
    void attendreLesBoucles()                   { etireur->attendre(); }
    const SamplerPlugin* samplerDeLaVoix (int piste, int voix) const   { return pistesMoteur[(size_t) piste].samplers[(size_t) voix]; }

    /** Envoi momentané : tant que c'est tenu, la piste part à fond dans le delay dub. */
    void envoiMomentane (int piste, bool tenu);
    bool envoiMomentaneTenu (int piste) const          { return momentanes[(size_t) piste]; }

    /** Filtre global de la sortie (bande tactile) : 0 passe-bas, 0,5 neutre, 1 passe-haut. Jeu en direct, ni sauvegardé ni annulé. */
    void filtreGlobal (float position);

    /** Plugins d'effet d'une piste, pour les essais. */
    te::Plugin* effetDeLaPiste (int piste, int emplacement) const   { return pistesMoteur[(size_t) piste].effets[(size_t) emplacement].get(); }
    DelayDubPlugin* getDelay() const                    { return delay; }

    /** Vrai si la piste joue avec Surge XT (sinon : son provisoire ou batterie). */
    bool joueAvecSurge (int piste) const;

    /** Pilote du Surge de la voix A d'une piste, pour les essais. */
    PiloteDeSurge* piloteSurge (int piste) const        { return pistesMoteur[(size_t) piste].pilotesSurge[0].get(); }

    /** Débuts des notes telles que le moteur les joue (swing compris), pour les essais. */
    std::vector<double> debutsJoues (int piste, int scene) const;

    /** Instrument d'une voix (0 : A, 1 : B), pour les essais. */
    te::AudioTrack* voix (int piste, int laquelle) const   { return pistesMoteur[(size_t) piste].voix[(size_t) laquelle]; }

    //==============================================================================
    /** Prises (bouton Record) : tous les gestes, calés sur les temps, et le son (sortie principale, micros s'il y a une entrée).
        À l'arrêt, la prise lance la lecture et commence à la mesure 1 ; en lecture, elle commence à la mesure en cours.
        Elle se termine au second appui, ou quand la lecture s'arrête. */
    std::function<juce::File()> dossierDesPrises;       ///< Dossier des prises du morceau ; sans lui, pas de prise
    bool demarrerPrise();
    std::optional<juce::File> arreterPrise();           ///< Renvoie le dossier de la prise terminée
    bool priseEnCours() const                           { return prise.has_value(); }
    const Prise* priseCourante() const                  { return prise ? &*prise : nullptr; }
    double dureeDeLaPrise() const;                      ///< En secondes
    std::function<void (const juce::File&)> surPriseTerminee;
    EnregistreurDEntree& getEntreeDesMicros()           { return entreeDesMicros; }

    //==============================================================================
    /** Rejeu d'un déroulé : chaque section s'ouvre à sa mesure (avec son enchaînement d'entrée, demandé avec la même
        avance que pendant la prise), son état est remis et ses gestes rejoués. Une piste qu'on touche (clip, note, réglage,
        envoi) est reprise à la main : le déroulé la laisse ; lancer une scène reprend tout. Rendre la main remet la piste là
        où le déroulé en est. Une section ouverte tourne jusqu'à passerALaSuite. À la fin, ou à l'arrêt, le morceau revient
        tel qu'il était avant le rejeu (rien de ce que le rejeu a changé n'est gardé). */
    bool rejouer (const Deroule&, int depuisLaSection = 0);
    bool rejeuEnCours() const                           { return rejeu.has_value(); }
    void passerALaSuite();
    void rendreLaMain (int piste);                      ///< -1 : toutes les pistes
    bool pisteReprise (int piste) const                 { return rejeu && rejeu->reprises[(size_t) piste]; }

    struct EtatDuRejeu
    {
        juce::String deroule, section, suivante;        ///< Suivante vide : la dernière
        int numero = 0, nombre = 0;
        double mesuresRestantes = 0.0;
        bool ouverte = false, suiteDemandee = false;
        int pistesReprises = 0;
    };

    std::optional<EtatDuRejeu> etatDuRejeu() const;

    /** Le déroulé que Play rejoue (choisi dans l'onglet Retouche) ; vide : Play lance la Session comme d'habitude. */
    std::optional<Deroule> deroulePourPlay;

    /** Play : le déroulé choisi s'il y en a un, sinon la lecture habituelle. */
    void lancerOuRejouer()                              { if (deroulePourPlay && ! deroulePourPlay->sections.empty()) rejouer (*deroulePourPlay); else lancer(); }

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
    std::unique_ptr<EtireurDeBoucles> etireur = std::make_unique<EtireurDeBoucles>();   ///< Détruit avant le moteur audio
    te::AudioTrack* pistePrincipale = nullptr;
    te::AudioTrack* pisteCasque = nullptr;
    BipPlugin* bipPrincipal = nullptr;
    BipPlugin* bipCasque = nullptr;
    ClicPlugin* clic = nullptr;
    te::AuxSendPlugin* envoiCasque = nullptr;
    te::AuxReturnPlugin* retourCasque = nullptr;
    te::MidiClip::Ptr clipDuClic;
    bool lectureVoulue = false;

    /** Un instant de calage : pour lancer les clips (temps monotone) et pour les enchaînements (temps du moteur). */
    struct Instant
    {
        te::MonotonicBeat monotone;
        double temps = 0.0;
    };

    struct PisteMoteur
    {
        te::FolderTrack* dossier = nullptr;
        std::array<te::AudioTrack*, 2> voix {};
        std::array<InstrumentProvisoire*, 2> instruments {};
        std::array<BatteriePlugin*, 2> batteries {};
        std::array<SamplerPlugin*, 2> samplers {};
        std::array<te::Plugin::Ptr, 2> surges;                      ///< Surge XT, s'il est installé (pistes de synthé)
        std::array<te::Plugin::Ptr, 3> effets;                      ///< Effets d'insertion, avant le volume
        std::array<std::optional<TypeEffet>, 3> typesDesEffets;
        te::AuxSendPlugin* envoiDelay = nullptr;
        te::AuxSendPlugin* envoiReverb = nullptr;
        std::array<std::unique_ptr<PiloteDeSurge>, 2> pilotesSurge;
        te::AuxSendPlugin* envoiCasque = nullptr;
        te::VolumeAndPanPlugin* volume = nullptr;
        std::array<VoixPlugin*, 2> reglagesDesVoix {};              ///< Volume et filtres d'enchaînement de chaque voix
        std::array<std::array<te::AuxSendPlugin*, 2>, 2> envoisDesVoix {};   ///< Queue d'écho : [voix][delay, réverbération]
        std::array<std::array<float, 2>, 2> niveauxDesEnvois {};
    };

    /** Enchaînement d'une piste, en temps du moteur. L'avancée va de pDebut à pFin : vers 1 à l'aller, vers 0 en annulation. */
    struct EnchainementDePiste
    {
        bool enCours = false;
        int voixSortante = 0, voixEntrante = 1;
        int sceneSortante = -1, sceneEntrante = -1;
        double debut = 0.0, fin = 0.0, duree = 0.0;
        float pDebut = 0.0f, pFin = 1.0f;
        TypeEnchainement type = TypeEnchainement::fondu;
        float intensite = 0.7f;
        bool manuel = false;            ///< Mené à la bande tactile : l'avancée est celle de la bande
        bool basculeAuMilieu = false;   ///< À la main, batterie nette : la batterie change d'un coup au milieu de la bande

        float avancee (double temps) const;
        bool versLeNouveau() const      { return manuel || pFin > 0.5f; }
        int voixFinale() const          { return versLeNouveau() ? voixEntrante : voixSortante; }
        int sceneQuittee() const        { return versLeNouveau() ? sceneSortante : sceneEntrante; }
        int sceneVisee() const          { return versLeNouveau() ? sceneEntrante : sceneSortante; }
    };

    float positionManuelle = 0.0f;
    bool bandeDejaMontee = false;
    int sceneManuelle = -1;

    /** Montée avant une bascule, en temps du moteur. */
    struct Montee
    {
        bool active = false;
        double debut = 0.0, fin = 0.0;
        float intensite = 0.7f;
    };

    Montee montee;
    std::array<float, nombreDePistes> envoisReverbDeLaMontee {};

    std::array<EnchainementDePiste, nombreDePistes> enchainements;
    std::array<int, nombreDePistes> voixCourantes {};

    std::array<PisteMoteur, nombreDePistes> pistesMoteur;
    std::array<bool, nombreDePistes> momentanes {};

    // Effets de départ et sortie
    te::AudioTrack* pisteDelay = nullptr;
    te::AudioTrack* pisteReverb = nullptr;
    DelayDubPlugin* delay = nullptr;
    te::ReverbPlugin* reverb = nullptr;
    std::array<te::AuxSendPlugin*, 2> envoisCasqueDesRetours {};
    FiltrePlugin* filtreDeSortie = nullptr;
    LimiteurPlugin* limiteurDeSortie = nullptr;
    LimiteurPlugin* limiteurDuCasque = nullptr;

    void appliquerEffets (int piste, const Morceau& ancien, bool tout);
    void appliquerEffetsDeDepart();

    // Démarrage : la lecture part quand la position demandée est prise en compte, avec les clips déjà calés
    bool demarrageEnAttente = false;
    double instantDuDemarrage = 0.0, tempsDuDepart = 0.0;
    std::vector<std::pair<int, int>> lancementsAuDemarrage;

    // Prise en cours
    std::optional<Prise> prise;
    juce::File dossierDeLaPrise;
    double instantDuDebutDeLaPrise = 0.0;
    EnregistreurDeSortiePlugin* enregistreurDeSortie = nullptr;
    EnregistreurDEntree entreeDesMicros;
    std::unique_ptr<EnregistrementAudio> enregistrementAudio;
    int imbrication = 0;

    /** Un appel venu de l'extérieur (et non d'une autre fonction du moteur) : c'est lui qu'on note dans la prise. */
    struct Appel
    {
        Moteur& m;
        const bool exterieur;
        explicit Appel (Moteur& mm)  : m (mm), exterieur (mm.imbrication++ == 0) {}
        ~Appel()                     { --m.imbrication; }
        bool aNoter() const          { return exterieur && m.prise.has_value(); }
    };

    void noter (EvenementDePrise);
    void noterReglages (const Morceau& avant);
    double caleDuLancement() const;

    // Rejeu en cours
    struct Rejeu
    {
        Deroule deroule;
        int section = 0;
        double debut = 0.0;                 ///< Temps du moteur du début de la section en cours
        size_t prochain = 0;                ///< Prochain geste à rejouer dans la section
        int sectionOuverte = -1;            ///< Section suivante déjà lancée (son lancement part avant sa mesure)
        double debutDeLaSuivante = 0.0;
        bool suiteDemandee = false;
        double finDemandee = 0.0;           ///< Section ouverte : mesure où l'on passe à la suite
        std::array<bool, nombreDePistes> reprises {};
        juce::var attendu;                  ///< Le morceau tel que le déroulé le voudrait maintenant
        std::array<int, nombreDePistes> scenesAttendues;
        Morceau avant;
        size_t tailleDeLHistorique = 0;
    };

    std::optional<Rejeu> rejeu;
    bool rejeuAgit = false;
    std::optional<double> caleForcee;

    void suivreLeRejeu();
    void lancerLaSection (int section, double debut);
    void entrerDansLaSection (int section, double debut);
    void rejouerGeste (const EvenementDePrise&);
    void appliquerEtat (const juce::var& vise, std::function<bool (const juce::String&)> garder = {});
    bool cheminRepris (const juce::String&) const;
    void reprendre (int piste);
    void terminerLeRejeu();

    // Annulation
    std::vector<Morceau> historique, futur;
    juce::String dernierGeste;
    double instantDuDernierGeste = 0.0;

    void appliquerDifferences (const Morceau& ancien);
    void appliquerPistes (const Morceau& ancien, bool tout);
    void appliquerSampler (int piste);
    void appliquerCases (const Morceau& ancien, bool tout);
    void appliquerCase (int piste, int scene);
    void assurerCases();
    void appliquerCaseSurLaVoix (int piste, int scene, int voix);
    te::MidiClip* clipDeLaCase (int piste, int scene, int voix = 0) const;
    double longueurDuCalage() const;
    std::optional<Instant> prochainCalage (std::optional<double> tempsVise = std::nullopt) const;
    void lancerSurLaVoix (int piste, int voix, int scene, te::MonotonicBeat);
    void arreterLaVoix (int piste, int voix, std::optional<te::MonotonicBeat>);
    void arreterMaintenant (int piste, std::optional<te::MonotonicBeat>);
    int sceneSurLaVoix (int piste, int voix) const;
    int voixQuiJoue (int piste, int scene) const;

    // Enchaînements
    void enchainer (int piste, int scene, const Enchainement&, bool aLaMain = false);
    void terminerALaMain (bool versLeNouveau);
    void finirALaMainToutSeul();
    void programmerMontee (double debut, double fin, float intensite);
    void annulerMontee();
    void suivreLaMontee (double maintenant);
    bool pisteAChanger (int piste, int scene, bool nette) const;
    void annulerEnchainement (int piste);
    void terminerEnchainement (int piste);
    void programmerLesVoix (int piste);
    void reposerLesVoix (int piste);
    void reglerEnvoisDeVoix (int piste, int voix, float niveau);
    void appliquerSurges (int piste);
    void suivreLesEnchainements();
    void avancerLeDemarrage();
    double tempsActuel() const;
    double margeDuFilPrincipal() const;
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
