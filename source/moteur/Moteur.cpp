#include "Moteur.h"
#include "FabriqueDeSons.h"

namespace groovebox
{

namespace
{
    template <typename TypeDePlugin>
    TypeDePlugin* ajouterPlugin (te::Track& piste, int position)
    {
        auto plugin = piste.edit.getPluginCache().createNewPlugin (TypeDePlugin::xmlTypeName, {});
        piste.pluginList.insertPlugin (plugin, position, nullptr);

        auto resultat = dynamic_cast<TypeDePlugin*> (plugin.get());
        jassert (resultat != nullptr);
        return resultat;
    }

    // Le clip du clic couvre quatre heures de jeu : une mesure qui tourne en boucle
    constexpr double dureeDuClipDuClic = 4.0 * 3600.0;
    constexpr size_t tailleMaximaleDeLHistorique = 1000;

    bool estEnCours (const std::shared_ptr<te::LaunchHandle>& lh)
    {
        return lh != nullptr && (lh->getPlayingStatus() == te::LaunchHandle::PlayState::playing
                                 || lh->getQueuedStatus() == te::LaunchHandle::QueueState::playQueued);
    }

    void contraindre (Morceau& m)
    {
        m.tempo = juce::jlimit (Morceau::tempoMin, Morceau::tempoMax, m.tempo);
        m.clic.decompte = juce::jlimit (0, 2, m.clic.decompte);
        m.clic.volume = juce::jlimit (0.0f, 1.0f, m.clic.volume);
        m.musiqueDansCasqueDb = juce::jlimit (-60.0f, 0.0f, m.musiqueDansCasqueDb);
        m.volumeGeneralDb = juce::jlimit (-40.0f, 0.0f, m.volumeGeneralDb);

        for (auto& p : m.pistes)
        {
            p.volumeDb = juce::jlimit (-60.0f, 6.0f, p.volumeDb);
            p.panoramique = juce::jlimit (-1.0f, 1.0f, p.panoramique);
        }

        m.assurerScenes (8);
    }
}

Moteur::Moteur (te::Engine& e, Morceau depart, BanqueDeSons* banqueFournie)  : engine (e), banque (banqueFournie), morceau (std::move (depart))
{
    if (banque == nullptr)
    {
        banquePropre = std::make_unique<BanqueDeSons>();
        banque = banquePropre.get();

        if (const int n = fabriqueDeSons::assurerKitsDeDepart (banque->getRacine()); n > 0)
            juce::Logger::writeToLog ("Kits de départ fabriqués : "_fr + juce::String (n) + " sons dans " + banque->getRacine().getFullPathName());
    }

    engine.getPluginManager().createBuiltInType<BipPlugin>();
    engine.getPluginManager().createBuiltInType<ClicPlugin>();
    engine.getPluginManager().createBuiltInType<InstrumentProvisoire>();
    engine.getPluginManager().createBuiltInType<BatteriePlugin>();
    contraindre (morceau);

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

    // Les 8 pistes : un dossier (volume, panoramique, mute, solo, envoi au casque) qui contient les voix A et B.
    // La voix B sert aux enchaînements (étape 8) : à l'étape 4, elle a son instrument mais ne joue jamais.
    te::Track* precedente = pisteCasque;

    for (int i = 0; i < nombreDePistes; ++i)
    {
        auto& pm = pistesMoteur[(size_t) i];
        auto dossier = edit->insertNewFolderTrack (te::TrackInsertPoint (nullptr, precedente), nullptr, true);
        pm.dossier = dossier.get();
        precedente = pm.dossier;

        for (int k = 0; k < 2; ++k)
        {
            auto v = edit->insertNewAudioTrack (te::TrackInsertPoint (pm.dossier, k == 0 ? nullptr : pm.voix[0]), nullptr);
            pm.voix[(size_t) k] = v.get();
            v->setName ("Piste " + juce::String (i + 1) + (k == 0 ? ", voix A" : ", voix B"));
            v->playSlotClips = true;
            pm.instruments[(size_t) k] = ajouterPlugin<InstrumentProvisoire> (*v, 0);
            pm.batteries[(size_t) k] = ajouterPlugin<BatteriePlugin> (*v, 1);
        }

        pm.volume = pm.dossier->getVolumePlugin();

        if (pm.volume == nullptr)
            pm.volume = ajouterPlugin<te::VolumeAndPanPlugin> (*pm.dossier, 0);

        pm.envoiCasque = ajouterPlugin<te::AuxSendPlugin> (*pm.dossier, -1);
    }

    if (auto tempo = edit->tempoSequence.getTempo (0))
        tempo->setCurve (1.0f);

    appliquerTempo();
    appliquerMesure();
    appliquerReglagesDuClic();
    appliquerPistes (morceau, true);
    appliquerCases (morceau, true);
    appliquerRoutage();
    engine.getDeviceManager().addChangeListener (this);

    edit->getTransport().ensureContextAllocated();
}

Moteur::~Moteur()
{
    stopTimer();
    engine.getDeviceManager().removeChangeListener (this);
    edit->getTransport().stop (false, true);
}

//==============================================================================
void Moteur::modifier (const std::function<void (Morceau&)>& changement, const juce::String& geste)
{
    auto nouveau = morceau;
    changement (nouveau);
    contraindre (nouveau);

    if (! nouveau.mesure.estValide() || enLecture())
        nouveau.mesure = morceau.mesure;

    if (sortieCasqueDisponible())
        nouveau.clic.ecouteSurMac = false;

    if (nouveau == morceau)
        return;

    // Un même geste continu (un encodeur qu'on tourne) ne fait qu'une étape d'annulation
    const auto maintenant = juce::Time::getMillisecondCounterHiRes();
    const bool memeGeste = geste.isNotEmpty() && geste == dernierGeste && maintenant - instantDuDernierGeste < 1000.0;

    if (! memeGeste)
    {
        historique.push_back (morceau);

        if (historique.size() > tailleMaximaleDeLHistorique)
            historique.erase (historique.begin());
    }

    dernierGeste = geste;
    instantDuDernierGeste = maintenant;
    futur.clear();

    const auto ancien = morceau;
    morceau = nouveau;
    appliquerDifferences (ancien);

    if (surChangementDeMorceau)
        surChangementDeMorceau();
}

void Moteur::appliquerDifferences (const Morceau& ancien)
{
    if (morceau.tempo != ancien.tempo)
        appliquerTempo();

    if (morceau.mesure != ancien.mesure)
    {
        appliquerMesure();
        appliquerCases (ancien, true);   // la longueur des clips en temps dépend de la mesure
    }
    else
    {
        if (morceau.clic.subdivisions != ancien.clic.subdivisions || morceau.clic.unClicParGroupe != ancien.clic.unClicParGroupe)
            appliquerMotifDuClic();

        appliquerCases (ancien, false);
    }

    appliquerReglagesDuClic();
    appliquerPistes (ancien, false);

    if (morceau.clic.ecouteSurMac != ancien.clic.ecouteSurMac)
        appliquerRoutage();
}

void Moteur::chargerMorceau (const Morceau& nouveau)
{
    arreter();

    const auto ancien = morceau;
    morceau = nouveau;
    contraindre (morceau);
    historique.clear();
    futur.clear();
    dernierGeste = {};

    appliquerTempo();
    appliquerMesure();
    appliquerReglagesDuClic();
    appliquerPistes (ancien, true);
    appliquerCases (ancien, true);
    appliquerRoutage();

    if (surChangementDeMorceau)
        surChangementDeMorceau();
}

bool Moteur::annuler()
{
    if (historique.empty())
        return false;

    const auto ancien = morceau;
    futur.push_back (morceau);
    morceau = historique.back();
    historique.pop_back();

    if (enLecture())
        morceau.mesure = ancien.mesure;     // la mesure ne change jamais pendant la lecture

    dernierGeste = {};
    appliquerDifferences (ancien);

    if (surChangementDeMorceau)
        surChangementDeMorceau();

    return true;
}

bool Moteur::retablir()
{
    if (futur.empty())
        return false;

    const auto ancien = morceau;
    historique.push_back (morceau);
    morceau = futur.back();
    futur.pop_back();

    if (enLecture())
        morceau.mesure = ancien.mesure;

    dernierGeste = {};
    appliquerDifferences (ancien);

    if (surChangementDeMorceau)
        surChangementDeMorceau();

    return true;
}

//==============================================================================
void Moteur::appliquerPistes (const Morceau& ancien, bool tout)
{
    for (int i = 0; i < nombreDePistes; ++i)
    {
        const auto& p = morceau.pistes[(size_t) i];
        const auto& a = ancien.pistes[(size_t) i];
        auto& pm = pistesMoteur[(size_t) i];

        if (! tout && p == a)
            continue;

        pm.dossier->setName (p.nom);

        // Batterie ou synthé provisoire : un seul des deux est actif ; les notes traversent celui qui est éteint
        const bool batterie = p.estUneBatterie();

        for (int k = 0; k < 2; ++k)
        {
            pm.instruments[(size_t) k]->son = (int) p.son;
            pm.instruments[(size_t) k]->setEnabled (! batterie);
            pm.batteries[(size_t) k]->setEnabled (batterie);

            if (batterie && (tout || ! (p.kit == a.kit) || p.son != a.son))
                pm.batteries[(size_t) k]->appliquerKit (p.kit, *banque);
        }

        pm.volume->setVolumeDb (p.volumeDb);
        pm.volume->setPan (p.panoramique);
        pm.dossier->setMute (p.mute);
        pm.dossier->setSolo (p.solo);
    }
}

void Moteur::assurerCases()
{
    const int n = juce::jmax (8, (int) morceau.scenes.size());
    edit->getSceneList().ensureNumberOfScenes (n);

    for (auto& pm : pistesMoteur)
        for (auto* v : pm.voix)
            v->getClipSlotList().ensureNumberOfSlots (n);
}

te::MidiClip* Moteur::clipDeLaCase (int piste, int scene) const
{
    auto cases = pistesMoteur[(size_t) piste].voix[0]->getClipSlotList().getClipSlots();

    if (scene < 0 || scene >= cases.size() || cases[scene] == nullptr)
        return nullptr;

    return dynamic_cast<te::MidiClip*> (cases[scene]->getClip());
}

void Moteur::appliquerCases (const Morceau& ancien, bool tout)
{
    assurerCases();
    const int n = juce::jmax ((int) morceau.scenes.size(), (int) ancien.scenes.size());

    for (int s = 0; s < n; ++s)
        for (int p = 0; p < nombreDePistes; ++p)
        {
            const auto* avant = ancien.clip (p, s);
            const auto* apres = morceau.clip (p, s);
            const bool change = (avant == nullptr) != (apres == nullptr) || (avant != nullptr && ! (*avant == *apres));

            if (tout || change)
                appliquerCase (p, s);
        }
}

void Moteur::appliquerCase (int piste, int scene)
{
    const auto* modele = morceau.clip (piste, scene);
    auto* existant = clipDeLaCase (piste, scene);

    if (modele == nullptr)
    {
        if (existant != nullptr)
        {
            if (auto lh = existant->getLaunchHandle())
                lh->stop ({});

            existant->removeFromParent();
        }

        return;
    }

    if (existant == nullptr)
    {
        auto cases = pistesMoteur[(size_t) piste].voix[0]->getClipSlotList().getClipSlots();

        if (scene >= cases.size())
            return;

        existant = te::insertMIDIClip (*cases[scene], modele->nom, { te::TimePosition(), te::TimeDuration::fromSeconds (1.0) }).get();

        if (existant == nullptr)
            return;
    }

    // On met à jour le clip sur place : s'il joue, il continue
    const auto longueur = (double) modele->longueurMesures * morceau.mesure.numerateur;
    existant->setName (modele->nom);
    existant->setLength (edit->tempoSequence.toTime (te::BeatPosition::fromBeats (longueur)) - te::TimePosition(), false);

    auto& notes = existant->getSequence();
    notes.clear (nullptr);

    for (auto& n : modele->notes)
        if (n.debut >= 0.0 && n.debut < longueur)
            notes.addNote (n.hauteur, te::BeatPosition::fromBeats (n.debut), te::BeatDuration::fromBeats (juce::jmin (n.duree, longueur - n.debut)),
                           n.velocite, 0, nullptr);

    existant->setLoopRangeBeats ({ te::BeatPosition(), te::BeatDuration::fromBeats (longueur) });
}

//==============================================================================
void Moteur::jouerNote (int piste, int note, int velocite)
{
    if (auto v = pistesMoteur[(size_t) juce::jlimit (0, nombreDePistes - 1, piste)].voix[0])
        v->injectLiveMidiMessage (juce::MidiMessage::noteOn (1, note, (juce::uint8) juce::jlimit (1, 127, velocite)), {});
}

void Moteur::relacherNote (int piste, int note)
{
    if (auto v = pistesMoteur[(size_t) juce::jlimit (0, nombreDePistes - 1, piste)].voix[0])
        v->injectLiveMidiMessage (juce::MidiMessage::noteOff (1, note), {});
}

std::optional<double> Moteur::positionDansLeClip (int piste) const
{
    const int scene = sceneJouee (piste);

    if (scene < 0)
        return std::nullopt;

    auto* c = clipDeLaCase (piste, scene);
    const auto* modele = morceau.clip (piste, scene);

    if (c == nullptr || modele == nullptr)
        return std::nullopt;

    const auto joue = c->getLaunchHandle()->getPlayedRange();

    if (! joue)
        return std::nullopt;

    const auto longueur = (double) modele->longueurMesures * morceau.mesure.numerateur;
    return std::fmod (joue->getLength().inBeats(), longueur);
}

//==============================================================================
double Moteur::tempsDeLaMesure1() const
{
    return (double) mesuresDePreparation * morceau.mesure.numerateur;
}

double Moteur::longueurDuCalage() const
{
    const auto parMesure = (double) morceau.mesure.numerateur;

    switch (morceau.calage)
    {
        case Calage::temps:         return 1.0;
        case Calage::mesure:        return parMesure;
        case Calage::deuxMesures:   return 2.0 * parMesure;
        case Calage::quatreMesures: return 4.0 * parMesure;
    }

    return parMesure;
}

std::optional<te::MonotonicBeat> Moteur::prochainCalage (std::optional<double> tempsVise) const
{
    auto contexte = edit->getCurrentPlaybackContext();

    if (contexte == nullptr)
        return std::nullopt;

    const auto point = contexte->getSyncPoint();

    if (! point)
        return std::nullopt;

    const auto maintenant = point->beat.inBeats();
    const auto mesure1 = tempsDeLaMesure1();
    double vise;

    if (tempsVise)
    {
        vise = *tempsVise;
    }
    else if (maintenant < mesure1)
    {
        vise = mesure1;     // pendant le décompte : tout part à la mesure 1
    }
    else
    {
        // Prochaine limite du calage, comptée depuis la mesure 1 (le décompte n'est pas une mesure du morceau)
        const auto longueur = longueurDuCalage();
        vise = mesure1 + (std::floor ((maintenant - mesure1) / longueur + 1.0e-9) + 1.0) * longueur;
    }

    return te::MonotonicBeat { point->monotonicBeat.v + te::BeatDuration::fromBeats (vise - maintenant) };
}

void Moteur::lancerMaintenant (int piste, int scene, te::MonotonicBeat quand)
{
    const int n = juce::jmax (8, (int) morceau.scenes.size());

    for (int s = 0; s < n; ++s)
    {
        auto* c = clipDeLaCase (piste, s);

        if (c == nullptr)
            continue;

        auto lh = c->getLaunchHandle();

        if (s == scene)
            lh->play (quand);
        else if (estEnCours (lh))
            lh->stop (quand);
    }
}

void Moteur::arreterMaintenant (int piste, std::optional<te::MonotonicBeat> quand)
{
    const int n = juce::jmax (8, (int) morceau.scenes.size());

    for (int s = 0; s < n; ++s)
        if (auto* c = clipDeLaCase (piste, s))
            if (auto lh = c->getLaunchHandle(); estEnCours (lh))
                lh->stop (quand);
}

void Moteur::lancerClip (int piste, int scene)
{
    if (morceau.clip (piste, scene) == nullptr)
    {
        arreterPiste (piste);   // une case vide arrête la piste
        return;
    }

    if (demarrageEnAttente || ! enLecture())
    {
        auto& attente = lancementsAuDemarrage;
        attente.erase (std::remove_if (attente.begin(), attente.end(), [piste] (auto& l) { return l.first == piste; }), attente.end());
        attente.push_back ({ piste, scene });

        if (! demarrageEnAttente)
            lancer();

        return;
    }

    if (auto quand = prochainCalage())
        lancerMaintenant (piste, scene, *quand);
}

void Moteur::lancerScene (int scene)
{
    for (int p = 0; p < nombreDePistes; ++p)
        if (morceau.clip (p, scene) != nullptr)
            lancerClip (p, scene);      // une case vide laisse la piste continuer
}

void Moteur::arreterPiste (int piste)
{
    if (demarrageEnAttente)
    {
        auto& attente = lancementsAuDemarrage;
        attente.erase (std::remove_if (attente.begin(), attente.end(), [piste] (auto& l) { return l.first == piste; }), attente.end());
        return;
    }

    if (enLecture())
        arreterMaintenant (piste, prochainCalage());
}

void Moteur::arreterTousLesClips()
{
    for (int p = 0; p < nombreDePistes; ++p)
        arreterPiste (p);
}

EtatCase Moteur::etatCase (int piste, int scene) const
{
    EtatCase e;
    auto* c = clipDeLaCase (piste, scene);

    if (c == nullptr || morceau.clip (piste, scene) == nullptr)
        return e;

    e.clip = true;
    auto lh = c->getLaunchHandle();
    const auto attente = lh->getQueuedStatus();
    e.joue = lh->getPlayingStatus() == te::LaunchHandle::PlayState::playing;
    e.departEnAttente = attente == te::LaunchHandle::QueueState::playQueued
                        || std::find (lancementsAuDemarrage.begin(), lancementsAuDemarrage.end(), std::pair { piste, scene }) != lancementsAuDemarrage.end();
    e.arretEnAttente = attente == te::LaunchHandle::QueueState::stopQueued;
    return e;
}

int Moteur::sceneJouee (int piste) const
{
    for (int s = 0; s < (int) morceau.scenes.size(); ++s)
        if (etatCase (piste, s).joue)
            return s;

    return -1;
}

void Moteur::avancer()
{
    if (! demarrageEnAttente)
    {
        stopTimer();
        return;
    }

    auto contexte = edit->getCurrentPlaybackContext();
    const auto point = contexte != nullptr ? contexte->getSyncPoint() : std::nullopt;

    if (! edit->getTransport().isPlaying() || ! point)
        return;

    // On attend que le fil audio joue depuis la position de départ (avant, le point de synchro date de l'ancienne lecture)
    const auto maintenant = point->beat.inBeats();
    const bool positionPrise = maintenant >= tempsDuDepart - 1.0e-3 && maintenant < tempsDeLaMesure1() + morceau.mesure.numerateur;

    if (! positionPrise && juce::Time::getMillisecondCounterHiRes() - instantDuDemarrage < 300.0)
        return;

    if (! positionPrise)
        juce::Logger::writeToLog ("Départ sans position confirmée : clips calés sur le calage suivant"_fr);

    // Pendant le décompte, prochainCalage() vise la mesure 1
    for (auto [piste, scene] : lancementsAuDemarrage)
        if (auto quand = prochainCalage())
            lancerMaintenant (piste, scene, *quand);

    lancementsAuDemarrage.clear();
    demarrageEnAttente = false;
    stopTimer();
}

void Moteur::appliquerTempo()
{
    auto& tempos = edit->tempoSequence;

    if (! edit->getTransport().isPlaying())
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

    for (auto& pm : pistesMoteur)
        if (pm.envoiCasque != nullptr)
            pm.envoiCasque->setGainDb (morceau.musiqueDansCasqueDb);

    if (auto volume = edit->getMasterVolumePlugin())
        volume->setVolumeDb (morceau.volumeGeneralDb);
}

//==============================================================================
void Moteur::lancer()
{
    auto& transport = edit->getTransport();

    if (transport.isPlaying())
    {
        for (int p = 0; p < nombreDePistes; ++p)
            arreterMaintenant (p, std::nullopt);

        transport.stop (false, false);
    }

    ramenerTempoAUnSeul();

    // Départ au début du décompte ; sans décompte, 80 ms avant la mesure 1, le temps de caler les clips
    const auto parSeconde = morceau.tempo / 60.0 * morceau.mesure.denominateur / 4.0;
    tempsDuDepart = morceau.clic.decompte > 0
                      ? (double) (mesuresDePreparation - morceau.clic.decompte) * morceau.mesure.numerateur
                      : tempsDeLaMesure1() - 0.08 * parSeconde;

    transport.setPosition (edit->tempoSequence.toTime (te::BeatPosition::fromBeats (tempsDuDepart)));
    transport.play (false);

    lectureVoulue = true;
    demarrageEnAttente = true;
    instantDuDemarrage = juce::Time::getMillisecondCounterHiRes();
    startTimer (2);
}

void Moteur::arreter()
{
    lectureVoulue = false;
    demarrageEnAttente = false;
    lancementsAuDemarrage.clear();
    stopTimer();

    for (int p = 0; p < nombreDePistes; ++p)
        arreterMaintenant (p, std::nullopt);

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
