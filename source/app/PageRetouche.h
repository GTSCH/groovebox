#pragma once

#include "PagePrises.h"
#include "modele/Deroule.h"

namespace groovebox
{

/** Onglet « Retouche » : une prise (ou un déroulé déjà créé) en ligne du temps découpée en sections, avec les courbes
    des réglages sous chaque piste. On écoute, coupe, supprime, déplace, répète, renomme, lisse ou efface une courbe,
    puis « Créer le déroulé » l'enregistre à côté du morceau. */
class PageRetouche  : public juce::Component,
                      private juce::Timer
{
public:
    PageRetouche (te::Engine&, Moteur&);

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;

    /** Pour les essais : ouvrir une prise ou un déroulé, et lire l'état. */
    void ouvrirPrise (const juce::File& dossierDeLaPrise);
    void ouvrirDeroule (const juce::File& fichier);
    const std::optional<Deroule>& getDeroule() const   { return deroule; }
    void choisirSection (int i)                         { sectionChoisie = i; choisirUneLigneQuiBouge(); miseAJour(); }
    int getLigneChoisie() const                         { return ligneChoisie; }

private:
    struct Source
    {
        juce::String nom;
        juce::File fichier;     ///< Dossier d'une prise, ou fichier d'un déroulé
        bool estUnDeroule = false;
    };

    Moteur& moteur;
    LecteurDePrise lecteur;
    std::vector<Source> sources;
    std::optional<Deroule> deroule;
    std::vector<Deroule> historique;
    juce::File fichierDuDeroule;           ///< Vide tant que le déroulé n'est pas enregistré
    bool modifie = false;
    int sectionChoisie = -1, ligneChoisie = -1;     ///< Ligne : -1 le morceau, 0 à 7 les pistes
    double curseur = -1.0;                 ///< Temps dans le déroulé (sur une mesure)
    std::map<juce::String, std::pair<double, double>> bornes;   ///< Plus petite et plus grande valeur de chaque courbe
    bool ecouteEnCours = false;
    int compteur = 0;

    juce::Label titre, infos;
    juce::ComboBox choixDeLaSource, choixDeLEntree, choixDeLaDuree, choixDeLaCourbe;
    juce::Label etiquetteEntree { {}, juce::String ("Entrée :"_fr) }, etiquetteCourbe { {}, "Courbe :" };
    juce::TextButton boutonEcouter { "Écouter"_fr }, boutonCouper { "Couper au curseur" }, boutonSupprimer { "Supprimer" },
                     boutonAvant { "Vers le début"_fr }, boutonApres { "Vers la fin" }, boutonRepeter { "Répéter"_fr },
                     boutonRenommer { juce::String::fromUTF8 ("Renommer…") }, boutonLisser { "Lisser" }, boutonEffacer { "Effacer" },
                     boutonAnnuler { "Annuler la retouche" }, boutonCreer { juce::String ("Créer le déroulé…"_fr) };
    juce::Rectangle<int> zoneDesSections, zoneDesLignes;

    juce::File dossierDesPrises() const;
    juce::File dossierDesDeroules() const;
    void relireLesSources();
    void choisirSource();
    void modifier (const std::function<bool (Deroule&)>&);
    void miseAJour();
    void calculerBornes();
    void choisirUneLigneQuiBouge();
    void montrerDansLeMenu (const juce::File&);
    void ecouter();
    void renommer();
    void creer();
    void changerEntree();
    Section* section();
    double versTemps (int x) const;
    float versX (double temps) const;
    void timerCallback() override;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PageRetouche)
};

} // namespace groovebox
