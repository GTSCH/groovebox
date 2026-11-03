#pragma once

#include "Texte.h"

namespace groovebox
{
namespace te = tracktion;

/** Dernier maillon de la sortie principale : recopie le son dans le fichier de la prise, sans le toucher.
    Le fil audio ne fait que déposer les échantillons ; un fil à part les écrit sur le disque. */
class EnregistreurDeSortiePlugin  : public te::Plugin
{
public:
    static const char* xmlTypeName;
    explicit EnregistreurDeSortiePlugin (te::PluginCreationInfo info)  : te::Plugin (info) {}
    ~EnregistreurDeSortiePlugin() override                      { notifyListenersOfDeletion(); }

    juce::String getName() const override                       { return "Enregistreur"; }
    juce::String getPluginType() override                       { return xmlTypeName; }
    juce::String getShortName (int) override                    { return getName(); }
    juce::String getSelectableDescription() override            { return getName(); }
    int getNumOutputChannelsGivenInputs (int) override          { return 2; }
    void initialise (const te::PluginInitialisationInfo&) override {}
    void deinitialise() override {}
    void applyToBuffer (const te::PluginRenderContext&) override;

    /** Fil principal. nullptr : plus rien n'est écrit (au retour, le fil audio n'y touche plus). */
    void brancher (juce::AudioFormatWriter::ThreadedWriter*);

    /** Instant de la ligne du temps (secondes) du premier échantillon écrit, ou -1. */
    double premierInstant() const                               { return premier.load(); }

private:
    juce::SpinLock verrou;
    juce::AudioFormatWriter::ThreadedWriter* ecrivain = nullptr;
    std::atomic<double> premier { -1.0 };
};

/** Les micros : l'entrée stéréo de la carte son, lue à côté du moteur (canaux 1 et 2). */
class EnregistreurDEntree  : public juce::AudioIODeviceCallback
{
public:
    void brancher (juce::AudioFormatWriter::ThreadedWriter*);

    /** Nombre de canaux d'entrée ouverts sur la carte son (0 : pas de micro possible). */
    int canauxDisponibles() const                               { return canaux.load(); }

    void audioDeviceIOCallbackWithContext (const float* const* entrees, int nombreDEntrees, float* const* sorties, int nombreDeSorties,
                                           int nombreDEchantillons, const juce::AudioIODeviceCallbackContext&) override;
    void audioDeviceAboutToStart (juce::AudioIODevice*) override;
    void audioDeviceStopped() override                          { canaux = 0; }

private:
    juce::SpinLock verrou;
    juce::AudioFormatWriter::ThreadedWriter* ecrivain = nullptr;
    std::atomic<int> canaux { 0 };
};

/** Les fichiers son d'une prise : sortie.wav et, s'il y a une entrée, micros.wav (WAV 48 kHz ou la fréquence de la carte, 24 bits). */
class EnregistrementAudio
{
public:
    EnregistrementAudio (EnregistreurDeSortiePlugin&, EnregistreurDEntree&);
    ~EnregistrementAudio();

    bool demarrer (const juce::File& dossier, double frequence, bool avecMicros);
    void arreter();
    bool enCours() const                                        { return sortie != nullptr; }

private:
    EnregistreurDeSortiePlugin& plugin;
    EnregistreurDEntree& entree;
    juce::TimeSliceThread fil { "Écriture des prises"_fr };
    std::unique_ptr<juce::AudioFormatWriter::ThreadedWriter> sortie, micros;

    std::unique_ptr<juce::AudioFormatWriter::ThreadedWriter> ouvrir (const juce::File&, double frequence);
};

} // namespace groovebox
