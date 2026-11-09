#pragma once

#include "moteur/Exports.h"

namespace groovebox
{

/** Onglet « Exports » : un déroulé, ou une scène sur des mesures choisies, en WAV 48 kHz 24 bits, dans le dossier
    « exports » du morceau. Le calcul se fait dans un second groovebox (--exporter), plus vite que la lecture,
    sans gêner la musique en cours. Aussi : la copie d'une prise de répétition. */
class PageExports  : public juce::Component,
                     private juce::Timer
{
public:
    explicit PageExports (Moteur&);
    ~PageExports() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void visibilityChanged() override;

private:
    /** Le second groovebox : lancé, lu ligne à ligne, puis fini. */
    class Tache  : public juce::Thread
    {
    public:
        Tache (juce::File demande, std::function<void (const juce::String&)> ligne, std::function<void (bool)> fin);
        ~Tache() override;
        void run() override;

    private:
        juce::File demande;
        std::function<void (const juce::String&)> surLigne;
        std::function<void (bool)> surFin;
        juce::ChildProcess processus;
    };

    struct Source
    {
        juce::String nom;
        std::optional<Deroule> deroule;
        int scene = 0;
    };

    Moteur& moteur;
    std::vector<Source> sources;
    std::vector<juce::File> prises;
    juce::String morceauAffiche;
    std::unique_ptr<Tache> tache;
    double progression = 0.0;
    juce::StringArray fichiersFaits, remarques;
    juce::File dernierFichier;

    juce::Label titre, infos, etiquetteSource, etiquetteMesures, etiquettePrise, etat, resultat;
    juce::ComboBox choixSource, choixMesures, choixPrise;
    juce::TextButton boutonUnGeste { "Mix sans guide + clic" }, boutonSelection { juce::String::fromUTF8 ("Exporter ce qui est coché") },
                     boutonFinder { "Montrer dans le Finder" }, boutonPrise { "Exporter la prise" };
    juce::ToggleButton caseComplet { "Mix complet" }, caseSansGuide { "Mix sans guide" }, caseAvecClic { "Mix sans guide + clic" },
                       casePistes { juce::String::fromUTF8 ("Pistes séparées") };
    juce::ProgressBar barre { progression };

    juce::File dossierDuMorceau() const;
    juce::File dossierDesExports() const        { return dossierDuMorceau().getChildFile ("exports"); }
    void relire();
    void exporter (std::vector<exports::Rendu>);
    void exporterLaPrise();
    void recevoir (const juce::String& ligne);
    void terminer (bool reussi);
    void mettreAJour();
    void timerCallback() override;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PageExports)
};

} // namespace groovebox
