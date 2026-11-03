#pragma once

#include "moteur/Moteur.h"

namespace groovebox
{

/** Réécoute d'une prise : sortie.wav, et les micros par-dessus s'il y en a, sur les sorties 1-2 seulement. */
class LecteurDePrise  : public juce::AudioIODeviceCallback
{
public:
    explicit LecteurDePrise (juce::AudioDeviceManager&);
    ~LecteurDePrise() override;

    /** Écoute depuis un instant (secondes dans les fichiers), jusqu'à un autre si on le donne. */
    bool ecouter (const juce::File& dossier, double depuis = 0.0, std::optional<double> jusqua = std::nullopt);
    void arreter();
    bool enCours() const;
    double position() const;
    double duree() const;

    void audioDeviceIOCallbackWithContext (const float* const*, int, float* const*, int, int, const juce::AudioIODeviceCallbackContext&) override;
    void audioDeviceAboutToStart (juce::AudioIODevice*) override;
    void audioDeviceStopped() override;

private:
    juce::AudioDeviceManager& cartes;
    juce::AudioFormatManager formats;
    juce::TimeSliceThread lecture { "Lecture des prises" };
    std::array<std::unique_ptr<juce::AudioFormatReaderSource>, 2> sources;
    std::array<juce::AudioTransportSource, 2> transports;
    juce::MixerAudioSource melange;
    juce::AudioBuffer<float> tampon;
    juce::CriticalSection verrou;
    double frequence = 48000.0;
    int taille = 512;
    std::optional<double> fin;
};

/** Onglet « Prises » : les prises du morceau ouvert. Enregistrer, réécouter, renommer, retrouver dans le Finder, jeter. */
class PagePrises  : public juce::Component,
                    private juce::ListBoxModel,
                    private juce::Timer
{
public:
    PagePrises (te::Engine&, Moteur&);
    ~PagePrises() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    struct Ligne
    {
        juce::File dossier;
        juce::String nom, date;
        double duree = 0.0;
        int gestes = 0;
        bool micros = false;
        juce::Time modification;
    };

    Moteur& moteur;
    LecteurDePrise lecteur;
    std::vector<Ligne> lignes;
    juce::File dossierAffiche;
    juce::String dossierEnEcoute;

    juce::Label titre, infos, etatDeLaCarte;
    juce::ListBox liste { "Prises", this };
    juce::TextButton boutonRecord { "Enregistrer une prise" }, boutonEcouter { "Écouter"_fr }, boutonRenommer { juce::String::fromUTF8 ("Renommer…") },
                     boutonFinder { "Montrer dans le Finder" }, boutonCorbeille { "Mettre à la corbeille"_fr };

    void relire();
    const Ligne* ligneChoisie() const;
    void renommer();
    void jeter();
    void timerCallback() override;

    int getNumRows() override                       { return (int) lignes.size(); }
    void paintListBoxItem (int, juce::Graphics&, int, int, bool) override;
    void selectedRowsChanged (int) override         { mettreAJourBoutons(); }
    void mettreAJourBoutons();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PagePrises)
};

} // namespace groovebox
