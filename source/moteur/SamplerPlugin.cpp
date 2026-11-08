#include "SamplerPlugin.h"

namespace groovebox
{

const char* SamplerPlugin::xmlTypeName = "groovebox_sampler";

SamplerPlugin::SamplerPlugin (te::PluginCreationInfo info)  : te::Plugin (info) {}

SamplerPlugin::~SamplerPlugin()
{
    notifyListenersOfDeletion();
}

void SamplerPlugin::initialise (const te::PluginInitialisationInfo& info)
{
    frequenceEchantillonnage = info.sampleRate;

    for (auto& v : voix)
        v.active = false;

    bas1 = {}; bas2 = {}; bande1 = {};
}

void SamplerPlugin::appliquer (const ReglagesSampler& r, const Echantillon* nouveau, double secondes)
{
    const auto [d, f] = r.partieJouee();
    son = nouveau;
    mode = (int) r.mode;
    debut = d;
    fin = f;
    vitesse = std::pow (2.0f, r.hauteur / 12.0f);
    filtre = r.filtre;
    attaque = (float) r.attaqueEnSecondes();
    relache = (float) r.relacheEnSecondes();
    gain = juce::Decibels::decibelsToGain (r.volumeDb);
    envers = r.envers;
    secondesDeBoucle = juce::jmax (0.05, secondes);
}

void SamplerPlugin::demarrer (int note, float velocite)
{
    const auto* s = son.load();

    if (s == nullptr || s->audio.getNumSamples() < 2)
        return;

    const auto m = (ModeSampler) mode.load();
    const auto fe = frequenceEchantillonnage;
    const double n = (double) s->audio.getNumSamples();
    double d0 = debut.load() * n, longueur = (fin.load() - debut.load()) * n;
    const auto* lu = s;
    double pas = vitesse.load() * s->frequence / fe;
    const auto fonduRapide = (float) (1.0 / (0.005 * fe));

    switch (m)
    {
        case ModeSampler::decoupe:
        {
            const int tranche = note - ReglagesSampler::premiereNote;

            if (tranche < 0 || tranche >= ReglagesSampler::nombreDeTranches)
                return;

            longueur /= ReglagesSampler::nombreDeTranches;
            d0 += tranche * longueur;

            // Même tranche rejouée : l'ancienne s'efface, comme un pad de batterie
            for (auto& v : voix)
                if (v.active && v.note == note)
                    v.pasDeFondu = fonduRapide;
            break;
        }

        case ModeSampler::unCoup:
        case ModeSampler::notes:
            pas *= std::pow (2.0, (note - ReglagesSampler::noteDeBase) / 12.0);
            break;

        case ModeSampler::boucle:
        {
            // Une seule boucle à la fois : la précédente s'efface en 5 ms ; la boucle étirée si elle est prête
            for (auto& v : voix)
                if (v.active)
                    v.pasDeFondu = fonduRapide;

            if (const auto* e = boucleEtiree.load(); e != nullptr && e->audio.getNumSamples() > 1)
            {
                lu = e;
                d0 = 0.0;
                longueur = (double) e->audio.getNumSamples();
            }

            pas = longueur / (secondesDeBoucle.load() * fe);
            break;
        }
    }

    if (longueur < 2.0)
        return;

    // Voix libre, sinon celle qui est la plus avancée dans sa relâche (ou la plus faible)
    Voix* cible = nullptr;

    for (auto& v : voix)
        if (! v.active) { cible = &v; break; }

    if (cible == nullptr)
    {
        cible = &voix[0];

        for (auto& v : voix)
            if ((v.relachee && ! cible->relachee) || (v.relachee == cible->relachee && v.niveau * v.fondu < cible->niveau * cible->fondu))
                cible = &v;
    }

    auto& v = *cible;
    v = Voix {};
    v.active = true;
    v.boucle = m == ModeSampler::boucle;
    v.note = note;
    v.son = lu;
    v.debut = d0;
    v.longueur = longueur;
    v.pas = pas;
    v.gain = std::pow (juce::jlimit (0.0f, 1.0f, velocite), 1.4f);
    v.pasDAttaque = (float) (1.0 / (juce::jmax (0.001, (double) attaque.load()) * fe));   // 1 ms au moins : pas de clic au départ
    v.facteurDeRelache = (float) std::exp (std::log (0.001) / (juce::jmax (0.005, (double) relache.load()) * fe));
}

void SamplerPlugin::relacher (int note)
{
    const auto m = (ModeSampler) mode.load();

    // Un coup et découpé : le son va jusqu'au bout, comme un pad
    if (m == ModeSampler::unCoup || m == ModeSampler::decoupe)
        return;

    for (auto& v : voix)
        if (v.active && v.note == note)
            v.relachee = true;
}

void SamplerPlugin::toutRelacher()
{
    for (auto& v : voix)
        if (v.active)
        {
            if (v.boucle || (ModeSampler) mode.load() == ModeSampler::notes)
                v.relachee = true;
        }
}

float SamplerPlugin::lire (const Voix& v, int canal) const
{
    const auto& audio = v.son->audio;
    const int c = juce::jmin (canal, audio.getNumChannels() - 1);
    const double dansLaPartie = envers.load() ? v.longueur - 1.0 - v.position : v.position;
    const double p = v.debut + juce::jlimit (0.0, v.longueur - 1.0, dansLaPartie);
    const int i = (int) p;
    const auto* d = audio.getReadPointer (c);

    if (i + 1 >= audio.getNumSamples())
        return d[audio.getNumSamples() - 1];

    const auto fraction = (float) (p - i);
    return d[i] + fraction * (d[i + 1] - d[i]);
}

void SamplerPlugin::rendre (juce::AudioBuffer<float>& sortie, int debutDuBloc, int nombre)
{
    const auto fe = (float) frequenceEchantillonnage;
    const int canaux = juce::jmin (2, sortie.getNumChannels());
    const auto g = gain.load() * 0.7f;
    const auto f = filtre.load();
    const auto finDouce = 0.003 * frequenceEchantillonnage;    // 3 ms de fondu avant la fin d'un son qui ne boucle pas

    // Filtre à variable d'état (passe-bas vers -1, passe-haut vers +1), coefficients du bloc
    const bool filtrer = std::abs (f) > 0.01f;
    const float coupure = f < 0.0f ? 20000.0f * std::pow (80.0f / 20000.0f, -f) : 20.0f * std::pow (6000.0f / 20.0f, f);
    const float gc = std::tan (juce::MathConstants<float>::pi * juce::jmin (coupure, 0.45f * fe) / fe);
    const float k = 1.2f;   // un peu de résonance
    const float a1 = 1.0f / (1.0f + gc * (gc + k)), a2 = gc * a1, a3 = gc * a2;

    for (int i = debutDuBloc; i < debutDuBloc + nombre; ++i)
    {
        std::array<float, 2> x {};

        for (auto& v : voix)
        {
            if (! v.active)
                continue;

            if (v.position >= v.longueur - 1.0)
            {
                if (! v.boucle)
                {
                    v.active = false;
                    continue;
                }

                v.position = std::fmod (v.position, v.longueur - 1.0);
            }

            // Enveloppe : attaque, puis relâche une fois la note lâchée
            if (v.relachee)
            {
                v.niveau *= v.facteurDeRelache;

                if (v.niveau < 1.0e-4f)
                {
                    v.active = false;
                    continue;
                }
            }
            else if (v.niveau < 1.0f)
            {
                v.niveau = juce::jmin (1.0f, v.niveau + v.pasDAttaque);
            }

            float niveau = v.niveau * v.gain * v.fondu;

            if (! v.boucle && v.longueur - v.position < finDouce * v.pas)
                niveau *= (float) ((v.longueur - v.position) / (finDouce * v.pas));

            for (int c = 0; c < 2; ++c)
                x[(size_t) c] += lire (v, c) * niveau;

            if (v.pasDeFondu > 0.0f)
            {
                v.fondu -= v.pasDeFondu;

                if (v.fondu <= 0.0f)
                    v.active = false;
            }

            v.position += v.pas;
        }

        for (int c = 0; c < 2; ++c)
        {
            auto y = x[(size_t) c];

            if (filtrer)
            {
                // Filtre TPT de Zavalishin
                const float v3 = y - bas2[(size_t) c];
                const float v1 = a1 * bande1[(size_t) c] + a2 * v3;
                const float v2 = bas2[(size_t) c] + a2 * bande1[(size_t) c] + a3 * v3;
                bande1[(size_t) c] = 2.0f * v1 - bande1[(size_t) c];
                bas2[(size_t) c] = 2.0f * v2 - bas2[(size_t) c];
                y = f < 0.0f ? v2 : y - k * v1 - v2;
            }
            else
            {
                bande1[(size_t) c] = 0.0f;
                bas2[(size_t) c] = 0.0f;
            }

            x[(size_t) c] = y * g;
        }

        if (canaux == 2)
        {
            sortie.addSample (0, i, x[0]);
            sortie.addSample (1, i, x[1]);
        }
        else if (canaux == 1)
        {
            sortie.addSample (0, i, 0.5f * (x[0] + x[1]));
        }
    }
}

void SamplerPlugin::applyToBuffer (const te::PluginRenderContext& fc)
{
    if (fc.destBuffer == nullptr)
        return;

    auto& sortie = *fc.destBuffer;
    const int debutDuBloc = fc.bufferStartSample;
    const int n = fc.bufferNumSamples;
    int position = 0;

    if (fc.bufferForMidiMessages != nullptr)
    {
        for (auto& m : *fc.bufferForMidiMessages)
        {
            const bool depart = m.isNoteOn(), arret = m.isNoteOff(), tout = m.isAllNotesOff() || m.isAllSoundOff();

            if (! depart && ! arret && ! tout)
                continue;

            const int instant = juce::jlimit (0, n, juce::roundToInt (m.getTimeStamp() * frequenceEchantillonnage));

            if (instant > position)
            {
                rendre (sortie, debutDuBloc + position, instant - position);
                position = instant;
            }

            if (depart)
                demarrer (m.getNoteNumber(), m.getFloatVelocity());
            else if (arret)
                relacher (m.getNoteNumber());
            else
                toutRelacher();
        }
    }

    if (position < n)
        rendre (sortie, debutDuBloc + position, n - position);
}

//==============================================================================
EtireurDeBoucles::EtireurDeBoucles()  : juce::Thread ("Etireur de boucles")
{
    startThread (juce::Thread::Priority::low);
}

EtireurDeBoucles::~EtireurDeBoucles()
{
    signalThreadShouldExit();
    notify();
    stopThread (5000);
}

bool EtireurDeBoucles::demander (int piste, Demande d)
{
    {
        const juce::ScopedLock sl (verrou);

        // Déjà faite ou déjà demandée : rien à refaire
        if (auto f = faites.find (piste); f != faites.end() && f->second.memeChose (d) && enAttente.count (piste) == 0)
            return false;

        if (auto a = enAttente.find (piste); a != enAttente.end() && a->second.memeChose (d))
            return false;

        enAttente[piste] = std::move (d);
        occupe = true;
    }

    notify();
    return true;
}

void EtireurDeBoucles::annuler (int piste)
{
    const juce::ScopedLock sl (verrou);
    enAttente.erase (piste);
    faites.erase (piste);
}

void EtireurDeBoucles::attendre()
{
    for (int i = 0; i < 3000; ++i)
    {
        {
            const juce::ScopedLock sl (verrou);

            if (enAttente.empty() && ! occupe)
                return;
        }

        juce::Thread::sleep (5);
    }
}

void EtireurDeBoucles::ranger (int piste, std::unique_ptr<Echantillon> e)
{
    const auto maintenant = juce::Time::getMillisecondCounterHiRes() / 1000.0;
    const juce::ScopedLock sl (verrou);

    // L'ancienne boucle de la piste part à la retraite ; celles retirées depuis plus de 5 s sont libérées
    if (auto c = courantes.find (piste); c != courantes.end())
        for (auto& [boucle, retrait] : enService)
            if (boucle.get() == c->second)
                retrait = maintenant;

    enService.erase (std::remove_if (enService.begin(), enService.end(),
                                     [maintenant] (auto& p) { return p.second > 0.0 && maintenant - p.second > 5.0; }),
                     enService.end());

    courantes[piste] = e.get();
    enService.push_back ({ std::move (e), 0.0 });
}

void EtireurDeBoucles::run()
{
    while (! threadShouldExit())
    {
        std::optional<std::pair<int, Demande>> travail;

        {
            const juce::ScopedLock sl (verrou);

            if (! enAttente.empty())
            {
                travail = *enAttente.begin();
                enAttente.erase (enAttente.begin());
            }
            else
            {
                occupe = false;
            }
        }

        if (! travail)
        {
            wait (-1);
            continue;
        }

        auto& [piste, d] = *travail;
        std::unique_ptr<Echantillon> etiree;

        if (d.son != nullptr)
            etiree = etirer (*d.son, d.debut, d.fin, d.secondesVisees, d.demiTons, d.son->frequence);

        // Une demande plus récente est arrivée entre-temps : celle-ci ne sert plus
        {
            const juce::ScopedLock sl (verrou);

            if (enAttente.count (piste) > 0)
                continue;

            faites[piste] = d;
        }

        auto* pointeur = etiree.get();

        if (etiree != nullptr)
            ranger (piste, std::move (etiree));

        for (auto* s : d.samplers)
            s->installerBoucle (pointeur);
    }
}

namespace
{
    /** Passe un son dans SoundTouch (étirement et transposition) ; renvoie tout ce qui sort. */
    juce::AudioBuffer<float> passerDansSoundTouch (const juce::AudioBuffer<float>& entree, double frequence, float rapport, float demiTons)
    {
        const int canaux = entree.getNumChannels();
        constexpr int bloc = 1024;
        te::TimeStretcher etireur;
        etireur.initialise (frequence, bloc, canaux, te::TimeStretcher::soundtouchBetter, {}, false);

        if (! etireur.isInitialised())
            return {};

        etireur.setSpeedAndPitch (rapport, demiTons);

        juce::AudioBuffer<float> sortie (canaux, (int) (entree.getNumSamples() * rapport) + 4 * bloc + 8192);
        juce::AudioBuffer<float> morceau (canaux, bloc);
        int lu = 0, ecrit = 0;
        std::vector<const float*> in ((size_t) canaux);
        std::vector<float*> out ((size_t) canaux);

        for (int securite = 0; securite < 100000 && ecrit < sortie.getNumSamples() - bloc; ++securite)
        {
            for (int c = 0; c < canaux; ++c)
                out[(size_t) c] = morceau.getWritePointer (c);

            int obtenus = 0;

            if (lu < entree.getNumSamples())
            {
                const int n = juce::jmin (etireur.getFramesNeeded(), entree.getNumSamples() - lu);

                for (int c = 0; c < canaux; ++c)
                    in[(size_t) c] = entree.getReadPointer (c, lu);

                obtenus = etireur.processData (in.data(), n, out.data());
                lu += n;
            }
            else
            {
                obtenus = etireur.flush (out.data());

                if (obtenus <= 0)
                    break;
            }

            for (int c = 0; c < canaux; ++c)
                sortie.copyFrom (c, ecrit, morceau, c, 0, obtenus);

            ecrit += obtenus;
        }

        sortie.setSize (canaux, ecrit, true);
        return sortie;
    }

    /** SoundTouch avance un peu le son, d'une quantité qui dépend du rapport : on la mesure sur une suite de coups
        passée dans les mêmes réglages (en échantillons de sortie ; positif : le son sort en avance). */
    int avanceDeSoundTouch (int longueur, double frequence, float rapport, float demiTons)
    {
        constexpr int coupsParTour = 8;
        const int periode = juce::jmax (64, longueur / coupsParTour);
        juce::AudioBuffer<float> coups (1, 3 * coupsParTour * periode);
        coups.clear();

        for (int k = 0; k < 3 * coupsParTour; ++k)
            for (int i = 0; i < 48; ++i)
                coups.setSample (0, k * periode + i, 1.0f - i / 48.0f);

        const auto sortie = passerDansSoundTouch (coups, frequence, rapport, demiTons);
        const double periodeSortie = periode * rapport;
        const auto* d = sortie.getReadPointer (0);
        double somme = 0.0;
        int compte = 0, calme = 0;

        // Coups du tour du milieu : écart à leur place attendue
        for (int i = 0; i < sortie.getNumSamples(); ++i)
        {
            if (std::abs (d[i]) > 0.3f && calme > (int) (periodeSortie * 0.5))
            {
                const double attendu = std::round (i / periodeSortie) * periodeSortie;

                if (attendu >= coupsParTour * periodeSortie && attendu < 2 * coupsParTour * periodeSortie)
                {
                    somme += attendu - i;
                    ++compte;
                }
            }

            calme = std::abs (d[i]) < 0.05f ? calme + 1 : 0;
        }

        return compte > 0 ? juce::roundToInt (somme / compte) : 0;
    }
}

std::unique_ptr<Echantillon> EtireurDeBoucles::etirer (const Echantillon& source, double debut, double fin, double secondesVisees, float demiTons, double frequence)
{
    const int total = source.audio.getNumSamples();
    const int d0 = juce::jlimit (0, total - 1, (int) (debut * total));
    const int l = juce::jlimit (1, total - d0, (int) ((fin - debut) * total));
    const int canaux = juce::jmax (1, source.audio.getNumChannels());
    const int vise = juce::jmax (1, juce::roundToInt (secondesVisees * source.frequence));

    if (l < 64)
        return nullptr;

    // La partie trois fois de suite
    juce::AudioBuffer<float> entree (canaux, 3 * l);

    for (int c = 0; c < canaux; ++c)
        for (int r = 0; r < 3; ++r)
            entree.copyFrom (c, r * l, source.audio, c, d0, l);

    const float rapport = (float) vise / (float) l;
    const auto sortie = passerDansSoundTouch (entree, source.frequence, rapport, demiTons);

    if (sortie.getNumSamples() < 2 * vise)
        return nullptr;

    // Le tour du milieu, recalé de l'avance de SoundTouch : il s'enchaîne avec lui-même sans raccord
    const int depart = juce::jlimit (0, sortie.getNumSamples() - vise, vise - avanceDeSoundTouch (l, source.frequence, rapport, demiTons));
    auto e = std::make_unique<Echantillon>();
    e->audio.setSize (canaux, vise);

    for (int c = 0; c < canaux; ++c)
        e->audio.copyFrom (c, 0, sortie, c, depart, vise);

    e->frequence = frequence;
    e->chemin = source.chemin + " (étirée)"_fr;
    return e;
}

} // namespace groovebox
