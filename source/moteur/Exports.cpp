#include "Exports.h"

namespace groovebox
{
namespace exports
{
    namespace
    {
        constexpr double frequence = 48000.0;
        constexpr int tailleDuBloc = 256;
        constexpr double secondesDeQueue = 4.0;     // échos et réverbération après la dernière mesure

        const juce::StringArray nomsDesRendus { "complet", "sansGuide", "sansGuideAvecClic", "pistes" };

        /** Une carte son simulée à 4 sorties (1-2 la sono, 3-4 le casque), le temps d'un rendu. */
        class CarteDExport
        {
        public:
            explicit CarteDExport (te::Engine& e)  : engine (e)
            {
                te::HostedAudioDeviceInterface::Parameters p;
                p.sampleRate = frequence;
                p.blockSize = tailleDuBloc;
                p.inputChannels = 0;
                p.outputChannels = 4;
                io.initialise (p);
                io.prepareToPlay (frequence, tailleDuBloc);
                engine.getDeviceManager().dispatchPendingUpdates();

                // Hors temps réel, un calcul plus long qu'un bloc n'est pas une coupure
                engine.getDeviceManager().setCpuLimitBeforeMuting (1.0e9);
            }

            ~CarteDExport()
            {
                engine.getDeviceManager().deviceManager.closeAudioDevice();
                engine.getDeviceManager().removeHostedAudioDeviceInterface();
            }

            void jouer (juce::AudioBuffer<float>& bloc)
            {
                bloc.clear();
                juce::MidiBuffer midi;
                io.processBlock (bloc, midi);
            }

        private:
            te::Engine& engine;
            te::HostedAudioDeviceInterface& io { engine.getDeviceManager().getHostedAudioDeviceInterface() };
        };

        double secondesDe (te::Edit& edit, double temps)
        {
            return edit.tempoSequence.toTime (te::BeatPosition::fromBeats (temps)).inSeconds();
        }

        /** Joue le morceau du début de la mesure 1 à la fin (déroulé, ou scène sur « mesures »), plus la queue.
            Renvoie les 4 sorties, à partir de la mesure 1, la queue silencieuse coupée. */
        juce::AudioBuffer<float> rendre (te::Engine& engine, BanqueDeSons* banque, const Morceau& m, const std::optional<Deroule>& deroule,
                                         int scene, int mesures, const std::function<void (float)>& avancee)
        {
            CarteDExport carte (engine);
            Moteur moteur (engine, m, banque);
            moteur.attendreLesBoucles();
            auto& edit = moteur.getEdit();

            if (deroule)
                moteur.rejouer (*deroule);
            else
                moteur.lancerScene (scene);

            const double debut = secondesDe (edit, moteur.tempsDeLaMesure1());
            const double finDeScene = secondesDe (edit, moteur.tempsDeLaMesure1() + (double) mesures * m.mesure.numerateur);
            const double unTour = secondesDe (edit, moteur.tempsDeLaMesure1() + m.mesure.numerateur) - debut;
            double dureePrevue = deroule ? 0.0 : finDeScene - debut;

            if (deroule)
                for (auto& s : deroule->sections)
                    dureePrevue += s.longueur * 60.0 / juce::jmax (1.0, m.tempo) * 4.0 / juce::jmax (1, m.mesure.denominateur);

            const double limite = dureePrevue * 1.5 + 30.0;     // garde-fou : un déroulé qui ne finirait pas
            juce::AudioBuffer<float> tout (4, (int) ((dureePrevue + secondesDeQueue + 2.0) * frequence));
            tout.clear();
            juce::AudioBuffer<float> bloc (4, tailleDuBloc);
            int ecrits = 0;
            std::optional<double> fin;
            bool demarre = false, arretDemande = false;
            double position = 0.0;

            for (int k = 0; ; ++k)
            {
                carte.jouer (bloc);
                moteur.avancer();

                // La position de lecture est celle du début du bloc qu'on vient de calculer
                if (auto contexte = edit.getCurrentPlaybackContext(); contexte != nullptr && edit.getTransport().isPlaying())
                {
                    const double auDebut = contexte->getPosition().inSeconds();
                    position = auDebut + tailleDuBloc / frequence;
                    demarre = demarre || position > debut;
                }

                // On garde le son à partir du bloc où passe la mesure 1, recalé à l'échantillon près
                if (demarre)
                {
                    int depart = 0;

                    if (ecrits == 0)
                        depart = juce::jlimit (0, tailleDuBloc - 1, tailleDuBloc - (int) std::round ((position - debut) * frequence));

                    const int n = tailleDuBloc - depart;

                    if (ecrits + n > tout.getNumSamples())
                        tout.setSize (4, ecrits + n + (int) (10.0 * frequence), true, true);

                    for (int c = 0; c < 4; ++c)
                        tout.copyFrom (c, ecrits, bloc, c, depart, n);

                    ecrits += n;
                }

                const double ecoule = ecrits / frequence;

                if (! fin)
                {
                    // Scène : les clips s'arrêtent au calage qui suit, la fin de la dernière mesure
                    if (! deroule && ! arretDemande && demarre && position >= finDeScene - unTour * 0.5)
                    {
                        moteur.arreterTousLesClips();
                        arretDemande = true;
                    }

                    if (! deroule && demarre && position >= finDeScene)
                        fin = finDeScene - debut;

                    // Déroulé : le moteur s'arrête tout seul à la fin de la dernière section
                    if (deroule && demarre && ! moteur.enLecture())
                        fin = ecoule;

                    if (ecoule > limite || (! demarre && k * tailleDuBloc > 20.0 * frequence))
                        fin = ecoule;
                }

                if (avancee && dureePrevue > 0.0)
                    avancee ((float) juce::jlimit (0.0, 1.0, ecoule / (dureePrevue + secondesDeQueue)));

                if (fin && ecoule >= *fin + secondesDeQueue)
                    break;
            }

            // La queue s'arrête où le son devient inaudible (-70 dB), avec un fondu de 20 ms
            const int finMusique = juce::jmin (ecrits, (int) (*fin * frequence));
            int dernier = finMusique;

            // Le clic (casque, sorties 3-4) s'arrête avec la musique
            for (int c = 2; c < 4; ++c)
                if (finMusique < ecrits)
                    tout.clear (c, finMusique, ecrits - finMusique);

            for (int i = ecrits; --i >= finMusique;)
            {
                bool audible = false;

                for (int c = 0; c < 4 && ! audible; ++c)
                    audible = std::abs (tout.getSample (c, i)) > 3.0e-4f;

                if (audible)
                {
                    dernier = i + 1;
                    break;
                }
            }

            const int longueur = juce::jmin (ecrits, dernier + (int) (0.05 * frequence));
            const int fondu = juce::jmin (longueur, (int) (0.02 * frequence));

            for (int c = 0; c < 4; ++c)
                tout.applyGainRamp (c, longueur - fondu, fondu, 1.0f, 0.0f);

            tout.setSize (4, juce::jmax (1, longueur), true, true);
            return tout;
        }

        bool ecrireWav (const juce::File& fichier, const juce::AudioBuffer<float>& audio)
        {
            fichier.getParentDirectory().createDirectory();
            fichier.deleteFile();
            juce::WavAudioFormat wav;
            std::unique_ptr<juce::AudioFormatWriter> ecrivain (wav.createWriterFor (new juce::FileOutputStream (fichier), frequence, 2, 24, {}, 0));
            return ecrivain != nullptr && ecrivain->writeFromAudioSampleBuffer (audio, 0, audio.getNumSamples());
        }

        /** Deux canaux d'un rendu : la sono (0-1), et le clic du casque (2-3) par-dessus, un peu moins fort, si on le demande.
            La musique garde son niveau ; seules les crêtes qui dépasseraient -0,2 dB sont rabotées (limiteur sans retard). */
        juce::AudioBuffer<float> stereo (const juce::AudioBuffer<float>& rendu, bool avecClic)
        {
            const int n = rendu.getNumSamples();
            juce::AudioBuffer<float> b (2, n);

            for (int c = 0; c < 2; ++c)
            {
                b.copyFrom (c, 0, rendu, c, 0, n);

                if (avecClic)
                    b.addFrom (c, 0, rendu, 2 + c, 0, n, 0.6f);
            }

            constexpr float plafond = 0.977f;

            if (b.getMagnitude (0, n) <= plafond)
                return b;

            const float remontee = 1.0f - std::exp (-1.0f / (0.05f * (float) frequence));
            float gain = 1.0f;

            for (int i = 0; i < n; ++i)
            {
                const float crete = juce::jmax (std::abs (b.getSample (0, i)), std::abs (b.getSample (1, i)));
                const float voulu = crete * gain > plafond ? plafond / crete : 1.0f;
                gain = voulu < gain ? voulu : gain + (juce::jmin (1.0f, voulu) - gain) * remontee;

                for (int c = 0; c < 2; ++c)
                    b.setSample (c, i, b.getSample (c, i) * gain);
            }

            return b;
        }
    }

    juce::String nomDuRendu (Rendu r, bool avecGuide)
    {
        switch (r)
        {
            case Rendu::complet:            return "mix complet";
            case Rendu::sansGuide:          return "mix sans guide";
            case Rendu::sansGuideAvecClic:  return avecGuide ? "mix sans guide + clic" : "mix + clic";
            case Rendu::pistes:             return "pistes séparées"_fr;
        }

        return {};
    }

    juce::var Demande::versJson() const
    {
        auto o = new juce::DynamicObject();
        o->setProperty ("morceau", morceau.versJson());

        if (deroule)
            o->setProperty ("deroule", deroule->versJson());

        o->setProperty ("scene", scene);
        o->setProperty ("mesures", mesures);
        juce::Array<juce::var> liste;

        for (auto r : rendus)
            liste.add (nomsDesRendus[(int) r]);

        o->setProperty ("rendus", liste);
        o->setProperty ("dossier", dossier.getFullPathName());
        o->setProperty ("nom", nom);
        return juce::var (o);
    }

    Demande Demande::depuisJson (const juce::var& o)
    {
        Demande d;
        d.morceau = Morceau::depuisJson (o.getProperty ("morceau", {}));

        if (const auto r = o.getProperty ("deroule", {}); r.isObject())
            d.deroule = Deroule::depuisJson (r);

        d.scene = juce::jmax (0, (int) o.getProperty ("scene", 0));
        d.mesures = juce::jlimit (1, 256, (int) o.getProperty ("mesures", 8));
        d.rendus.clear();

        if (auto liste = o.getProperty ("rendus", {}).getArray())
            for (auto& r : *liste)
                if (const auto i = nomsDesRendus.indexOf (r.toString()); i >= 0)
                    d.rendus.push_back ((Rendu) i);

        d.dossier = juce::File (o.getProperty ("dossier", {}).toString());
        d.nom = o.getProperty ("nom", {}).toString();
        return d;
    }

    void modifierPartout (Morceau& m, std::optional<Deroule>& deroule, const std::function<void (Morceau&)>& f)
    {
        f (m);

        if (deroule)
            for (auto& s : deroule->sections)
                if (s.etat.isObject())
                {
                    auto etat = Morceau::depuisJson (s.etat);
                    f (etat);
                    s.etat = etat.versJson();
                }
    }

    Resultat exporter (te::Engine& engine, const Demande& demande, BanqueDeSons* banque, std::function<void (float, const juce::String&)> progression)
    {
        Resultat resultat;
        const auto debutDuCalcul = juce::Time::getMillisecondCounterHiRes();

        // Le morceau de départ des rendus : sans décompte ; une scène s'arrête à la mesure près ; un déroulé se joue d'un trait
        auto base = demande.morceau;
        auto deroule = demande.deroule;

        if (deroule)
            for (auto& s : deroule->sections)
                s.ouverte = false;

        modifierPartout (base, deroule, [scene = ! deroule] (Morceau& m)
        {
            m.clic.decompte = 0;

            if (scene)
                m.calage = Calage::mesure;
        });

        // Les pistes guides : celles du morceau, et celles marquées dans une section du déroulé
        std::array<bool, nombreDePistes> guides {};
        modifierPartout (base, deroule, [&guides] (Morceau& m) { for (int p = 0; p < nombreDePistes; ++p) guides[(size_t) p] = guides[(size_t) p] || m.pistes[(size_t) p].estUneGuide(); });
        const bool avecGuide = std::any_of (guides.begin(), guides.end(), [] (bool g) { return g; });

        auto veut = [&] (Rendu r) { return std::find (demande.rendus.begin(), demande.rendus.end(), r) != demande.rendus.end(); };

        // Les passages à calculer : nom affiché, réglages du morceau, et ce qu'on en écrit
        struct Passage
        {
            juce::String titre;
            std::function<void (Morceau&)> reglage;
            std::function<void (const juce::AudioBuffer<float>&)> ecrire;
        };

        std::vector<Passage> passages;
        auto fichier = [&] (const juce::String& suffixe) { return demande.dossier.getChildFile (juce::File::createLegalFileName (demande.nom + " - " + suffixe) + ".wav"); };
        auto garder = [&] (const juce::File& f, const juce::AudioBuffer<float>& audio)
        {
            if (ecrireWav (f, audio))
                resultat.fichiers.add (f.getFullPathName());
            else
                resultat.remarques.add ("Impossible d'écrire "_fr + f.getFileName());
        };

        if (veut (Rendu::complet))
            passages.push_back ({ nomDuRendu (Rendu::complet, avecGuide), [] (Morceau& m) { m.clic.actif = false; },
                                  [&] (const juce::AudioBuffer<float>& a) { garder (fichier (nomDuRendu (Rendu::complet, avecGuide)), stereo (a, false)); } });

        if (veut (Rendu::sansGuide) && ! avecGuide)
            resultat.remarques.add ("Pas de batterie guide dans ce morceau : le mix sans guide serait le mix complet"_fr);

        if ((veut (Rendu::sansGuide) && avecGuide) || veut (Rendu::sansGuideAvecClic))
        {
            // Un seul passage : la sono sans guide sur 1-2, le clic seul dans le casque sur 3-4
            passages.push_back ({ nomDuRendu (Rendu::sansGuideAvecClic, avecGuide),
                                  [guides] (Morceau& m)
                                  {
                                      for (int p = 0; p < nombreDePistes; ++p)
                                          if (guides[(size_t) p])
                                              m.pistes[(size_t) p].mute = true;

                                      m.clic.actif = true;
                                      m.musiqueDansCasqueDb = -60.0f;
                                  },
                                  [&] (const juce::AudioBuffer<float>& a)
                                  {
                                      if (veut (Rendu::sansGuide) && avecGuide)
                                          garder (fichier (nomDuRendu (Rendu::sansGuide, true)), stereo (a, false));

                                      if (veut (Rendu::sansGuideAvecClic))
                                          garder (fichier (nomDuRendu (Rendu::sansGuideAvecClic, avecGuide)), stereo (a, true));
                                  } });
        }

        if (veut (Rendu::pistes))
            for (int p = 0; p < nombreDePistes; ++p)
            {
                const auto nomDeLaPiste = demande.morceau.pistes[(size_t) p].nom;
                passages.push_back ({ "piste " + juce::String (p + 1) + " (" + nomDeLaPiste + ")",
                                      [p] (Morceau& m)
                                      {
                                          m.clic.actif = false;

                                          for (int autre = 0; autre < nombreDePistes; ++autre)
                                          {
                                              if (autre != p)
                                                  m.pistes[(size_t) autre].mute = true;

                                              m.pistes[(size_t) autre].solo = false;
                                          }
                                      },
                                      [&, p, nomDeLaPiste] (const juce::AudioBuffer<float>& a)
                                      {
                                          // Une piste qui ne joue jamais ne donne pas de fichier
                                          auto s = stereo (a, false);

                                          if (s.getMagnitude (0, s.getNumSamples()) > 1.0e-4f)
                                              garder (fichier ("piste " + juce::String (p + 1) + " " + nomDeLaPiste), s);
                                      } });
            }

        for (size_t i = 0; i < passages.size(); ++i)
        {
            auto& passage = passages[i];
            auto m = base;
            auto d = deroule;
            modifierPartout (m, d, passage.reglage);

            auto avancee = [&, i] (float x)
            {
                if (progression)
                    progression (((float) i + x) / (float) passages.size(), passage.titre);
            };

            avancee (0.0f);
            const auto audio = rendre (engine, banque, m, d, demande.scene, demande.mesures, avancee);
            resultat.secondesDeMusique = juce::jmax (resultat.secondesDeMusique, audio.getNumSamples() / frequence);
            passage.ecrire (audio);
        }

        if (progression)
            progression (1.0f, "Fini");

        resultat.secondesDeCalcul = (juce::Time::getMillisecondCounterHiRes() - debutDuCalcul) / 1000.0;
        return resultat;
    }

    juce::StringArray copierLaPrise (const juce::File& prise, const juce::File& dossier, const juce::String& nom)
    {
        juce::StringArray fichiers;
        dossier.createDirectory();

        for (auto [source, suffixe] : { std::pair { "sortie.wav", "" }, std::pair { "micros.wav", " - micros" } })
        {
            const auto f = prise.getChildFile (source);

            if (! f.existsAsFile())
                continue;

            const auto cible = dossier.getChildFile (juce::File::createLegalFileName ("Prise " + nom + suffixe) + ".wav");
            cible.deleteFile();

            if (f.copyFileTo (cible))
                fichiers.add (cible.getFullPathName());
        }

        return fichiers;
    }
}

} // namespace groovebox
