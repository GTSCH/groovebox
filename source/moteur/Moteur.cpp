#include "Moteur.h"

namespace groovebox
{

namespace
{
    BipPlugin* ajouterBip (te::AudioTrack& piste, float frequence)
    {
        auto plugin = piste.edit.getPluginCache().createNewPlugin (BipPlugin::xmlTypeName, {});
        piste.pluginList.insertPlugin (plugin, 0, nullptr);

        auto bip = dynamic_cast<BipPlugin*> (plugin.get());
        jassert (bip != nullptr);
        bip->frequence = frequence;
        return bip;
    }
}

Moteur::Moteur (te::Engine& e)  : engine (e)
{
    engine.getPluginManager().createBuiltInType<BipPlugin>();

    edit = te::Edit::createSingleTrackEdit (engine);
    edit->ensureNumberOfAudioTracks (2);

    auto pistes = te::getAudioTracks (*edit);
    pistePrincipale = pistes[0];
    pisteCasque = pistes[1];
    pistePrincipale->setName ("Test sorties 1-2");
    pisteCasque->setName ("Test sorties 3-4");

    bipPrincipal = ajouterBip (*pistePrincipale, 440.0f);
    bipCasque = ajouterBip (*pisteCasque, 880.0f);

    appliquerRoutage();
    engine.getDeviceManager().addChangeListener (this);

    // La lecture tourne en permanence : le jeu ne s'arrête jamais
    auto& transport = edit->getTransport();
    transport.ensureContextAllocated();
    transport.play (false);
}

Moteur::~Moteur()
{
    engine.getDeviceManager().removeChangeListener (this);
    edit->getTransport().stop (false, true);
}

BipPlugin* Moteur::bip (Sortie s) const
{
    return s == Sortie::principale ? bipPrincipal : bipCasque;
}

void Moteur::activerBip (Sortie s, bool actif)
{
    if (s == Sortie::casque && ! sortieCasqueDisponible())
        actif = false;

    bip (s)->actif = actif;
}

bool Moteur::bipActif (Sortie s) const
{
    return bip (s)->actif;
}

void Moteur::reglerNiveauBip (Sortie s, float niveau)
{
    bip (s)->niveau = juce::jlimit (0.0f, 1.0f, niveau);
}

te::WaveOutputDevice* Moteur::trouverSortie (int premierCanal) const
{
    auto& peripheriques = engine.getDeviceManager();

    for (int i = 0; i < peripheriques.getNumWaveOutDevices(); ++i)
        if (auto sortie = peripheriques.getWaveOutDevice (i))
            if (sortie->isEnabled() && sortie->getLeftChannel() == premierCanal)
                return sortie;

    return nullptr;
}

bool Moteur::sortieCasqueDisponible() const
{
    return trouverSortie (2) != nullptr;
}

void Moteur::appliquerRoutage()
{
    auto& peripheriques = engine.getDeviceManager();

    if (auto sortie12 = trouverSortie (0))
        if (peripheriques.getDefaultWaveOutDeviceID() != sortie12->getDeviceID())
            peripheriques.setDefaultWaveOutDevice (sortie12->getDeviceID());

    pistePrincipale->getOutput().setOutputToDefaultDevice (false);

    if (auto sortie34 = trouverSortie (2))
    {
        pisteCasque->getOutput().setOutputToDeviceID (sortie34->getDeviceID());
        pisteCasque->setMute (false);
    }
    else
    {
        // Pas de sorties 3-4 : la piste du casque se tait plutôt que de finir dans la sono
        pisteCasque->setMute (true);
        bipCasque->actif = false;
    }
}

void Moteur::changeListenerCallback (juce::ChangeBroadcaster*)
{
    appliquerRoutage();

    if (auto contexte = edit->getCurrentPlaybackContext())
        contexte->reallocate();

    // Quand le moteur refait la liste des sorties (au démarrage ou au changement de carte), il arrête la lecture : on la relance
    auto& transport = edit->getTransport();

    if (! transport.isPlaying())
    {
        juce::Logger::writeToLog ("Lecture relancée après un changement de carte son");
        transport.play (false);
    }

    if (surChangementDeCarte)
        surChangementDeCarte();
}

} // namespace groovebox
