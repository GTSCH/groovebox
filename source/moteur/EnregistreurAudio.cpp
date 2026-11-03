#include "EnregistreurAudio.h"

namespace groovebox
{

const char* EnregistreurDeSortiePlugin::xmlTypeName = "groovebox_enregistreur";

void EnregistreurDeSortiePlugin::brancher (juce::AudioFormatWriter::ThreadedWriter* e)
{
    const juce::SpinLock::ScopedLockType l (verrou);
    ecrivain = e;
    premier = -1.0;
}

void EnregistreurDeSortiePlugin::applyToBuffer (const te::PluginRenderContext& fc)
{
    if (fc.destBuffer == nullptr)
        return;

    const juce::SpinLock::ScopedTryLockType l (verrou);

    if (! l.isLocked() || ecrivain == nullptr)
        return;

    auto& b = *fc.destBuffer;
    const float* canaux[2] = { b.getReadPointer (0, fc.bufferStartSample), b.getReadPointer (b.getNumChannels() > 1 ? 1 : 0, fc.bufferStartSample) };

    if (premier.load() < 0.0)
        premier = fc.editTime.getStart().inSeconds();

    ecrivain->write (canaux, fc.bufferNumSamples);
}

//==============================================================================
void EnregistreurDEntree::brancher (juce::AudioFormatWriter::ThreadedWriter* e)
{
    const juce::SpinLock::ScopedLockType l (verrou);
    ecrivain = e;
}

void EnregistreurDEntree::audioDeviceAboutToStart (juce::AudioIODevice* carte)
{
    canaux = carte != nullptr ? carte->getActiveInputChannels().countNumberOfSetBits() : 0;
}

void EnregistreurDEntree::audioDeviceIOCallbackWithContext (const float* const* entrees, int nombreDEntrees, float* const* sorties, int nombreDeSorties,
                                                             int n, const juce::AudioIODeviceCallbackContext&)
{
    // Ce rappel ne joue rien : sa sortie s'ajoute à celle du moteur
    for (int c = 0; c < nombreDeSorties; ++c)
        if (sorties[c] != nullptr)
            juce::FloatVectorOperations::clear (sorties[c], n);

    if (nombreDEntrees <= 0 || entrees[0] == nullptr)
        return;

    const juce::SpinLock::ScopedTryLockType l (verrou);

    if (! l.isLocked() || ecrivain == nullptr)
        return;

    const float* canaux[2] = { entrees[0], nombreDEntrees > 1 && entrees[1] != nullptr ? entrees[1] : entrees[0] };
    ecrivain->write (canaux, n);
}

//==============================================================================
EnregistrementAudio::EnregistrementAudio (EnregistreurDeSortiePlugin& p, EnregistreurDEntree& e)  : plugin (p), entree (e)
{
    fil.startThread();
}

EnregistrementAudio::~EnregistrementAudio()
{
    arreter();
    fil.stopThread (2000);
}

std::unique_ptr<juce::AudioFormatWriter::ThreadedWriter> EnregistrementAudio::ouvrir (const juce::File& fichier, double frequence)
{
    fichier.deleteFile();
    auto flux = std::make_unique<juce::FileOutputStream> (fichier);

    if (flux->failedToOpen())
        return {};

    juce::WavAudioFormat wav;
    auto* ecrivain = wav.createWriterFor (flux.get(), frequence, 2, 24, {}, 0);

    if (ecrivain == nullptr)
        return {};

    flux.release();     // l'écrivain garde le flux

    // 8 secondes d'avance : le disque peut prendre son temps sans jamais faire attendre le son
    return std::make_unique<juce::AudioFormatWriter::ThreadedWriter> (ecrivain, fil, (int) (8.0 * frequence));
}

bool EnregistrementAudio::demarrer (const juce::File& dossier, double frequence, bool avecMicros)
{
    arreter();
    sortie = ouvrir (dossier.getChildFile ("sortie.wav"), frequence);

    if (sortie == nullptr)
        return false;

    if (avecMicros)
        micros = ouvrir (dossier.getChildFile ("micros.wav"), frequence);

    // Les deux en même temps, pour qu'ils partent au même bloc
    plugin.brancher (sortie.get());
    entree.brancher (micros.get());
    return true;
}

void EnregistrementAudio::arreter()
{
    plugin.brancher (nullptr);
    entree.brancher (nullptr);

    // Détruire l'écrivain vide sa réserve sur le disque et termine le fichier
    sortie.reset();
    micros.reset();
}

} // namespace groovebox
