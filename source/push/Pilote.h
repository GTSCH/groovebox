#pragma once

#include "Surface.h"
#include "moteur/Moteur.h"
#include "modele/TapTempo.h"

namespace groovebox
{

/** Ce que fait chaque contrôle du Push, et ce que le Push montre : LED et écran.
    Pads : la Session (8 pistes en colonnes, 8 scènes en lignes, la première scène en haut).
    Encodeurs : page Mix (volumes des pistes) ou page Clic (bouton Setup). */
class Pilote  : private juce::Timer
{
public:
    Pilote (Moteur&, Surface&);
    ~Pilote() override;

    void traiter (const Evenement&);

    /** Calcule l'état des LED et dessine l'écran, puis les envoie au Push et au simulateur. */
    void rafraichir();

    EtatSurface calculerEtat() const;
    void dessinerEcran (juce::Image&) const;

    enum class Page { mix, clic, device, generateur, commun, variation, browse, gamme, macros, melodie, communSynthe,
                      envoisDelay, envoisReverb, departs, effet, ajoutEffet, enchainement, sampler, communSampler };
    enum class Mode { session, note };
    Page getPage() const                    { return page; }
    Mode getMode() const                    { return mode; }
    int getPadChoisi() const                { return padChoisi; }
    int getEmplacementEffet() const         { return emplacementEffet; }

    /** Jeu en gamme : note MIDI d'un pad (ligne 0 en bas) sur une piste de synthé. */
    int noteDuPad (int ligne, int colonne) const;
    int getOctave() const                   { return octaves[(size_t) pisteChoisie]; }

    /** Pas à pas : longueur d'un pas en temps du moteur, et nombre de pas du clip visé. */
    double longueurDUnPas() const;
    int nombreDePas() const;

    /** Clip que vise le pas à pas : celui qui joue sur la piste choisie, sinon la case choisie. */
    std::optional<std::pair<int, int>> caseDuPasAPas() const;
    int getPremiereScene() const            { return premiereScene; }
    int getPisteChoisie() const             { return pisteChoisie; }

    /** Scène montrée par une ligne de pads (ligne 7 en haut = première scène visible). */
    int sceneDeLaLigne (int ligne) const    { return premiereScene + (7 - ligne); }

    struct Annonce
    {
        juce::String titre, valeur;
        double fin = 0.0;
    };

    const Annonce& getAnnonce() const       { return annonce; }

    /** Des sons glissés sur la fenêtre du Mac, déjà copiés dans Sons : le premier va au sampler de la piste choisie. */
    void sonsDeposes (const juce::StringArray& chemins);

private:
    struct Reglage
    {
        juce::String nom;
        std::function<juce::String()> valeur;
        std::function<float()> proportion;
        std::function<void (int pas, bool fin)> tourner;
        juce::Colour couleur;
    };

    Moteur& moteur;
    Surface& surface;
    TapTempo tapTempo;
    Page page = Page::mix;
    Mode mode = Mode::session;
    int premiereScene = 0, pisteChoisie = 0, padChoisi = 0, pageDePas = 0;
    std::array<bool, 16> padsAppuyes {};
    std::array<int, 64> notesTenues {};                     ///< Jeu en gamme : note jouée par chaque pad, -1 sinon
    std::array<int, nombreDePistes> octaves { 2, 1, 3, 4, 3, 2, 3, 4 };
    juce::StringArray categoriesDuNavigateur;
    int categorieChoisie = 0, sonChoisi = 0;
    std::optional<std::pair<int, int>> caseChoisie;
    std::optional<std::pair<int, int>> sourceDeCopie;     ///< Duplicate tenu : premier pad appuyé
    std::optional<int> sceneSourceDeCopie;
    bool copieFaite = false;
    std::array<bool, nombreEncodeurs> touches {};
    std::array<int, nombreEncodeurs> crans {};
    std::array<Reglage, 8> pageClic, pageMix, pageDevice, pageBrowse, pageEuclide, pageCartes, pageCommun, pageVariation, pageGamme, pageMacros, pageMelodie, pageCommunSynthe,
                           pageEnvoisDelay, pageEnvoisReverb, pageDeparts, pageEffet, pageAjout, pageEnchainement, pageSampler, pageCommunSampler,
                           pageBatteur;
    int emplacementEffet = 0, typeAAjouter = 0, emplacementAAjouter = 0;
    int voixEuclidienne = 0;
    bool varierEnDupliquant = false;
    bool ignorerLaBande = false;        ///< Après un mélange à la main, la bande attend d'être lâchée avant de redevenir le filtre
    Annonce annonce;
    std::shared_ptr<bool> vivant = std::make_shared<bool> (true);
    juce::Image ecran { juce::Image::RGB, EtatSurface::largeurEcran, EtatSurface::hauteurEcran, true };

    const std::array<Reglage, 8>& reglages() const
    {
        switch (page)
        {
            case Page::mix:     return pageMix;
            case Page::clic:    return pageClic;
            case Page::device:  return pageDevice;
            case Page::browse:  return pageBrowse;
            case Page::commun:  return pageCommun;
            case Page::variation: return pageVariation;
            case Page::gamme:   return pageGamme;
            case Page::macros:  return pageMacros;
            case Page::melodie: return pageMelodie;
            case Page::communSynthe: return pageCommunSynthe;
            case Page::envoisDelay: return pageEnvoisDelay;
            case Page::envoisReverb: return pageEnvoisReverb;
            case Page::departs: return pageDeparts;
            case Page::effet:   return pageEffet;
            case Page::ajoutEffet: return pageAjout;
            case Page::enchainement: return pageEnchainement;
            case Page::sampler: return pageSampler;
            case Page::communSampler: return pageCommunSampler;
            case Page::generateur:
            {
                // Sans générateur, une batterie guide part sur le moteur batteur
                const auto* g = generateurVise();
                const auto m = g != nullptr ? g->moteur : (pisteChoisieEstUneGuide() ? MoteurRythmique::batteur : MoteurRythmique::euclidien);
                return m == MoteurRythmique::batteur ? pageBatteur : m != MoteurRythmique::euclidien ? pageCartes : pageEuclide;
            }
        }

        return pageMix;
    }

    bool pisteChoisieEstUneBatterie() const;
    bool pisteChoisieEstUneGuide() const;

    /** Layout : coupe toutes les batteries guides, ou les rétablit. */
    void basculerLaGuide();

    /** Batterie guide : un clip généré par le batteur dans la case visée, si elle est vide. */
    void preparerClipDeGuide();
    bool pisteChoisieEstUnSampler() const;
    bool pisteChoisieEstUnSynthe() const;
    bool pisteChoisieJouePads() const;      ///< Batterie, ou sampler découpé : pads en 4 sur 4 et pas à pas
    void modifierSampler (int encodeur, std::function<void (ReglagesSampler&)>);
    void preparerClipDeBoucle();
    void changerDeMachine (const juce::String& machine);
    void ouvrirLeNavigateur();
    void preecouterLeSampler();
    const Generateur* generateurVise() const;
    void modifierGenerateur (int encodeur, std::function<void (Generateur&)>);
    const GenerateurMelodique* melodieVisee() const;
    void modifierMelodie (int encodeur, std::function<void (GenerateurMelodique&)>);
    void modifierClipVise (int encodeur, std::function<void (Clip&)>);
    void appliquerVariation (Clip&, Morceau&);
    void figerLeClip();
    void modifierPad (int encodeur, std::function<void (PadBatterie&)>);
    juce::StringArray sonsDeLaCategorie() const;
    void choisirDansLeNavigateur();
    void appuyerPadDeNote (int ligne, int colonne, int velocite);
    void preecouterLeSynthe();
    void pageSuivanteDeDevice();
    void modifierEffet (int encodeur, std::function<void (Effet&)>);
    void basculerPas (int pas);
    bool shift() const;
    bool tenu (Bouton b) const                      { return surface.estAppuye (b); }
    void annoncer (const juce::String& titre, const juce::String& valeur);
    int crantsDiscrets (int encodeur, int pas);
    void tournerEncodeur (int encodeur, int pas);
    void appuyer (int bouton);
    void relacher (int bouton);
    void appuyerPad (int ligne, int colonne, int velocite = 100);
    void appuyerScene (int scene);
    void appuyerPiste (int piste);
    void dupliquerVersLeBas (int piste, int scene);
    void timerCallback() override                   { rafraichir(); }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Pilote)
};

} // namespace groovebox
