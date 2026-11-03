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

    enum class Page { mix, clic, device, browse };
    enum class Mode { session, note };
    Page getPage() const                    { return page; }
    Mode getMode() const                    { return mode; }
    int getPadChoisi() const                { return padChoisi; }

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
    juce::StringArray categoriesDuNavigateur;
    int categorieChoisie = 0, sonChoisi = 0;
    std::optional<std::pair<int, int>> caseChoisie;
    std::optional<std::pair<int, int>> sourceDeCopie;     ///< Duplicate tenu : premier pad appuyé
    std::optional<int> sceneSourceDeCopie;
    bool copieFaite = false;
    std::array<bool, nombreEncodeurs> touches {};
    std::array<int, nombreEncodeurs> crans {};
    std::array<Reglage, 8> pageClic, pageMix, pageDevice, pageBrowse;
    Annonce annonce;
    juce::Image ecran { juce::Image::RGB, EtatSurface::largeurEcran, EtatSurface::hauteurEcran, true };

    const std::array<Reglage, 8>& reglages() const
    {
        switch (page)
        {
            case Page::mix:     return pageMix;
            case Page::clic:    return pageClic;
            case Page::device:  return pageDevice;
            case Page::browse:  return pageBrowse;
        }

        return pageMix;
    }

    bool pisteChoisieEstUneBatterie() const;
    void modifierPad (int encodeur, std::function<void (PadBatterie&)>);
    juce::StringArray sonsDeLaCategorie() const;
    void choisirDansLeNavigateur();
    void appuyerPadDeNote (int ligne, int colonne, int velocite);
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
