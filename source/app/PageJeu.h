#pragma once

#include "moteur/Moteur.h"
#include "modele/TapTempo.h"

namespace groovebox
{

/** Onglet « Jeu » : lecture, position, tempo, mesure et clic. */
class PageJeu  : public juce::Component,
                 private juce::Timer
{
public:
    explicit PageJeu (Moteur&);
    ~PageJeu() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    void lectureOuArret();
    void taperLeTempo();

private:
    Moteur& moteur;
    TapTempo tapTempo;
    std::optional<double> tempoEnAttente;

    juce::TextButton boutonLecture { "Lecture" };
    juce::Label position, aideMesure;

    juce::Label libelleTempo, libelleMesure, libelleGroupement, libelleClic, libelleCasque;
    juce::TextButton moinsTempo { "−"_fr }, plusTempo { "+" }, boutonTap { "Tap Tempo" };
    juce::Slider tempo;
    juce::ComboBox choixMesure, choixGroupement, choixDecompte;

    juce::ToggleButton clicActif { "Clic" }, subdivisions { "Subdivisions" }, unClicParGroupe { "Un clic par groupe" };
    juce::ToggleButton ecouteSurMac { "Écouter le clic sur le Mac (essai sans carte son)"_fr };
    juce::Slider volumeClic, musiqueDansCasque;

    juce::Rectangle<int> zoneDesTemps;
    PositionMusicale positionAffichee;

    void mettreAJour();
    void remplirMesures();
    void remplirGroupements();
    void choisirAutreMesure();
    void choisirAutreGroupement();
    void timerCallback() override;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PageJeu)
};

} // namespace groovebox
