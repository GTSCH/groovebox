#include "Moteur.h"

namespace groovebox
{

namespace
{
    template <typename TypeDePlugin>
    TypeDePlugin* ajouterPlugin (te::AudioTrack& piste, int position)
    {
        auto plugin = piste.edit.getPluginCache().createNewPlugin (TypeDePlugin::xmlTypeName, {});
        piste.pluginList.insertPlugin (plugin, position, nullptr);

        auto resultat = dynamic_cast<TypeDePlugin*> (plugin.get());
        jassert (resultat != nullptr);
        return resultat;
    }

    // Le clip du clic couvre quatre heures de jeu : une mesure qui tourne en boucle
    constexpr double dureeDuClipDuClic = 4.0 * 3600.0;
}

Moteur::Moteur (te::Engine& e, Morceau depart)  : engine (e), morceau (std::move (depart))
{
    engine.getPluginManager().createBuiltInType<BipPlugin>();
    engine.getPluginManager().createBuiltInType<ClicPlugin>();

    edit = te::Edit::createSingleTrackEdit (engine);
    edit->ensureNumberOfAudioTracks (2);

    auto pistes = te::getAudioTracks (*edit);
    pistePrincipale = pistes[0];
    pisteCasque = pistes[1];
    pistePrincipale->setName ("Musique (test sorties 1-2)");
    pisteCasque->setName ("Casque du batteur (sorties 3-4)");

    // Piste principale : le bip de test, et un envoi de toute la musique vers le casque
    bipPrincipal = ajouterPlugin<BipPlugin> (*pistePrincipale, 0);
    bipPrincipal->frequence = 440.0f;
    envoiCasque = ajouterPlugin<te::AuxSendPlugin> (*pistePrincipale, 1);

    // Piste du casque : le clic, le bip de test du casque, et le retour de la musique
    clic = ajouterPlugin<ClicPlugin> (*pisteCasque, 0);
    bipCasque = ajouterPlugin<BipPlugin> (*pisteCasque, 1);
    bipCasque->frequence = 880.0f;
    retourCasque = ajouterPlugin<te::AuxReturnPlugin> (*pisteCasque, 2);

    clipDuClic = pisteCasque->insertMIDIClip ("Clic", { te::TimePosition(), te::TimeDuration::fromSeconds (dureeDuClipDuClic) }, nullptr);

    if (auto tempo = edit->tempoSequence.getTempo (0))
        tempo->setCurve (1.0f);

    appliquerTempo();
    appliquerMesure();
    appliquerReglagesDuClic();
    appliquerRoutage();
    engine.getDeviceManager().addChangeListener (this);

    edit->getTransport().ensureContextAllocated();
}

Moteur::~Moteur()
{
    engine.getDeviceManager().removeChangeListener (this);
    edit->getTransport().stop (false, true);
}

//==============================================================================
void Moteur::modifier (const std::function<void (Morceau&)>& changement)
{
    auto nouveau = morceau;
    changement (nouveau);

    nouveau.tempo = juce::jlimit (Morceau::tempoMin, Morceau::tempoMax, nouveau.tempo);
    nouveau.clic.decompte = juce::jlimit (0, 2, nouveau.clic.decompte);
    nouveau.clic.volume = juce::jlimit (0.0f, 1.0f, nouveau.clic.volume);
    nouveau.musiqueDansCasqueDb = juce::jlimit (-40.0f, 0.0f, nouveau.musiqueDansCasqueDb);
    nouveau.volumeGeneralDb = juce::jlimit (-40.0f, 0.0f, nouveau.volumeGeneralDb);

    if (! nouveau.mesure.estValide() || enLecture())
        nouveau.mesure = morceau.mesure;

    if (sortieCasqueDisponible())
        nouveau.clic.ecouteSurMac = false;

    const auto ancien = morceau;
    morceau = nouveau;

    if (morceau.tempo != ancien.tempo)
        appliquerTempo();

    if (morceau.mesure != ancien.mesure)
        appliquerMesure();
    else if (morceau.clic.subdivisions != ancien.clic.subdivisions || morceau.clic.unClicParGroupe != ancien.clic.unClicParGroupe)
        appliquerMotifDuClic();

    appliquerReglagesDuClic();

    if (morceau.clic.ecouteSurMac != ancien.clic.ecouteSurMac)
        appliquerRoutage();

    if (surChangementDeMorceau)
        surChangementDeMorceau();
}

void Moteur::appliquerTempo()
{
    auto& tempos = edit->tempoSequence;

    if (! enLecture())
    {
        ramenerTempoAUnSeul();
        return;
    }

    // En pleine lecture, le nouveau tempo démarre juste après la position actuelle : la mesure en cours ne saute pas
    const auto maintenant = positionExacte() + te::TimeDuration::fromSeconds (0.05);
    tempos.insertTempo (tempos.toBeats (maintenant), morceau.tempo, 1.0f);
}

void Moteur::ramenerTempoAUnSeul()
{
    auto& tempos = edit->tempoSequence;

    for (int i = tempos.getNumTempos(); --i > 0;)
        tempos.removeTempo (i, false);

    if (auto tempo = tempos.getTempo (0))
    {
        tempo->setBpm (morceau.tempo);
        tempo->setCurve (1.0f);
    }
}

void Moteur::appliquerMesure()
{
    if (auto signature = edit->tempoSequence.getTimeSig (0))
        signature->setStringTimeSig (morceau.mesure.nom());

    appliquerMotifDuClic();
}

void Moteur::appliquerMotifDuClic()
{
    auto& notes = clipDuClic->getSequence();
    notes.clear (nullptr);

    for (auto& coup : morceau.mesure.motifDeClic (morceau.clic.unClicParGroupe, morceau.clic.subdivisions))
        notes.addNote (ClicPlugin::noteDe (coup.type), te::BeatPosition::fromBeats (coup.position),
                       te::BeatDuration::fromBeats (0.1), 127, 0, nullptr);

    clipDuClic->setLoopRangeBeats ({ te::BeatPosition(), te::BeatDuration::fromBeats (morceau.mesure.numerateur) });
}

void Moteur::appliquerReglagesDuClic()
{
    clic->actif = morceau.clic.actif;
    clic->volume = morceau.clic.volume;
    envoiCasque->setGainDb (morceau.musiqueDansCasqueDb);

    if (auto volume = edit->getMasterVolumePlugin())
        volume->setVolumeDb (morceau.volumeGeneralDb);
}

//==============================================================================
void Moteur::lancer()
{
    auto& transport = edit->getTransport();

    if (transport.isPlaying())
        transport.stop (false, false);

    ramenerTempoAUnSeul();

    const int mesureDeDepart = mesuresDePreparation - morceau.clic.decompte;
    const auto temps = te::BeatPosition::fromBeats ((double) mesureDeDepart * morceau.mesure.numerateur);
    transport.setPosition (edit->tempoSequence.toTime (temps));

    lectureVoulue = true;
    transport.play (false);
}

void Moteur::arreter()
{
    lectureVoulue = false;
    edit->getTransport().stop (false, false);
    ramenerTempoAUnSeul();
}

te::TimePosition Moteur::positionExacte() const
{
    // La tête de lecture elle-même : la position du transport n'est rafraîchie que 50 fois par seconde
    if (auto contexte = edit->getCurrentPlaybackContext())
        return contexte->getPosition();

    return edit->getTransport().getPosition();
}

bool Moteur::enLecture() const
{
    return edit->getTransport().isPlaying();
}

PositionMusicale Moteur::getPosition() const
{
    PositionMusicale p;
    p.enLecture = enLecture();

    if (! p.enLecture)
        return p;

    const auto temps = edit->tempoSequence.toBeats (positionExacte()).inBeats();
    const auto parMesure = (double) morceau.mesure.numerateur;
    const auto indexDeMesure = std::floor (temps / parMesure);
    const auto dansLaMesure = temps - indexDeMesure * parMesure;

    p.mesure = (int) indexDeMesure - mesuresDePreparation + 1;
    p.temps = juce::jlimit (0, morceau.mesure.numerateur - 1, (int) std::floor (dansLaMesure));
    p.fraction = dansLaMesure - std::floor (dansLaMesure);
    return p;
}

//==============================================================================
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

bool Moteur::ecouteSurMacEnCours() const
{
    return ! sortieCasqueDisponible() && morceau.clic.ecouteSurMac;
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
        // Le casque reçoit le clic et une copie de la musique
        morceau.clic.ecouteSurMac = false;
        pisteCasque->getOutput().setOutputToDeviceID (sortie34->getDeviceID());
        pisteCasque->setMute (false);
        retourCasque->setEnabled (true);
    }
    else if (morceau.clic.ecouteSurMac)
    {
        // Essai sans carte son : le clic seul sur les sorties 1-2 (la musique y est déjà)
        pisteCasque->getOutput().setOutputToDefaultDevice (false);
        pisteCasque->setMute (false);
        retourCasque->setEnabled (false);
        bipCasque->actif = false;
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

    if (lectureVoulue && ! transport.isPlaying())
    {
        juce::Logger::writeToLog ("Lecture relancée après un changement de carte son"_fr);
        transport.play (false);
    }

    if (surChangementDeCarte)
        surChangementDeCarte();
}

} // namespace groovebox
