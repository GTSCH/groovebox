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
            p.envoiDelayDb = juce::jlimit (-60.0f, 6.0f, p.envoiDelayDb);
            p.envoiReverbDb = juce::jlimit (-60.0f, 6.0f, p.envoiReverbDb);
            p.panoramique = juce::jlimit (-1.0f, 1.0f, p.panoramique);
        }

        m.assurerScenes (8);
        m.delay.retour = juce::jlimit (0.0f, 1.2f, m.delay.retour);
        m.delay.division = juce::jlimit (0, (int) DelayDub::divisions.size() - 1, m.delay.division);

        // Pistes de synthé : un son de Surge par défaut, avec ses macros de départ
        for (auto& p : m.pistes)
            if (! p.estUneBatterie() && sonsDeSynthe::trouver (p.sonDeSynthe) == nullptr)
            {
                p.sonDeSynthe = sonsDeSynthe::sonParDefaut ((int) p.son);

                if (auto son = sonsDeSynthe::trouver (p.sonDeSynthe))
                    for (int k = 0; k < 8; ++k)
                        p.macros[(size_t) k] = son->macros[(size_t) k].depart;
            }
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
    engine.getPluginManager().createBuiltInType<FiltrePlugin>();
    engine.getPluginManager().createBuiltInType<SaturationPlugin>();
    engine.getPluginManager().createBuiltInType<DelayDubPlugin>();
    engine.getPluginManager().createBuiltInType<LimiteurPlugin>();
    engine.getPluginManager().createBuiltInType<DistorsionPlugin>();
    engine.getPluginManager().createBuiltInType<PunchPlugin>();
    engine.getPluginManager().createBuiltInType<GravePlugin>();
    engine.getPluginManager().createBuiltInType<VoixPlugin>();
    engine.getPluginManager().createBuiltInType<EnregistreurDeSortiePlugin>();
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

    // Les 8 pistes : un dossier (effets, volume, panoramique, mute, solo, envois) qui contient les voix A et B.
    // Pendant un enchaînement, l'ancien clip joue sur une voix et le nouveau sur l'autre ; chaque voix finit par son
    // réglage d'enchaînement (volume, filtres), précédé de ses envois pour la queue d'écho.
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

            for (int e = 0; e < 2; ++e)
            {
                auto envoi = ajouterPlugin<te::AuxSendPlugin> (*v, -1);
                envoi->busNumber = e + 1;
                envoi->setGainDb (-100.0f);
                pm.envoisDesVoix[(size_t) k][(size_t) e] = envoi;
            }

            pm.reglagesDesVoix[(size_t) k] = ajouterPlugin<VoixPlugin> (*v, -1);
            pm.reglagesDesVoix[(size_t) k]->reposer (k == 0 ? 1.0f : 0.0f);
        }

        pm.volume = pm.dossier->getVolumePlugin();

        if (pm.volume == nullptr)
            pm.volume = ajouterPlugin<te::VolumeAndPanPlugin> (*pm.dossier, 0);

        pm.envoiCasque = ajouterPlugin<te::AuxSendPlugin> (*pm.dossier, -1);

        // Envois vers les effets de départ : bus 1 (delay dub) et bus 2 (réverbération)
        pm.envoiDelay = ajouterPlugin<te::AuxSendPlugin> (*pm.dossier, -1);
        pm.envoiDelay->busNumber = 1;
        pm.envoiReverb = ajouterPlugin<te::AuxSendPlugin> (*pm.dossier, -1);
        pm.envoiReverb->busNumber = 2;
    }

    // Retours des effets de départ : ils vont à la sono et, par le bus 0, au casque du batteur
    auto retour = [this, &precedente] (const juce::String& nom, int bus)
    {
        auto piste = edit->insertNewAudioTrack (te::TrackInsertPoint (nullptr, precedente), nullptr);
        piste->setName (nom);
        precedente = piste.get();
        auto r = ajouterPlugin<te::AuxReturnPlugin> (*piste, 0);
        r->busNumber = bus;
        return piste.get();
    };

    pisteDelay = retour ("Delay dub", 1);
    delay = ajouterPlugin<DelayDubPlugin> (*pisteDelay, 1);
    envoisCasqueDesRetours[0] = ajouterPlugin<te::AuxSendPlugin> (*pisteDelay, -1);
    pisteReverb = retour ("Réverbération"_fr, 2);
    reverb = ajouterPlugin<te::ReverbPlugin> (*pisteReverb, 1);
    envoisCasqueDesRetours[1] = ajouterPlugin<te::AuxSendPlugin> (*pisteReverb, -1);

    // Sortie principale : filtre global (bande tactile), puis compresseur léger et limiteur ; le casque a aussi son limiteur
    auto ajouterALaSortie = [this] (const char* type)
    {
        auto plugin = edit->getPluginCache().createNewPlugin (type, {});
        edit->getMasterPluginList().insertPlugin (plugin, -1, nullptr);
        return plugin.get();
    };

    filtreDeSortie = dynamic_cast<FiltrePlugin*> (ajouterALaSortie (FiltrePlugin::xmlTypeName));
    filtreDeSortie->resonance = 0.25f;
    limiteurDeSortie = dynamic_cast<LimiteurPlugin*> (ajouterALaSortie (LimiteurPlugin::xmlTypeName));
    enregistreurDeSortie = dynamic_cast<EnregistreurDeSortiePlugin*> (ajouterALaSortie (EnregistreurDeSortiePlugin::xmlTypeName));
    enregistrementAudio = std::make_unique<EnregistrementAudio> (*enregistreurDeSortie, entreeDesMicros);
    engine.getDeviceManager().deviceManager.addAudioCallback (&entreeDesMicros);
    limiteurDuCasque = ajouterPlugin<LimiteurPlugin> (*pisteCasque, -1);
    limiteurDuCasque->compression = 0.0f;

    if (auto tempo = edit->tempoSequence.getTempo (0))
        tempo->setCurve (1.0f);

    appliquerTempo();
    appliquerMesure();
    appliquerReglagesDuClic();
    appliquerPistes (morceau, true);
    appliquerCases (morceau, true);
    appliquerEffetsDeDepart();
    appliquerRoutage();
    engine.getDeviceManager().addChangeListener (this);

    edit->getTransport().ensureContextAllocated();
}

Moteur::~Moteur()
{
    arreterPrise();
    engine.getDeviceManager().deviceManager.removeAudioCallback (&entreeDesMicros);
    enregistrementAudio.reset();
    stopTimer();
    engine.getDeviceManager().removeChangeListener (this);
    edit->getTransport().stop (false, true);
}

//==============================================================================
void Moteur::modifier (const std::function<void (Morceau&)>& changement, const juce::String& geste)
{
    Appel appel (*this);
    auto nouveau = morceau;
    changement (nouveau);
    contraindre (nouveau);

    if (! nouveau.mesure.estValide() || enLecture())
        nouveau.mesure = morceau.mesure;

    // Les clips générés suivent la mesure et la gamme
    if (nouveau.mesure != morceau.mesure || ! (nouveau.gamme == morceau.gamme))
        nouveau.regenererTout();

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

    if (appel.aNoter())
        noterReglages (ancien);

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

        // Swing changé : on réécrit les clips des pistes concernées
        for (int p = 0; p < nombreDePistes; ++p)
            if (morceau.swingDeLaPiste (p) != ancien.swingDeLaPiste (p))
                for (int s = 0; s < (int) morceau.scenes.size(); ++s)
                    appliquerCase (p, s);
    }

    appliquerReglagesDuClic();
    appliquerPistes (ancien, false);
    appliquerEffetsDeDepart();

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
    appliquerEffetsDeDepart();
    appliquerRoutage();

    if (surChangementDeMorceau)
        surChangementDeMorceau();
}

bool Moteur::annuler()
{
    Appel appel (*this);

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

    if (appel.aNoter())
        noterReglages (ancien);

    if (surChangementDeMorceau)
        surChangementDeMorceau();

    return true;
}

bool Moteur::retablir()
{
    Appel appel (*this);

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

    if (appel.aNoter())
        noterReglages (ancien);

    if (surChangementDeMorceau)
        surChangementDeMorceau();

    return true;
}

//==============================================================================
double Moteur::caleDuLancement() const
{
    // À l'arrêt ou pendant le démarrage, tout part à la mesure 1
    if (enLecture() && ! demarrageEnAttente)
        if (auto instant = prochainCalage())
            return instant->temps;

    return tempsDeLaMesure1();
}

void Moteur::noter (EvenementDePrise e)
{
    if (! prise)
        return;

    e.temps = demarrageEnAttente || ! enLecture() ? tempsDuDepart : tempsActuel();
    prise->evenements.push_back (std::move (e));
}

void Moteur::noterReglages (const Morceau& avant)
{
    const auto changements = ecarts::entre (avant.versJson(), morceau.versJson());

    if (changements.isEmpty())
        return;

    EvenementDePrise e;
    e.genre = EvenementDePrise::Genre::reglages;
    e.changements = changements;
    noter (std::move (e));
}

bool Moteur::demarrerPrise()
{
    if (prise || ! dossierDesPrises)
        return false;

    const auto racine = dossierDesPrises();

    if (racine == juce::File() || ! racine.createDirectory())
        return false;

    // Un dossier par prise, à la date et à l'heure ; le nom se change ensuite
    const auto date = juce::Time::getCurrentTime();
    auto dossier = racine.getChildFile (date.formatted ("%Y-%m-%d %H-%M-%S"));

    for (int k = 2; dossier.exists(); ++k)
        dossier = racine.getChildFile (date.formatted ("%Y-%m-%d %H-%M-%S") + " (" + juce::String (k) + ")");

    if (! dossier.createDirectory())
        return false;

    int deja = 0;

    for (auto& d : racine.findChildFiles (juce::File::findDirectories, false))
        deja += d.getChildFile ("prise.json").existsAsFile() ? 1 : 0;

    Prise p;
    p.nom = "Prise " + juce::String (deja + 1);
    p.date = date;
    p.morceauDeDepart = morceau;

    if (! enLecture())
    {
        lancer();
        p.debut = tempsDeLaMesure1();
    }
    else
    {
        // La prise commence au début de la mesure en cours
        const auto parMesure = (double) morceau.mesure.numerateur;
        const auto mesure1 = tempsDeLaMesure1();
        p.debut = mesure1 + std::floor ((juce::jmax (mesure1, tempsActuel()) - mesure1) / parMesure) * parMesure;

        for (int k = 0; k < nombreDePistes; ++k)
        {
            const auto& t = enchainements[(size_t) k];
            p.scenesDeDepart[(size_t) k] = t.enCours ? t.sceneVisee() : sceneSurLaVoix (k, voixCourantes[(size_t) k]);
        }
    }

    p.avecMicros = entreeDesMicros.canauxDisponibles() > 0;

    if (! enregistrementAudio->demarrer (dossier, engine.getDeviceManager().getSampleRate(), p.avecMicros))
        juce::Logger::writeToLog ("Prise : son impossible à enregistrer dans "_fr + dossier.getFullPathName());

    p.secondesDuDebut = edit->tempoSequence.toTime (te::BeatPosition::fromBeats (p.debut)).inSeconds();
    prise = std::move (p);
    dossierDeLaPrise = dossier;
    instantDuDebutDeLaPrise = juce::Time::getMillisecondCounterHiRes();
    prise->enregistrer (dossier.getChildFile ("prise.json"));
    juce::Logger::writeToLog ("Prise commencée : "_fr + dossier.getFullPathName());
    return true;
}

std::optional<juce::File> Moteur::arreterPrise()
{
    if (! prise)
        return std::nullopt;

    prise->fin = enLecture() && ! demarrageEnAttente ? juce::jmax (prise->debut, tempsActuel()) : prise->debut;
    prise->debutDuSon = enregistreurDeSortie->premierInstant();
    enregistrementAudio->arreter();

    if (! prise->enregistrer (dossierDeLaPrise.getChildFile ("prise.json")))
        juce::Logger::writeToLog ("Prise : journal impossible à écrire dans "_fr + dossierDeLaPrise.getFullPathName());

    juce::Logger::writeToLog ("Prise terminée : "_fr + juce::String ((int) prise->evenements.size()) + " gestes, "
                              + juce::String (prise->dureeEnSecondes(), 1) + " s");
    prise.reset();
    const auto dossier = dossierDeLaPrise;

    if (surPriseTerminee)
        surPriseTerminee (dossier);

    return dossier;
}

double Moteur::dureeDeLaPrise() const
{
    return prise ? (juce::Time::getMillisecondCounterHiRes() - instantDuDebutDeLaPrise) / 1000.0 : 0.0;
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

        // Batterie, Surge XT ou synthé provisoire : un seul est actif ; les notes traversent ceux qui sont éteints
        const bool batterie = p.estUneBatterie();
        const auto* son = sonsDeSynthe::trouver (p.sonDeSynthe);

        for (int k = 0; k < 2; ++k)
        {
            bool surgePret = false;

            // Un Surge par voix, avec les mêmes réglages : celui de la voix libre se repose
            if (! batterie && son != nullptr && surge::trouver (engine))
            {
                const bool nouveau = pm.surges[(size_t) k] == nullptr;

                if (nouveau)
                {
                    pm.surges[(size_t) k] = surge::creer (*edit);

                    if (pm.surges[(size_t) k] != nullptr)
                    {
                        pm.voix[(size_t) k]->pluginList.insertPlugin (pm.surges[(size_t) k], 0, nullptr);
                        pm.pilotesSurge[(size_t) k] = std::make_unique<PiloteDeSurge> (*pm.surges[(size_t) k]);
                    }
                }

                if (auto& pilote = pm.pilotesSurge[(size_t) k])
                {
                    if (nouveau || tout || p.sonDeSynthe != a.sonDeSynthe || p.son != a.son)
                        pilote->appliquerSon (*son, p.macros);
                    else if (p.macros != a.macros)
                        pilote->appliquerMacros (*son, p.macros);

                    surgePret = true;
                }
            }

            pm.instruments[(size_t) k]->son = (int) p.son;
            pm.instruments[(size_t) k]->setEnabled (! batterie && ! surgePret);
            pm.batteries[(size_t) k]->setEnabled (batterie);

            if (batterie && (tout || ! (p.kit == a.kit) || p.son != a.son))
                pm.batteries[(size_t) k]->appliquerKit (p.kit, *banque);
        }

        appliquerSurges (i);
        appliquerEffets (i, ancien, tout);
        pm.envoiDelay->setGainDb (momentanes[(size_t) i] ? 0.0f : p.envoiDelayDb);
        pm.envoiReverb->setGainDb (p.envoiReverbDb);

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

te::MidiClip* Moteur::clipDeLaCase (int piste, int scene, int voix) const
{
    auto cases = pistesMoteur[(size_t) piste].voix[(size_t) voix]->getClipSlotList().getClipSlots();

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
    // Chaque clip existe sur les deux voix : l'enchaînement peut le lancer sur celle qui est libre
    for (int v = 0; v < 2; ++v)
        appliquerCaseSurLaVoix (piste, scene, v);
}

void Moteur::appliquerCaseSurLaVoix (int piste, int scene, int voix)
{
    const auto* modele = morceau.clip (piste, scene);
    auto* existant = clipDeLaCase (piste, scene, voix);

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
        auto cases = pistesMoteur[(size_t) piste].voix[(size_t) voix]->getClipSlotList().getClipSlots();

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

    // Swing : les double-croches du contretemps (pas impairs) sont retardées, à la lecture seulement
    const auto pasDuSwing = generateurs::pas (morceau.mesure);
    const auto swing = (double) morceau.swingDeLaPiste (piste);

    for (auto& n : modele->notes)
    {
        if (n.debut < 0.0 || n.debut >= longueur)
            continue;

        auto debut = n.debut;
        const auto indice = debut / pasDuSwing;
        const auto arrondi = std::round (indice);

        if (swing > 0.0 && std::abs (indice - arrondi) < 0.1 && ((long long) arrondi) % 2 != 0)
            debut = juce::jmin (longueur - 1.0e-3, debut + swing * pasDuSwing);

        notes.addNote (n.hauteur, te::BeatPosition::fromBeats (debut), te::BeatDuration::fromBeats (juce::jmax (0.01, juce::jmin (n.duree, longueur - debut))),
                       n.velocite, 0, nullptr);
    }

    existant->setLoopRangeBeats ({ te::BeatPosition(), te::BeatDuration::fromBeats (longueur) });
}

namespace
{
    void reglerParNom (te::Plugin& plugin, const juce::String& nom, float valeur)
    {
        for (auto* p : plugin.getAutomatableParameters())
            if (p->getParameterName() == nom)
                p->setParameter (juce::jlimit (0.0f, 1.0f, valeur), juce::sendNotificationSync);
    }

    const char* typeDuPlugin (TypeEffet t)
    {
        switch (t)
        {
            case TypeEffet::filtre:         return FiltrePlugin::xmlTypeName;
            case TypeEffet::saturation:     return SaturationPlugin::xmlTypeName;
            case TypeEffet::lofi:           return te::AirWindowsDeRez2::xmlTypeName;
            case TypeEffet::compresseur:    return te::AirWindowsButterComp2::xmlTypeName;
            case TypeEffet::echo:           return te::AirWindowsTapeDelay::xmlTypeName;
            case TypeEffet::chorus:         return te::AirWindowsChorusEnsemble::xmlTypeName;
            case TypeEffet::distorsion:     return DistorsionPlugin::xmlTypeName;
            case TypeEffet::punch:          return PunchPlugin::xmlTypeName;
            case TypeEffet::grave:          return GravePlugin::xmlTypeName;
        }

        return FiltrePlugin::xmlTypeName;
    }
}

void Moteur::appliquerEffets (int i, const Morceau& ancien, bool tout)
{
    auto& pm = pistesMoteur[(size_t) i];
    const auto& p = morceau.pistes[(size_t) i];
    juce::ignoreUnused (ancien, tout);

    for (int k = 0; k < 3; ++k)
    {
        const auto& voulu = p.effets[(size_t) k];
        auto& plugin = pm.effets[(size_t) k];

        // Type changé (ou effet retiré) : on remplace le plugin de l'emplacement
        if (! voulu || ! pm.typesDesEffets[(size_t) k] || *pm.typesDesEffets[(size_t) k] != voulu->type)
        {
            if (plugin != nullptr)
            {
                plugin->deleteFromParent();
                plugin = nullptr;
            }

            pm.typesDesEffets[(size_t) k].reset();

            if (voulu)
            {
                // Les effets se rangent dans l'ordre des emplacements, avant le volume de la piste
                int position = 0;

                for (int j = 0; j < k; ++j)
                    position += pm.effets[(size_t) j] != nullptr ? 1 : 0;

                plugin = edit->getPluginCache().createNewPlugin (typeDuPlugin (voulu->type), {});
                pm.dossier->pluginList.insertPlugin (plugin, position, nullptr);
                pm.typesDesEffets[(size_t) k] = voulu->type;
            }
        }

        if (! voulu || plugin == nullptr)
            continue;

        const auto& r = voulu->reglages;

        switch (voulu->type)
        {
            case TypeEffet::filtre:
                if (auto f = dynamic_cast<FiltrePlugin*> (plugin.get())) { f->position = r[0]; f->resonance = r[1]; f->actif = voulu->actif; }
                break;

            case TypeEffet::saturation:
                if (auto s = dynamic_cast<SaturationPlugin*> (plugin.get())) { s->quantite = r[0]; s->couleur = r[1]; s->melange = r[2]; s->actif = voulu->actif; }
                break;

            case TypeEffet::lofi:
                reglerParNom (*plugin, "Rez", 1.0f - 0.92f * r[0]);
                reglerParNom (*plugin, "Rate", 1.0f - 0.95f * r[1]);
                reglerParNom (*plugin, "Dry/Wet", r[2]);
                plugin->setEnabled (voulu->actif);
                break;

            case TypeEffet::compresseur:
                reglerParNom (*plugin, "Compress", r[0]);
                reglerParNom (*plugin, "Output", 0.5f);
                reglerParNom (*plugin, "Dry/Wet", 1.0f);
                plugin->setEnabled (voulu->actif);
                break;

            case TypeEffet::echo:
                reglerParNom (*plugin, "Dry", 1.0f);
                reglerParNom (*plugin, "Wet", r[2]);
                reglerParNom (*plugin, "Delay", 0.05f + 0.95f * r[0]);
                reglerParNom (*plugin, "Feedbak", 0.7f * r[1]);    // l'écho court reste sage ; c'est le delay dub qui s'emballe
                plugin->setEnabled (voulu->actif);
                break;

            case TypeEffet::distorsion:
                if (auto e = dynamic_cast<DistorsionPlugin*> (plugin.get())) { e->gain = r[0]; e->durete = r[1]; e->tonalite = r[2]; e->melange = r[3]; e->actif = voulu->actif; }
                break;

            case TypeEffet::punch:
                if (auto e = dynamic_cast<PunchPlugin*> (plugin.get())) { e->attaque = r[0]; e->maintien = r[1]; e->puissance = r[2]; e->actif = voulu->actif; }
                break;

            case TypeEffet::grave:
                if (auto e = dynamic_cast<GravePlugin*> (plugin.get())) { e->quantite = r[0]; e->frequenceDuGrave = r[1]; e->saturation = r[2]; e->actif = voulu->actif; }
                break;

            case TypeEffet::chorus:
                reglerParNom (*plugin, "Range", r[0]);
                reglerParNom (*plugin, "Speed", r[1]);
                reglerParNom (*plugin, "Dry/Wet", r[2]);
                plugin->setEnabled (voulu->actif);
                break;
        }
    }
}

void Moteur::appliquerEffetsDeDepart()
{
    const auto& d = morceau.delay;
    delay->tempo = (float) morceau.tempo;
    delay->noires = (float) DelayDub::divisions[(size_t) juce::jlimit (0, (int) DelayDub::divisions.size() - 1, d.division)];
    delay->retour = d.retour;
    delay->filtre = d.filtre;
    delay->saturation = d.saturation;

    // Réverbération : la taille élargit et allonge, la durée allonge, la couleur éclaircit
    const auto& r = morceau.reverb;
    reverb->roomSizeParam->setParameter (juce::jlimit (0.0f, 1.0f, 0.35f + 0.45f * r.duree + 0.18f * r.taille), juce::sendNotificationSync);
    reverb->widthParam->setParameter (0.4f + 0.6f * r.taille, juce::sendNotificationSync);
    reverb->dampParam->setParameter (0.9f - 0.85f * r.couleur, juce::sendNotificationSync);
    reverb->wetParam->setParameter (0.8f, juce::sendNotificationSync);
    reverb->dryParam->setParameter (0.0f, juce::sendNotificationSync);

    limiteurDeSortie->compression = morceau.compressionDeSortie;
}

void Moteur::envoiMomentane (int piste, bool tenu)
{
    Appel appel (*this);
    piste = juce::jlimit (0, nombreDePistes - 1, piste);

    if (appel.aNoter())
        noter ({ EvenementDePrise::Genre::envoiMomentane, 0.0, std::nullopt, piste, -1, std::nullopt, tenu ? 1.0f : 0.0f });

    momentanes[(size_t) piste] = tenu;
    pistesMoteur[(size_t) piste].envoiDelay->setGainDb (tenu ? 0.0f : morceau.pistes[(size_t) piste].envoiDelayDb);
}

void Moteur::filtreGlobal (float position)
{
    Appel appel (*this);

    if (appel.aNoter())
        noter ({ EvenementDePrise::Genre::filtreGlobal, 0.0, std::nullopt, -1, -1, std::nullopt, juce::jlimit (0.0f, 1.0f, position) });

    filtreDeSortie->position = juce::jlimit (0.0f, 1.0f, position);
}

bool Moteur::joueAvecSurge (int piste) const
{
    const auto& s = pistesMoteur[(size_t) piste].surges[(size_t) voixCourantes[(size_t) piste]];
    return s != nullptr && s->isEnabled();
}

std::vector<double> Moteur::debutsJoues (int piste, int scene) const
{
    std::vector<double> debuts;

    if (auto* c = clipDeLaCase (piste, scene))
        for (auto* n : c->getSequence().getNotes())
            debuts.push_back (n->getStartBeat().inBeats());

    std::sort (debuts.begin(), debuts.end());
    return debuts;
}

//==============================================================================
void Moteur::jouerNote (int piste, int note, int velocite)
{
    Appel appel (*this);

    if (appel.aNoter())
        noter ({ EvenementDePrise::Genre::note, 0.0, std::nullopt, piste, -1, std::nullopt, (float) note, juce::jlimit (1, 127, velocite) });

    piste = juce::jlimit (0, nombreDePistes - 1, piste);

    if (auto v = pistesMoteur[(size_t) piste].voix[(size_t) voixCourantes[(size_t) piste]])
        v->injectLiveMidiMessage (juce::MidiMessage::noteOn (1, note, (juce::uint8) juce::jlimit (1, 127, velocite)), {});
}

void Moteur::relacherNote (int piste, int note)
{
    Appel appel (*this);

    if (appel.aNoter())
        noter ({ EvenementDePrise::Genre::note, 0.0, std::nullopt, piste, -1, std::nullopt, (float) note, 0 });

    // Relâchée sur les deux voix : la voix courante a pu changer pendant que la note était tenue
    for (auto* v : pistesMoteur[(size_t) juce::jlimit (0, nombreDePistes - 1, piste)].voix)
        v->injectLiveMidiMessage (juce::MidiMessage::noteOff (1, note), {});
}

std::optional<double> Moteur::positionDansLeClip (int piste) const
{
    const int scene = sceneJouee (piste);

    if (scene < 0)
        return std::nullopt;

    auto* c = clipDeLaCase (piste, scene, voixQuiJoue (piste, scene));
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

std::optional<Moteur::Instant> Moteur::prochainCalage (std::optional<double> tempsVise) const
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

    return Instant { te::MonotonicBeat { point->monotonicBeat.v + te::BeatDuration::fromBeats (vise - maintenant) }, vise };
}

void Moteur::lancerSurLaVoix (int piste, int voix, int scene, te::MonotonicBeat quand)
{
    const int n = juce::jmax (8, (int) morceau.scenes.size());

    for (int s = 0; s < n; ++s)
    {
        auto* c = clipDeLaCase (piste, s, voix);

        if (c == nullptr)
            continue;

        auto lh = c->getLaunchHandle();

        if (s == scene)
            lh->play (quand);
        else if (estEnCours (lh))
            lh->stop (quand);
    }
}

void Moteur::arreterLaVoix (int piste, int voix, std::optional<te::MonotonicBeat> quand)
{
    const int n = juce::jmax (8, (int) morceau.scenes.size());

    for (int s = 0; s < n; ++s)
        if (auto* c = clipDeLaCase (piste, s, voix))
            if (auto lh = c->getLaunchHandle(); estEnCours (lh))
                lh->stop (quand);
}

void Moteur::arreterMaintenant (int piste, std::optional<te::MonotonicBeat> quand)
{
    for (int v = 0; v < 2; ++v)
        arreterLaVoix (piste, v, quand);
}

int Moteur::sceneSurLaVoix (int piste, int voix) const
{
    for (int s = 0; s < (int) morceau.scenes.size(); ++s)
        if (auto* c = clipDeLaCase (piste, s, voix); c != nullptr && estEnCours (c->getLaunchHandle()))
            return s;

    return -1;
}

int Moteur::voixQuiJoue (int piste, int scene) const
{
    const int courante = voixCourantes[(size_t) piste];

    for (int v : { courante, 1 - courante })
        if (auto* c = clipDeLaCase (piste, scene, v))
            if (c->getLaunchHandle()->getPlayingStatus() == te::LaunchHandle::PlayState::playing)
                return v;

    return courante;
}

void Moteur::lancerClip (int piste, int scene, std::optional<Enchainement> choisi)
{
    Appel appel (*this);

    if (appel.aNoter())
        noter ({ EvenementDePrise::Genre::scene, 0.0, caleDuLancement(), piste, scene, choisi.value_or (morceau.enchainement) });

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

    enchainer (piste, scene, choisi.value_or (morceau.enchainement));
}

void Moteur::lancerScene (int scene, std::optional<Enchainement> choisi)
{
    Appel appel (*this);

    if (scene < 0 || scene >= (int) morceau.scenes.size())
        return;

    if (appel.aNoter())
        noter ({ EvenementDePrise::Genre::scene, 0.0, caleDuLancement(), -1, scene,
                 choisi ? *choisi : morceau.scenes[(size_t) scene].entree.value_or (morceau.enchainement) });

    // Pendant un enchaînement à la main : relancer la scène préparée le termine tout seul ; une autre scène l'annule d'abord
    if (enchainementALaMain())
    {
        if (scene == sceneManuelle)
            return finirALaMainToutSeul();

        terminerALaMain (false);
    }

    const auto e = choisi ? *choisi : morceau.scenes[(size_t) scene].entree.value_or (morceau.enchainement);
    const bool joue = enLecture() && ! demarrageEnAttente;

    // Une nouvelle scène remplace la montée prévue ; relancer la scène qu'on quitte n'en fait pas
    annulerMontee();
    bool annulation = false;

    for (int p = 0; p < nombreDePistes; ++p)
        if (const auto& t = enchainements[(size_t) p]; t.enCours && t.sceneQuittee() == scene)
            annulation = true;

    if (joue && e.montee && ! annulation)
        if (auto instant = prochainCalage())
        {
            const double maintenant = tempsActuel() + margeDuFilPrincipal();
            programmerMontee (juce::jmax (maintenant, instant->temps - morceau.mesure.numerateur), instant->temps, e.intensite);
        }

    for (int p = 0; p < nombreDePistes; ++p)
        if (morceau.clip (p, scene) != nullptr        // une case vide laisse la piste continuer
             && (! joue || pisteAChanger (p, scene, e.coupeNette (morceau.pistes[(size_t) p].estUneBatterie()))))
            lancerClip (p, scene, e);
}

bool Moteur::pisteAChanger (int p, int scene, bool nette) const
{
    // Dans un enchaînement, une piste dont le clip ne change pas continue sans rien changer
    if (nette)
        return true;

    const auto& t = enchainements[(size_t) p];
    const int actuelle = t.enCours ? t.sceneVisee() : sceneSurLaVoix (p, voixCourantes[(size_t) p]);
    const auto* clipActuel = morceau.clip (p, actuelle);
    const auto* clip = morceau.clip (p, scene);
    return ! (actuelle >= 0 && clipActuel != nullptr && clip != nullptr && (actuelle == scene || *clipActuel == *clip));
}

//==============================================================================
void Moteur::preparerScene (int scene, std::optional<Enchainement> choisi)
{
    Appel appel (*this);

    if (scene < 0 || scene >= (int) morceau.scenes.size())
        return;

    if (appel.aNoter())
        noter ({ EvenementDePrise::Genre::preparation, 0.0, caleDuLancement(), -1, scene,
                 choisi ? *choisi : morceau.scenes[(size_t) scene].entree.value_or (morceau.enchainement) });

    // À l'arrêt, rien à mélanger : lancement normal
    if (! enLecture() || demarrageEnAttente)
        return lancerScene (scene, choisi);

    if (enchainementALaMain())
        terminerALaMain (false);

    annulerMontee();
    auto e = choisi ? *choisi : morceau.scenes[(size_t) scene].entree.value_or (morceau.enchainement);

    // À la main, une coupe n'a pas de sens : la bande fait un fondu
    if (e.type == TypeEnchainement::coupe)
        e.type = TypeEnchainement::fondu;

    bool prepare = false;

    for (int p = 0; p < nombreDePistes; ++p)
    {
        if (morceau.clip (p, scene) == nullptr || ! pisteAChanger (p, scene, false))
            continue;

        enchainer (p, scene, e, true);
        prepare = prepare || enchainements[(size_t) p].manuel;
    }

    positionManuelle = 0.0f;
    bandeDejaMontee = false;
    sceneManuelle = prepare ? scene : -1;
}

bool Moteur::enchainementALaMain() const
{
    return std::any_of (enchainements.begin(), enchainements.end(), [] (auto& t) { return t.enCours && t.manuel; });
}

void Moteur::melangerALaMain (float position)
{
    Appel appel (*this);

    if (! enchainementALaMain())
        return;

    if (appel.aNoter())
        noter ({ EvenementDePrise::Genre::melange, 0.0, std::nullopt, -1, -1, std::nullopt, juce::jlimit (0.0f, 1.0f, position) });

    positionManuelle = juce::jlimit (0.0f, 1.0f, position);
    bandeDejaMontee = bandeDejaMontee || positionManuelle > 0.05f;

    for (int p = 0; p < nombreDePistes; ++p)
        if (auto& t = enchainements[(size_t) p]; t.enCours && t.manuel)
        {
            t.pDebut = t.pFin = t.basculeAuMilieu ? (positionManuelle >= 0.5f ? 1.0f : 0.0f) : positionManuelle;
            programmerLesVoix (p);
        }

    if (positionManuelle >= 0.99f)
        terminerALaMain (true);
    else if (positionManuelle <= 0.01f && bandeDejaMontee)
        terminerALaMain (false);
}

void Moteur::terminerALaMain (bool versLeNouveau)
{
    const double maintenant = tempsActuel() + margeDuFilPrincipal();

    for (int p = 0; p < nombreDePistes; ++p)
    {
        auto& t = enchainements[(size_t) p];

        if (! (t.enCours && t.manuel))
            continue;

        t.manuel = false;

        if (versLeNouveau && maintenant < t.debut)
        {
            // Arrivé en haut avant que la nouvelle scène ait commencé : on finit le fondu au calage
            t.pDebut = t.basculeAuMilieu ? (positionManuelle >= 0.5f ? 1.0f : 0.0f) : positionManuelle;
            t.pFin = 1.0f;
            t.fin = t.debut;
            t.debut = maintenant;
            programmerLesVoix (p);
            continue;
        }

        t.pDebut = t.pFin = versLeNouveau ? 1.0f : 0.0f;
        programmerLesVoix (p);
        terminerEnchainement (p);
    }

    sceneManuelle = -1;
}

void Moteur::finirALaMainToutSeul()
{
    // La bande lâchée en route : le mélange finit seul, à la vitesse de la durée choisie
    const double maintenant = tempsActuel() + margeDuFilPrincipal();

    for (int p = 0; p < nombreDePistes; ++p)
        if (auto& t = enchainements[(size_t) p]; t.enCours && t.manuel)
        {
            t.manuel = false;
            const float depart = t.pDebut;
            t.debut = juce::jmax (maintenant, t.debut);
            t.fin = t.debut + t.duree * (1.0 - depart);
            t.pFin = 1.0f;
            programmerLesVoix (p);
        }

    sceneManuelle = -1;

    if (! demarrageEnAttente)
        startTimer (10);
}

void Moteur::programmerMontee (double debut, double fin, float intensite)
{
    auto enSecondes = [this] (double temps) { return edit->tempoSequence.toTime (te::BeatPosition::fromBeats (temps)).inSeconds(); };
    montee = { true, debut, fin, intensite };
    filtreDeSortie->programmerMontee (enSecondes (debut), enSecondes (fin), 0.5f + 0.45f * intensite);

    for (int p = 0; p < nombreDePistes; ++p)
        envoisReverbDeLaMontee[(size_t) p] = morceau.pistes[(size_t) p].envoiReverbDb;

    if (! demarrageEnAttente)
        startTimer (10);
}

void Moteur::annulerMontee()
{
    if (! montee.active)
        return;

    montee.active = false;
    filtreDeSortie->annulerMontee();

    for (int p = 0; p < nombreDePistes; ++p)
        pistesMoteur[(size_t) p].envoiReverb->setGainDb (morceau.pistes[(size_t) p].envoiReverbDb);
}

void Moteur::suivreLaMontee (double maintenant)
{
    if (! montee.active)
        return;

    // Après la bascule, tout retombe d'un coup : la queue de la réverbération sonne sur la nouvelle scène
    if (maintenant >= montee.fin)
        return annulerMontee();

    if (maintenant < montee.debut)
        return;

    // Les envois à la réverbération montent vers -4 dB (selon l'intensité), de plus en plus vite
    const auto x = (float) ((maintenant - montee.debut) / juce::jmax (1.0e-6, montee.fin - montee.debut));
    const float vise = -40.0f + (36.0f * montee.intensite) * x * x;

    for (int p = 0; p < nombreDePistes; ++p)
    {
        const float db = juce::jmax (morceau.pistes[(size_t) p].envoiReverbDb, vise);

        if (std::abs (db - envoisReverbDeLaMontee[(size_t) p]) > 0.25f)
        {
            envoisReverbDeLaMontee[(size_t) p] = db;
            pistesMoteur[(size_t) p].envoiReverb->setGainDb (db);
        }
    }
}

//==============================================================================
float Moteur::EnchainementDePiste::avancee (double temps) const
{
    if (fin <= debut)
        return temps >= debut ? pFin : pDebut;

    return pDebut + (pFin - pDebut) * (float) juce::jlimit (0.0, 1.0, (temps - debut) / (fin - debut));
}

double Moteur::tempsActuel() const
{
    return edit->tempoSequence.toBeats (positionExacte()).inBeats();
}

double Moteur::margeDuFilPrincipal() const
{
    // Le fil audio calcule un peu en avance : un changement décidé maintenant prend effet juste après
    return 0.03 * morceau.tempo / 60.0 * morceau.mesure.denominateur / 4.0;
}

void Moteur::enchainer (int piste, int scene, const Enchainement& e, bool aLaMain)
{
    auto instant = prochainCalage();

    if (! instant)
        return;

    auto& t = enchainements[(size_t) piste];
    const bool batterie = morceau.pistes[(size_t) piste].estUneBatterie();
    const bool nette = ! aLaMain && e.coupeNette (batterie);
    const double maintenant = tempsActuel() + margeDuFilPrincipal();

    // Relancer le clip qu'on est en train de quitter : l'enchaînement repart à l'envers
    if (t.enCours && ! aLaMain && ! nette && scene == t.sceneQuittee())
        return annulerEnchainement (piste);

    if (nette)
    {
        // Coupe : un enchaînement en cours se termine au calage, puis le nouveau clip part sur la voix courante
        if (t.enCours)
        {
            if (maintenant < t.debut)
            {
                t.debut = t.fin = instant->temps;
            }
            else
            {
                t.pDebut = t.avancee (maintenant);
                t.debut = maintenant;
                t.fin = juce::jmax (maintenant, instant->temps);
            }

            programmerLesVoix (piste);
            arreterLaVoix (piste, 1 - voixCourantes[(size_t) piste], instant->monotone);
        }

        lancerSurLaVoix (piste, voixCourantes[(size_t) piste], scene, instant->monotone);
        return;
    }

    // Le nouveau clip part sur la voix libre ; un reste d'enchaînement précédent s'y arrête au même instant
    const int sortante = voixCourantes[(size_t) piste];
    const int entrante = 1 - sortante;

    t.enCours = true;
    t.voixSortante = sortante;
    t.voixEntrante = entrante;
    t.sceneSortante = sceneSurLaVoix (piste, sortante);
    t.sceneEntrante = scene;
    t.debut = instant->temps;
    t.duree = (double) e.mesures * morceau.mesure.numerateur;
    t.fin = t.debut + t.duree;
    t.pDebut = 0.0f;
    t.pFin = 1.0f;
    t.type = e.type;
    t.intensite = e.intensite;
    t.manuel = aLaMain;
    t.basculeAuMilieu = aLaMain && batterie && e.batterieNette;

    // À la main, le nouveau part en silence ; la batterie nette change d'un coup au milieu de la bande
    if (aLaMain)
    {
        t.pFin = 0.0f;

        if (t.basculeAuMilieu)
            t.type = TypeEnchainement::fondu;
    }

    voixCourantes[(size_t) piste] = entrante;
    appliquerSurges (piste);
    reglerEnvoisDeVoix (piste, entrante, 0.0f);
    programmerLesVoix (piste);
    lancerSurLaVoix (piste, entrante, scene, instant->monotone);

    if (! demarrageEnAttente)
        startTimer (10);
}

void Moteur::annulerEnchainement (int piste)
{
    auto& t = enchainements[(size_t) piste];
    const double maintenant = tempsActuel() + margeDuFilPrincipal();

    if (maintenant < t.debut)
    {
        // Pas encore commencé : on oublie tout, le clip prévu ne part pas
        arreterLaVoix (piste, t.voixEntrante, std::nullopt);
        voixCourantes[(size_t) piste] = t.voixSortante;
        t.enCours = false;
        reposerLesVoix (piste);
        appliquerSurges (piste);
        return;
    }

    if (t.type == TypeEnchainement::queueDEcho)
    {
        // À l'envers, c'est le nouveau qui part dans l'écho pendant que l'ancien revient
        std::swap (t.voixSortante, t.voixEntrante);
        std::swap (t.sceneSortante, t.sceneEntrante);
        reglerEnvoisDeVoix (piste, t.voixEntrante, 0.0f);
        t.debut = maintenant;
        t.fin = maintenant + t.duree;
        t.pDebut = 0.0f;
        t.pFin = 1.0f;
    }
    else
    {
        // Les deux voix refont le même chemin en sens inverse, depuis là où elles en sont
        const float p = t.avancee (maintenant);
        const float cible = t.pFin > 0.5f ? 0.0f : 1.0f;
        t.debut = maintenant;
        t.fin = maintenant + t.duree * std::abs (p - cible);
        t.pDebut = p;
        t.pFin = cible;
    }

    voixCourantes[(size_t) piste] = t.voixFinale();
    programmerLesVoix (piste);
}

void Moteur::terminerEnchainement (int piste)
{
    auto& t = enchainements[(size_t) piste];
    const int finale = t.voixFinale();

    arreterLaVoix (piste, 1 - finale, std::nullopt);
    voixCourantes[(size_t) piste] = finale;
    t.enCours = false;
    reposerLesVoix (piste);
    appliquerSurges (piste);
}

void Moteur::programmerLesVoix (int piste)
{
    const auto& t = enchainements[(size_t) piste];
    auto& pm = pistesMoteur[(size_t) piste];
    auto enSecondes = [this] (double temps) { return edit->tempoSequence.toTime (te::BeatPosition::fromBeats (temps)).inSeconds(); };

    // À la main, la position de la bande vaut tout de suite (le trajet n'a pas de durée)
    TrajetDeVoix trajet { t.manuel ? 0.0 : enSecondes (t.debut), t.manuel ? 0.0 : enSecondes (t.fin), t.pDebut, t.pFin, t.type, false, t.intensite };
    pm.reglagesDesVoix[(size_t) t.voixSortante]->programmer (trajet);
    trajet.entrante = true;
    pm.reglagesDesVoix[(size_t) t.voixEntrante]->programmer (trajet);
}

void Moteur::reposerLesVoix (int piste)
{
    auto& pm = pistesMoteur[(size_t) piste];
    const int courante = voixCourantes[(size_t) piste];

    for (int v = 0; v < 2; ++v)
    {
        pm.reglagesDesVoix[(size_t) v]->reposer (v == courante ? 1.0f : 0.0f);
        reglerEnvoisDeVoix (piste, v, 0.0f);
    }
}

void Moteur::reglerEnvoisDeVoix (int piste, int voix, float niveau)
{
    auto& pm = pistesMoteur[(size_t) piste];
    const float db = niveau > 0.001f ? juce::Decibels::gainToDecibels (niveau) : -100.0f;

    for (int e = 0; e < 2; ++e)
    {
        auto& dernier = pm.niveauxDesEnvois[(size_t) voix][(size_t) e];

        if (std::abs (dernier - db) > 0.25f)
        {
            dernier = db;
            pm.envoisDesVoix[(size_t) voix][(size_t) e]->setGainDb (db);
        }
    }
}

void Moteur::appliquerSurges (int piste)
{
    // Le Surge de la voix libre se repose ; pendant un enchaînement, les deux jouent
    auto& pm = pistesMoteur[(size_t) piste];
    const auto& p = morceau.pistes[(size_t) piste];
    const bool synthe = ! p.estUneBatterie() && sonsDeSynthe::trouver (p.sonDeSynthe) != nullptr;

    for (int k = 0; k < 2; ++k)
        if (auto& s = pm.surges[(size_t) k]; s != nullptr && pm.pilotesSurge[(size_t) k] != nullptr)
            s->setEnabled (synthe && (k == voixCourantes[(size_t) piste] || enchainements[(size_t) piste].enCours));
}

void Moteur::suivreLesEnchainements()
{
    if (! enLecture())
        return;

    const double maintenant = tempsActuel();
    suivreLaMontee (maintenant);

    for (int p = 0; p < nombreDePistes; ++p)
    {
        auto& t = enchainements[(size_t) p];

        if (! t.enCours)
            continue;

        // Queue d'écho : la voix sortante part dans le delay et la réverbération dès le temps qui précède la bascule
        // (à la main : selon la bande)
        if (t.type == TypeEnchainement::queueDEcho)
        {
            const auto f = formeDeLaVoix (t.type, false, t.avancee (maintenant), t.intensite);
            reglerEnvoisDeVoix (p, t.voixSortante, t.manuel || maintenant >= t.debut - 1.0 ? f.envoi : 0.0f);
        }

        if (! t.manuel && maintenant >= t.fin + 0.05)
            terminerEnchainement (p);
    }
}

std::optional<EtatEnchainement> Moteur::enchainementDeLaPiste (int piste) const
{
    const auto& t = enchainements[(size_t) piste];

    if (! t.enCours)
        return std::nullopt;

    const double maintenant = enLecture() ? tempsActuel() : t.debut;
    EtatEnchainement e;
    e.sceneDe = t.sceneQuittee();
    e.sceneVers = t.sceneVisee();
    e.type = t.type;
    e.enAttente = maintenant < t.debut;
    e.aLaMain = t.manuel;
    e.annulation = ! t.versLeNouveau();
    // Un retour se montre comme un trajet à lui : de 0 à 1 entre l'annulation et l'arrivée
    const float p = t.avancee (maintenant);
    e.avancee = t.manuel ? positionManuelle : e.annulation ? (t.pDebut > 0.0f ? (t.pDebut - p) / t.pDebut : 1.0f) : p;
    return e;
}

std::optional<EtatEnchainement> Moteur::enchainementEnCours() const
{
    std::optional<EtatEnchainement> plusRecent;
    double debut = -1.0e9;

    for (int p = 0; p < nombreDePistes; ++p)
        if (auto e = enchainementDeLaPiste (p); e && enchainements[(size_t) p].debut > debut)
        {
            plusRecent = e;
            debut = enchainements[(size_t) p].debut;
        }

    return plusRecent;
}

void Moteur::arreterPiste (int piste)
{
    Appel appel (*this);

    if (appel.aNoter())
        noter ({ EvenementDePrise::Genre::arret, 0.0, caleDuLancement(), piste });

    if (demarrageEnAttente)
    {
        auto& attente = lancementsAuDemarrage;
        attente.erase (std::remove_if (attente.begin(), attente.end(), [piste] (auto& l) { return l.first == piste; }), attente.end());
        return;
    }

    if (enLecture())
    {
        const auto instant = prochainCalage();
        arreterMaintenant (piste, instant ? std::optional (instant->monotone) : std::nullopt);
    }
}

void Moteur::arreterTousLesClips()
{
    Appel appel (*this);

    if (appel.aNoter())
        noter ({ EvenementDePrise::Genre::arret, 0.0, caleDuLancement(), -1 });

    for (int p = 0; p < nombreDePistes; ++p)
        arreterPiste (p);
}

EtatCase Moteur::etatCase (int piste, int scene) const
{
    EtatCase e;

    if (morceau.clip (piste, scene) == nullptr)
        return e;

    for (int v = 0; v < 2; ++v)
    {
        auto* c = clipDeLaCase (piste, scene, v);

        if (c == nullptr)
            continue;

        e.clip = true;
        auto lh = c->getLaunchHandle();
        const auto attente = lh->getQueuedStatus();
        e.joue = e.joue || lh->getPlayingStatus() == te::LaunchHandle::PlayState::playing;
        e.departEnAttente = e.departEnAttente || attente == te::LaunchHandle::QueueState::playQueued;
        e.arretEnAttente = e.arretEnAttente || attente == te::LaunchHandle::QueueState::stopQueued;
    }

    e.departEnAttente = e.departEnAttente
                        || std::find (lancementsAuDemarrage.begin(), lancementsAuDemarrage.end(), std::pair { piste, scene }) != lancementsAuDemarrage.end();

    // Le clip qu'un enchaînement est en train d'effacer
    if (const auto& t = enchainements[(size_t) piste]; t.enCours && t.sceneQuittee() == scene && e.joue)
        e.arretEnAttente = true;

    return e;
}

int Moteur::sceneJouee (int piste) const
{
    // D'abord la voix courante (le clip qui reste), puis l'autre (celui qui s'efface)
    const int courante = voixCourantes[(size_t) piste];

    for (int v : { courante, 1 - courante })
        for (int s = 0; s < (int) morceau.scenes.size(); ++s)
            if (auto* c = clipDeLaCase (piste, s, v); c != nullptr && morceau.clip (piste, s) != nullptr
                                                     && c->getLaunchHandle()->getPlayingStatus() == te::LaunchHandle::PlayState::playing)
                return s;

    return -1;
}

void Moteur::avancer()
{
    if (demarrageEnAttente)
        avancerLeDemarrage();

    suivreLesEnchainements();

    if (demarrageEnAttente)
        return;

    if (montee.active || std::any_of (enchainements.begin(), enchainements.end(), [] (auto& t) { return t.enCours; }))
    {
        if (getTimerInterval() != 10)
            startTimer (10);
    }
    else
    {
        stopTimer();
    }
}

void Moteur::avancerLeDemarrage()
{
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
            lancerSurLaVoix (piste, voixCourantes[(size_t) piste], scene, quand->monotone);

    lancementsAuDemarrage.clear();
    demarrageEnAttente = false;
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

    if (delay != nullptr)
        delay->tempo = (float) morceau.tempo;

    // Les enchaînements sont comptés en temps : leurs instants en secondes changent avec le tempo
    for (int p = 0; p < nombreDePistes; ++p)
        if (enchainements[(size_t) p].enCours)
            programmerLesVoix (p);
}

void Moteur::ramenerTempoAUnSeul()
{
    if (delay != nullptr)
        delay->tempo = (float) morceau.tempo;

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

    for (auto* e : envoisCasqueDesRetours)
        if (e != nullptr)
            e->setGainDb (morceau.musiqueDansCasqueDb);

    if (auto volume = edit->getMasterVolumePlugin())
        volume->setVolumeDb (morceau.volumeGeneralDb);
}

//==============================================================================
void Moteur::lancer()
{
    auto& transport = edit->getTransport();

    if (transport.isPlaying())
    {
        annulerMontee();
        sceneManuelle = -1;

        for (int p = 0; p < nombreDePistes; ++p)
        {
            arreterMaintenant (p, std::nullopt);
            enchainements[(size_t) p].enCours = false;
            voixCourantes[(size_t) p] = 0;
            reposerLesVoix (p);
            appliquerSurges (p);
        }

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
    arreterPrise();     // la prise s'arrête avec la lecture
    lectureVoulue = false;
    demarrageEnAttente = false;
    lancementsAuDemarrage.clear();
    stopTimer();

    annulerMontee();
    sceneManuelle = -1;

    for (int p = 0; p < nombreDePistes; ++p)
    {
        arreterMaintenant (p, std::nullopt);
        enchainements[(size_t) p].enCours = false;
        voixCourantes[(size_t) p] = 0;
        reposerLesVoix (p);
        appliquerSurges (p);
    }

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
