// Essais automatiques de groovebox : ce qui se vérifie sans oreille.
// Une carte son simulée (4 sorties, ou 2) remplace la vraie : aucun son ne sort du Mac.

#include <JuceHeader.h>
#include "moteur/Moteur.h"
#include "moteur/Reglages.h"
#include "modele/TapTempo.h"
#include "modele/Demo.h"
#include "modele/Bibliotheque.h"
#include "moteur/FabriqueDeSons.h"
#include "modele/CarteDeGrooves.h"
#include "push/Pilote.h"
#include "push/EcranPush.h"
#include "push/PushMidi.h"
#include "app/Simulateur.h"
#include "app/PagePrises.h"
#include "app/PageRetouche.h"
#include "moteur/SonsDeSynthe.h"

namespace groovebox
{

namespace
{
    constexpr double frequence = 48000.0;
    constexpr int uneSeconde = 48000;

    struct ComportementEssai  : public te::EngineBehaviour
    {
        bool autoInitialiseDeviceManager() override     { return false; }
    };

    /** Carte son simulée : on lui fait jouer N échantillons, elle renvoie ce qui serait sorti. */
    class CarteSimulee
    {
    public:
        CarteSimulee (te::Engine& e, int sorties, int entrees = 0)
            : engine (e), nombreDeSorties (sorties), nombreDEntrees (entrees)
        {
            te::HostedAudioDeviceInterface::Parameters p;
            p.sampleRate = frequence;
            p.blockSize = 128;
            p.inputChannels = entrees;
            p.outputChannels = sorties;
            io.initialise (p);
            io.prepareToPlay (frequence, 128);
            engine.getDeviceManager().dispatchPendingUpdates();

            // Hors temps réel, une pause du système ferait croire au moteur que le calcul déborde : il sauterait un bloc
            engine.getDeviceManager().setCpuLimitBeforeMuting (1.0e9);
        }

        ~CarteSimulee()
        {
            engine.getDeviceManager().deviceManager.closeAudioDevice();
            engine.getDeviceManager().removeHostedAudioDeviceInterface();
        }

        /** Ce qui arrive sur les entrées (les micros) : une valeur par canal et par échantillon. */
        std::function<float (int canal, juce::int64 echantillon)> entree;

        juce::AudioBuffer<float> jouer (int n)
        {
            const int canaux = std::max (nombreDeSorties, nombreDEntrees);
            juce::AudioBuffer<float> tout (canaux, n);
            tout.clear();
            juce::MidiBuffer midi;

            for (int c = 0; c < nombreDEntrees && entree; ++c)
                for (int i = 0; i < n; ++i)
                    tout.setSample (c, i, entree (c, joues + i));

            for (int debut = 0; debut < n; debut += 128)
            {
                const int k = std::min (128, n - debut);
                juce::AudioBuffer<float> bloc (tout.getArrayOfWritePointers(), canaux, debut, k);
                io.processBlock (bloc, midi);
            }

            joues += n;
            juce::AudioBuffer<float> sortie (nombreDeSorties, n);

            for (int c = 0; c < nombreDeSorties; ++c)
                sortie.copyFrom (c, 0, tout, c, 0, n);

            return sortie;
        }

    private:
        te::Engine& engine;
        int nombreDeSorties, nombreDEntrees;
        juce::int64 joues = 0;
        te::HostedAudioDeviceInterface& io { engine.getDeviceManager().getHostedAudioDeviceInterface() };
    };

    /** Banque de sons des essais : les kits de départ, fabriqués une fois dans un dossier temporaire. */
    BanqueDeSons& banqueEssai()
    {
        static auto dossier = []
        {
            auto d = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("groovebox-essais-sons");
            d.deleteRecursively();
            d.createDirectory();
            fabriqueDeSons::assurerKitsDeDepart (d);
            fabriqueDeSons::assurerSonsDuSampler (d, false);
            return d;
        }();

        static BanqueDeSons banque (dossier);
        return banque;
    }

    /** Laisse le fil principal traiter ce qui attend (reconstruction du graphe après un changement). */
    void laisserTravailler (int millisecondes = 100)
    {
        juce::MessageManager::getInstance()->runDispatchLoopUntil (millisecondes);
    }

    float crete (const juce::AudioBuffer<float>& audio, int canal, int debut = 0, int fin = -1)
    {
        if (fin < 0)
            fin = audio.getNumSamples();

        return audio.getMagnitude (canal, debut, fin - debut);
    }

    float niveauEfficace (const juce::AudioBuffer<float>& audio, int canal, int debut = 0, int fin = -1)
    {
        if (fin < 0)
            fin = audio.getNumSamples();

        return audio.getRMSLevel (canal, debut, fin - debut);
    }

    float sautMax (const juce::AudioBuffer<float>& audio, int canal)
    {
        auto d = audio.getReadPointer (canal);
        float m = 0.0f;

        for (int i = 1; i < audio.getNumSamples(); ++i)
            m = std::max (m, std::abs (d[i] - d[i - 1]));

        return m;
    }

    double frequenceMesuree (const juce::AudioBuffer<float>& audio, int canal, int debut, int fin)
    {
        // Le bip sonne la moitié du temps : on compte les montées par zéro, puis on double
        auto d = audio.getReadPointer (canal);
        int passages = 0;

        for (int i = debut + 1; i < fin; ++i)
            if (d[i - 1] <= 0.0f && d[i] > 0.0f)
                ++passages;

        return 2.0 * passages * frequence / (fin - debut);
    }

    /** Repère les coups de clic : un son qui démarre après au moins 10 ms de quasi-silence. */
    class DetecteurDeCoups
    {
    public:
        struct Coup
        {
            juce::int64 echantillon;
            TypeDeTemps type;
        };

        void analyser (const juce::AudioBuffer<float>& audio, int canal)
        {
            auto d = audio.getReadPointer (canal);

            for (int i = 0; i < audio.getNumSamples(); ++i, ++position)
            {
                const auto x = d[i];

                if (restantPourMesure > 0)
                {
                    // On reconnaît le son à sa hauteur : passages par zéro pendant 10 ms
                    if (precedent <= 0.0f && x > 0.0f)
                        ++passages;

                    if (--restantPourMesure == 0)
                        coups.back().type = classer (passages * 100.0);
                }

                if (std::abs (x) > 0.01f && silence >= 480)
                {
                    coups.push_back ({ position, TypeDeTemps::autre });
                    restantPourMesure = 480;
                    passages = 0;
                }

                silence = std::abs (x) < 0.008f ? silence + 1 : 0;
                precedent = x;
            }
        }

        std::vector<Coup> coups;

    private:
        juce::int64 position = 0;
        int silence = 1000000;
        int restantPourMesure = 0, passages = 0;
        float precedent = 0.0f;

        static TypeDeTemps classer (double hauteur)
        {
            auto meilleur = TypeDeTemps::autre;
            double ecart = 1.0e9;

            for (auto t : { TypeDeTemps::premier, TypeDeTemps::groupe, TypeDeTemps::autre, TypeDeTemps::subdivision })
            {
                const auto e = std::abs (hauteur - ClicPlugin::hauteurDe (t));

                if (e < ecart)
                {
                    ecart = e;
                    meilleur = t;
                }
            }

            return meilleur;
        }
    };

    juce::String nomDuType (TypeDeTemps t)
    {
        switch (t)
        {
            case TypeDeTemps::premier:      return "P";
            case TypeDeTemps::groupe:       return "G";
            case TypeDeTemps::autre:        return "a";
            case TypeDeTemps::subdivision:  return "s";
        }

        return "?";
    }

    juce::String suiteDesTypes (const std::vector<DetecteurDeCoups::Coup>& coups, size_t nombre)
    {
        juce::String s;

        for (size_t i = 0; i < std::min (nombre, coups.size()); ++i)
            s << nomDuType (coups[i].type);

        return s;
    }

    juce::String suiteAttendue (const Mesure& m, int mesures)
    {
        juce::String s;

        for (int k = 0; k < mesures; ++k)
            for (auto t : m.typesDesTemps())
                s << nomDuType (t);

        return s;
    }

    /** Lance la lecture et laisse la carte tourner jusqu'au vrai départ (quelques blocs). */
    void demarrer (Moteur& moteur, CarteSimulee& carte)
    {
        moteur.lancer();
        carte.jouer (128);
        moteur.avancer();
    }

    Morceau morceauAvec (double tempo, Mesure mesure, int decompte = 0)
    {
        Morceau m;
        m.tempo = tempo;
        m.mesure = mesure;
        m.clic.decompte = decompte;
        return m;
    }
}

//==============================================================================
class EssaisModele  : public juce::UnitTest
{
public:
    EssaisModele()  : juce::UnitTest ("Modèle"_fr, "groovebox") {}

    void runTest() override
    {
        beginTest ("Les accents suivent le groupement");
        {
            auto texte = [] (const Mesure& m) { return suiteAttendue (m, 1); };
            expectEquals (texte ({ 9, 8, { 2, 2, 2, 3 } }), juce::String ("PaGaGaGaa"));
            expectEquals (texte ({ 7, 8, { 3, 2, 2 } }), juce::String ("PaaGaGa"));
            expectEquals (texte (Mesure::standard (7, 8)), juce::String ("PaGaGaa"));
            expectEquals (texte (Mesure::standard (6, 8)), juce::String ("PaaGaa"));
            expectEquals (texte (Mesure::standard (4, 4)), juce::String ("Paaa"));
            expectEquals (texte ({ 4, 4, { 2, 2 } }), juce::String ("PaGa"));
        }

        beginTest ("Toutes les mesures du menu sont valides, avec tous leurs groupements");
        for (auto& m : Mesure::mesuresProposees())
        {
            expect (m.estValide(), m.nom());

            for (auto& g : Mesure::groupementsProposes (m.numerateur, m.denominateur))
                expect (Mesure { m.numerateur, m.denominateur, g }.estValide(), m.nom() + " " + Mesure::nomGroupement (g));
        }

        beginTest ("Mesures libres et groupements saisis");
        {
            expect (Mesure::lireGroupement ("2 + 2 + 3") == std::vector<int> { 2, 2, 3 });
            expect (! Mesure::lireGroupement ("2+a").has_value());
            expect (! Mesure::lireGroupement ("").has_value());
            expect (! (Mesure { 7, 8, { 2, 2, 2 } }).estValide(), "le total doit faire 7");
            expect (! (Mesure { 7, 3, { 7 } }).estValide(), "le bas doit être 2, 4, 8 ou 16"_fr);
            expect ((Mesure { 11, 16, { 3, 3, 3, 2 } }).estValide());
        }

        beginTest ("Grille par défaut : double-croche en x/4, croche en x/8"_fr);
        expectEquals (Mesure::standard (4, 4).grilleParDefaut(), 16);
        expectEquals (Mesure::standard (7, 8).grilleParDefaut(), 8);

        beginTest ("Un clic par groupe, avec ou sans subdivisions");
        {
            const auto m = Mesure::standard (6, 8);
            const auto groupes = m.motifDeClic (true, false);
            expectEquals ((int) groupes.size(), 2);
            expectEquals (groupes[1].position, 3.0);
            expect (groupes[1].type == TypeDeTemps::groupe);
            expectEquals ((int) m.motifDeClic (true, true).size(), 6);
            expectEquals ((int) m.motifDeClic (false, true).size(), 12);
        }

        beginTest ("Le morceau se sauvegarde et se recharge à l'identique"_fr);
        {
            Morceau m;
            m.nom = "Essai 9/8";
            m.tempo = 137.5;
            m.mesure = { 9, 8, { 2, 2, 2, 3 } };
            m.clic = { false, 0.5f, true, true, 2, true };
            m.musiqueDansCasqueDb = -6.0f;
            m.scenes.push_back ({ "Intro", std::nullopt, std::nullopt });
            m.scenes.push_back ({ "Pont", Mesure::standard (7, 8), 140.0 });

            juce::TemporaryFile fichier (".json");
            expect (m.enregistrer (fichier.getFile()));
            auto relu = Morceau::charger (fichier.getFile());
            expect (relu.has_value());
            expect (relu.has_value() && *relu == m, "le morceau relu diffère"_fr);
            expect (fichier.getFile().loadFileAsString().contains ("\"groupement\""), "le JSON doit rester lisible");
        }

        beginTest ("Un fichier abîmé ne donne pas de morceau absurde"_fr);
        {
            auto m = Morceau::depuisJson (juce::JSON::parse (R"({"tempo": 900, "mesure": {"numerateur": 7, "denominateur": 8, "groupement": [5, 5]}})"));
            expectEquals (m.tempo, Morceau::tempoMax);
            expect (m.mesure == Mesure::standard (7, 8), "groupement faux remplacé par celui par défaut"_fr);
        }

        beginTest ("Tap Tempo");
        {
            TapTempo tap;
            expect (! tap.taper (10.0).has_value());

            std::optional<double> resultat;

            for (int i = 1; i <= 4; ++i)
                resultat = tap.taper (10.0 + 0.5 * i);

            expect (resultat.has_value());
            expectWithinAbsoluteError (resultat.value_or (0.0), 120.0, 0.01);

            expect (! tap.taper (20.0).has_value(), "après une pause de plus de 2 s, on recommence"_fr);
            expectWithinAbsoluteError (tap.taper (20.4).value_or (0.0), 150.0, 0.01);
        }
    }
};

//==============================================================================
class EssaisSorties  : public juce::UnitTest
{
public:
    EssaisSorties()  : juce::UnitTest ("Sorties", "groovebox") {}

    void runTest() override
    {
        auto& engine = *te::Engine::getEngines()[0];

        beginTest ("Le son de test sort sur les sorties 1-2, et aussi dans le casque (3-4)");
        {
            CarteSimulee carte (engine, 4);
            Moteur moteur (engine, {}, &banqueEssai());
            expect (moteur.sortieCasqueDisponible(), "les sorties 3-4 devraient être vues"_fr);

            moteur.activerBip (Sortie::principale, true);
            auto audio = carte.jouer (2 * uneSeconde);

            for (int canal : { 0, 1, 2, 3 })
            {
                expect (niveauEfficace (audio, canal, uneSeconde / 2) > 0.05f, "niveau trop faible sur la sortie " + juce::String (canal + 1));
                expectWithinAbsoluteError (frequenceMesuree (audio, canal, uneSeconde / 2, 2 * uneSeconde), 440.0, 15.0);
            }
        }

        beginTest ("Le test casque et le clic sortent sur 3-4, jamais sur 1-2");
        {
            CarteSimulee carte (engine, 4);
            auto morceau = morceauAvec (120.0, Mesure::standard (4, 4));
            morceau.clic.ecouteSurMac = true;   // doit être ignoré : la carte a des sorties 3-4
            Moteur moteur (engine, morceau, &banqueEssai());
            expect (! moteur.getMorceau().clic.ecouteSurMac);

            moteur.activerBip (Sortie::casque, true);
            demarrer (moteur, carte);
            auto audio = carte.jouer (3 * uneSeconde);

            for (int canal : { 0, 1 })
                expectEquals (crete (audio, canal), 0.0f, "rien du casque sur la sortie " + juce::String (canal + 1));

            for (int canal : { 2, 3 })
                expect (crete (audio, canal) > 0.3f, "clic absent sur la sortie " + juce::String (canal + 1));
        }

        beginTest ("Sans sorties 3-4, le clic et le test casque restent muets");
        {
            CarteSimulee carte (engine, 2);
            Moteur moteur (engine, morceauAvec (120.0, Mesure::standard (4, 4)), &banqueEssai());
            expect (! moteur.sortieCasqueDisponible());

            moteur.activerBip (Sortie::casque, true);
            expect (! moteur.bipActif (Sortie::casque), "le bouton casque doit refuser de s'allumer");
            demarrer (moteur, carte);
            auto audio = carte.jouer (2 * uneSeconde);

            for (int canal : { 0, 1 })
                expectEquals (crete (audio, canal), 0.0f);
        }

        beginTest ("Écoute sur le Mac : le clic sur 1-2, sans doubler la musique"_fr);
        {
            CarteSimulee carte (engine, 2);
            Moteur moteur (engine, morceauAvec (120.0, Mesure::standard (4, 4)), &banqueEssai());
            moteur.activerBip (Sortie::principale, true);
            auto sansEcoute = carte.jouer (uneSeconde);

            moteur.modifier ([] (Morceau& m) { m.clic.actif = false; m.clic.ecouteSurMac = true; });
            expect (moteur.ecouteSurMacEnCours());
            laisserTravailler();
            carte.jouer (uneSeconde / 2);
            auto avecEcoute = carte.jouer (uneSeconde);

            expectWithinAbsoluteError (niveauEfficace (avecEcoute, 0), niveauEfficace (sansEcoute, 0), 0.01f, "la musique ne doit pas être doublée"_fr);

            moteur.activerBip (Sortie::principale, false);
            moteur.modifier ([] (Morceau& m) { m.clic.actif = true; });
            demarrer (moteur, carte);
            laisserTravailler();
            auto clic = carte.jouer (2 * uneSeconde);
            expect (crete (clic, 0) > 0.3f, "le clic doit s'entendre sur le Mac");
        }

        beginTest ("Le son de test marche aussi à l'arrêt"_fr);
        {
            CarteSimulee carte (engine, 2);
            Moteur moteur (engine, {}, &banqueEssai());
            expect (! moteur.enLecture());
            moteur.activerBip (Sortie::principale, true);
            auto audio = carte.jouer (uneSeconde);
            expect (niveauEfficace (audio, 0) > 0.05f);
            expect (sautMax (audio, 0) < 0.03f, "clic parasite au démarrage du bip"_fr);
        }

        beginTest ("Réglages de la carte son : sauvegarde et rechargement"_fr);
        {
            auto dossier = juce::File::createTempFile ("groovebox-reglages");
            dossier.createDirectory();

            {
                Reglages reglages (dossier);
                reglages.getPropertiesFile().setValue ("essai", 128);
                reglages.getPropertiesFile().saveIfNeeded();
            }

            {
                Reglages reglages (dossier);
                expectEquals (reglages.getPropertiesFile().getIntValue ("essai"), 128);
            }

            dossier.deleteRecursively();
            expect (Reglages::dossierParDefaut().getFullPathName().contains ("Library/Application Support/groovebox"));
        }
    }
};

//==============================================================================
class EssaisHorloge  : public juce::UnitTest
{
public:
    EssaisHorloge()  : juce::UnitTest ("Horloge et clic", "groovebox") {}

    void runTest() override
    {
        auto& engine = *te::Engine::getEngines()[0];

        // Le clic des essais passe sur 1-2 (écoute sur le Mac) : carte à 2 sorties, moins de mémoire pour 10 minutes
        auto lancerSurDeuxSorties = [] (Moteur& moteur, CarteSimulee& carte)
        {
            moteur.modifier ([] (Morceau& m) { m.clic.ecouteSurMac = true; });
            laisserTravailler();
            demarrer (moteur, carte);
        };

        for (auto [tempo, mesure] : { std::pair { 100.0, Mesure { 9, 8, { 2, 2, 2, 3 } } },
                                      std::pair { 100.0, Mesure { 7, 8, { 3, 2, 2 } } },
                                      std::pair { 140.0, Mesure::standard (6, 8) },
                                      std::pair { 90.0, Mesure::standard (5, 4) },
                                      std::pair { 120.0, Mesure::standard (4, 4) },
                                      std::pair { 174.0, Mesure::standard (12, 8) } })
        {
            beginTest ("Accents justes en " + mesure.nom() + " " + mesure.nomGroupement() + " à "_fr + juce::String (tempo) + " BPM");
            CarteSimulee carte (engine, 2);
            Moteur moteur (engine, morceauAvec (tempo, mesure), &banqueEssai());
            lancerSurDeuxSorties (moteur, carte);

            const double tempsParSeconde = tempo / 60.0 * mesure.denominateur / 4.0;
            DetecteurDeCoups detecteur;
            detecteur.analyser (carte.jouer ((int) (frequence * (2 * mesure.numerateur + 0.5) / tempsParSeconde)), 0);

            const auto attendu = suiteAttendue (mesure, 2);
            expectEquals (suiteDesTypes (detecteur.coups, (size_t) attendu.length()), attendu);
        }

        for (auto [tempo, mesure] : { std::pair { 120.0, Mesure::standard (4, 4) }, std::pair { 173.0, Mesure::standard (7, 8) } })
        {
            beginTest ("Aucune dérive sur 10 minutes en "_fr + mesure.nom() + " à "_fr + juce::String (tempo) + " BPM");
            CarteSimulee carte (engine, 2);
            Moteur moteur (engine, morceauAvec (tempo, mesure), &banqueEssai());
            lancerSurDeuxSorties (moteur, carte);

            DetecteurDeCoups detecteur;

            for (int seconde = 0; seconde < 600; ++seconde)
                detecteur.analyser (carte.jouer (uneSeconde), 0);

            const double echantillonsParTemps = frequence * 60.0 / tempo * 4.0 / mesure.denominateur;
            const auto& coups = detecteur.coups;
            const auto attendus = (size_t) (600.0 * frequence / echantillonsParTemps);
            expect (coups.size() + 2 >= attendus && coups.size() <= attendus + 2,
                    "nombre de coups : " + juce::String ((int) coups.size()) + " au lieu d'environ " + juce::String ((int) attendus));

            // Le premier coup peut arriver une fraction de milliseconde en retard au démarrage ; la dérive se mesure ensuite
            auto ecart = [&] (size_t k) { return (double) (coups[k].echantillon - coups[1].echantillon) - (double) (k - 1) * echantillonsParTemps; };
            double ecartMax = 0.0;

            size_t ouLeMax = 0;

            for (size_t k = 1; k < coups.size(); ++k)
                if (std::abs (ecart (k)) > ecartMax)
                {
                    ecartMax = std::abs (ecart (k));
                    ouLeMax = k;
                }

            if (ecartMax > 1.5)
            {
                juce::String autour;

                for (size_t k = ouLeMax > 3 ? ouLeMax - 3 : 1; k < std::min (coups.size(), ouLeMax + 4); ++k)
                    autour << (int) k << ":" << juce::String (ecart (k), 1) << " ";

                logMessage ("  saut vers le coup " + juce::String ((int) ouLeMax) + " (" + juce::String (coups[ouLeMax].echantillon / frequence, 2) + " s) : " + autour);
            }

            const auto retardAuDepart = std::abs ((double) (coups[1].echantillon - coups[0].echantillon) - echantillonsParTemps);
            expect (ecartMax <= 1.5, "écart maximal avec l'horloge théorique : "_fr + juce::String (ecartMax, 2) + " échantillons"_fr);
            expect (retardAuDepart <= 12.0, "premier coup décalé de "_fr + juce::String (retardAuDepart, 1) + " échantillons"_fr);
            expect (moteur.enLecture(), "la lecture s'est arrêtée"_fr);
            logMessage ("  " + juce::String ((int) coups.size()) + " coups, écart maximal "_fr + juce::String (ecartMax, 2)
                        + " échantillon(s), premier coup décalé de "_fr + juce::String (retardAuDepart, 1));
        }

        beginTest ("Subdivisions et un clic par groupe en 6/8");
        {
            CarteSimulee carte (engine, 2);
            auto morceau = morceauAvec (100.0, Mesure::standard (6, 8));
            morceau.clic.unClicParGroupe = true;
            Moteur moteur (engine, morceau, &banqueEssai());
            lancerSurDeuxSorties (moteur, carte);
            DetecteurDeCoups detecteur;
            detecteur.analyser (carte.jouer (uneSeconde * 3), 0);
            expectEquals (suiteDesTypes (detecteur.coups, 4), juce::String ("PGPG"));

            moteur.arreter();
            moteur.modifier ([] (Morceau& m) { m.clic.subdivisions = true; });
            laisserTravailler();
            demarrer (moteur, carte);
            DetecteurDeCoups avecSubdivisions;
            avecSubdivisions.analyser (carte.jouer (uneSeconde * 2), 0);
            expectEquals (suiteDesTypes (avecSubdivisions.coups, 6), juce::String ("PssGss"));
        }

        beginTest ("Décompte de 2 mesures, puis mesure 1"_fr);
        {
            CarteSimulee carte (engine, 2);
            Moteur moteur (engine, morceauAvec (120.0, Mesure::standard (4, 4), 2), &banqueEssai());
            lancerSurDeuxSorties (moteur, carte);

            DetecteurDeCoups detecteur;
            detecteur.analyser (carte.jouer (uneSeconde), 0);
            expectEquals (moteur.getPosition().mesure, -1, "première mesure de décompte"_fr);
            detecteur.analyser (carte.jouer (uneSeconde * 2), 0);
            expectEquals (moteur.getPosition().mesure, 0, "seconde mesure de décompte"_fr);
            detecteur.analyser (carte.jouer (uneSeconde + uneSeconde / 4), 0);
            expectEquals (moteur.getPosition().mesure, 1, "après le décompte, mesure 1"_fr);
            expectEquals (moteur.getPosition().temps, 0, "milieu du premier temps de la mesure 1"_fr);
            expectEquals (suiteDesTypes (detecteur.coups, 9), juce::String ("PaaaPaaaP"));
        }

        beginTest ("Changer le tempo en pleine lecture ne fait pas sauter la mesure");
        {
            CarteSimulee carte (engine, 2);
            Moteur moteur (engine, morceauAvec (120.0, Mesure::standard (4, 4)), &banqueEssai());
            lancerSurDeuxSorties (moteur, carte);

            DetecteurDeCoups detecteur;
            detecteur.analyser (carte.jouer (uneSeconde * 2 + uneSeconde / 4), 0);
            const auto avant = moteur.getPosition();

            moteur.modifier ([] (Morceau& m) { m.tempo = 90.0; });
            laisserTravailler();
            const auto apres = moteur.getPosition();
            expect (apres.mesure == avant.mesure && apres.temps == avant.temps, "la position a sauté"_fr);

            detecteur.analyser (carte.jouer (uneSeconde * 4), 0);
            const auto& coups = detecteur.coups;
            expect (coups.size() >= 9, "pas assez de coups");

            juce::String intervalles;
            bool tousPlausibles = true;

            for (size_t k = 1; k < coups.size(); ++k)
            {
                const auto i = coups[k].echantillon - coups[k - 1].echantillon;
                intervalles << (int) i << " ";
                tousPlausibles = tousPlausibles && i >= 24000 - 2 && i <= 32000 + 2;
            }

            expect (tousPlausibles, "intervalles : " + intervalles);
            expectWithinAbsoluteError ((double) (coups.back().echantillon - coups[coups.size() - 2].echantillon), 32000.0, 1.5, "tempo final 90");
            expectEquals (suiteDesTypes (coups, 9), juce::String ("PaaaPaaaP"));
            expect (moteur.enLecture());
        }

        beginTest ("Couper puis rallumer le clic : pas d'arrêt, pas de saut"_fr);
        {
            CarteSimulee carte (engine, 2);
            Moteur moteur (engine, morceauAvec (120.0, Mesure::standard (4, 4)), &banqueEssai());
            lancerSurDeuxSorties (moteur, carte);
            carte.jouer (uneSeconde / 3);

            moteur.modifier ([] (Morceau& m) { m.clic.actif = false; });
            carte.jouer (uneSeconde / 10);
            auto coupe = carte.jouer (uneSeconde * 2);
            expect (crete (coupe, 0) < 0.001f, "le clic coupé doit se taire"_fr);
            expect (moteur.enLecture(), "couper le clic ne doit pas arrêter la lecture"_fr);

            moteur.modifier ([] (Morceau& m) { m.clic.actif = true; });
            auto rallume = carte.jouer (uneSeconde * 2);
            expect (crete (rallume, 0) > 0.3f);
        }

        beginTest ("Arrêter puis relancer repart à la mesure 1, au tempo choisi"_fr);
        {
            CarteSimulee carte (engine, 2);
            Moteur moteur (engine, morceauAvec (120.0, Mesure::standard (4, 4)), &banqueEssai());
            lancerSurDeuxSorties (moteur, carte);
            carte.jouer (uneSeconde);
            moteur.modifier ([] (Morceau& m) { m.tempo = 150.0; });
            laisserTravailler();
            carte.jouer (uneSeconde);
            moteur.arreter();
            laisserTravailler();
            expect (! moteur.enLecture());
            expectEquals (moteur.getEdit().tempoSequence.getNumTempos(), 1, "un seul tempo après l'arrêt"_fr);

            demarrer (moteur, carte);
            carte.jouer (uneSeconde / 10);
            expectEquals (moteur.getPosition().mesure, 1);
            expectWithinAbsoluteError (moteur.getEdit().tempoSequence.getTempo (0)->getBpm(), 150.0, 0.001);
        }

        beginTest ("La mesure ne change pas pendant la lecture");
        {
            CarteSimulee carte (engine, 2);
            Moteur moteur (engine, morceauAvec (120.0, Mesure::standard (4, 4)), &banqueEssai());
            demarrer (moteur, carte);
            moteur.modifier ([] (Morceau& m) { m.mesure = Mesure::standard (7, 8); });
            expect (moteur.getMorceau().mesure == Mesure::standard (4, 4));
            moteur.arreter();
            moteur.modifier ([] (Morceau& m) { m.mesure = Mesure::standard (7, 8); });
            expect (moteur.getMorceau().mesure == Mesure::standard (7, 8));
        }
    }
};


//==============================================================================
class EssaisPush  : public juce::UnitTest
{
public:
    EssaisPush()  : juce::UnitTest ("Push 2", "groovebox") {}

    struct ClientEssai  : public ClientSurface
    {
        void afficher (const EtatSurface& e, const juce::Image& i) override     { etat = e; ecran = i; ++nombre; }
        EtatSurface etat;
        juce::Image ecran;
        int nombre = 0;
    };

    static juce::String octets (const juce::MidiMessage& m)
    {
        return juce::String::toHexString (m.getRawData(), m.getRawDataSize()).toUpperCase().removeCharacters (" ");
    }

    void runTest() override
    {
        using T = Evenement::Type;

        beginTest ("Lecture des messages du Push");
        {
            auto lire = [] (const juce::MidiMessage& m) { return lireMessage (m).value_or (Evenement { T::bouton, -1, -1, false }); };

            expect (lire (juce::MidiMessage::noteOn (1, 36, (juce::uint8) 100)) == Evenement { T::pad, 0, 100, true }, "pad en bas à gauche"_fr);
            const auto haut = lire (juce::MidiMessage::noteOn (1, 99, (juce::uint8) 5));
            expect (haut.type == T::pad && haut.ligne() == 7 && haut.colonne() == 7, "pad en haut à droite"_fr);
            expect (lire (juce::MidiMessage::noteOff (1, 50)) == Evenement { T::pad, 14, 0, false }, "pad relâché"_fr);
            expect (lire (juce::MidiMessage::controllerEvent (1, 71, 1)) == Evenement { T::encodeurTourne, 0, 1, false });
            expect (lire (juce::MidiMessage::controllerEvent (1, 78, 20)) == Evenement { T::encodeurTourne, 7, 20, false });
            expect (lire (juce::MidiMessage::controllerEvent (1, 14, 127)) == Evenement { T::encodeurTourne, (int) Encodeur::tempo, -1, false });
            expect (lire (juce::MidiMessage::controllerEvent (1, 79, 124)) == Evenement { T::encodeurTourne, (int) Encodeur::master, -4, false });
            expect (lire (juce::MidiMessage::controllerEvent (1, 15, 64)) == Evenement { T::encodeurTourne, (int) Encodeur::swing, -64, false });
            expect (lire (juce::MidiMessage::noteOn (1, 10, (juce::uint8) 127)) == Evenement { T::encodeurTouche, (int) Encodeur::tempo, 127, true });
            expect (lire (juce::MidiMessage::noteOn (1, 0, (juce::uint8) 0)) == Evenement { T::encodeurTouche, 0, 0, false });
            expect (lire (juce::MidiMessage::noteOn (1, 12, (juce::uint8) 127)) == Evenement { T::bandeTouchee, 0, 127, true });
            expect (lire (juce::MidiMessage::controllerEvent (1, 85, 127)) == Evenement { T::bouton, 85, 1, true }, "Play appuyé"_fr);
            expect (lire (juce::MidiMessage::controllerEvent (1, 85, 0)) == Evenement { T::bouton, 85, 0, false }, "Play relâché"_fr);
            expect (lire (juce::MidiMessage::pitchWheel (1, 16383)) == Evenement { T::bandeDeplacee, 0, 16383, false });
        }

        beginTest ("Chaque contrôle du simulateur produit le message du vrai Push");
        {
            int pads = 0, boutons = 0, encodeurs = 0;
            std::set<int> numerosDeBoutons;

            for (auto& e : Simulateur::plan())
            {
                using G = Simulateur::Element::Genre;
                Evenement ev;

                if (e.genre == G::pad)              { ev = { T::pad, e.numero, 90, true }; ++pads; }
                else if (e.genre == G::bouton)      { ev = { T::bouton, e.numero, 1, true }; ++boutons; numerosDeBoutons.insert (e.numero); }
                else if (e.genre == G::encodeur)    { ev = { T::encodeurTourne, e.numero, -3, false }; ++encodeurs; }
                else                                continue;

                const auto relu = lireMessage (messageDe (ev));
                expect (relu.has_value() && *relu == ev, "contrôle "_fr + juce::String (e.numero));

                if (e.genre == G::bouton)
                    expect (! nomDuBouton (e.numero).startsWith ("Contrôle"_fr), "bouton sans nom : "_fr + juce::String (e.numero));
            }

            expectEquals (pads, 64);
            expectEquals (boutons, 65, "boutons du Push 2"_fr);
            expectEquals ((int) numerosDeBoutons.size(), 65, "chaque bouton a son propre numéro"_fr);
            expectEquals (encodeurs, 11);
        }

        beginTest ("Messages des LED : seules les différences partent");
        {
            EtatSurface avant, apres;
            apres.pads[0] = palette::indexDePiste (2, 3);
            apres.boutons[(size_t) Bouton::play] = palette::vert;

            const auto messages = apres.messagesDepuis (avant);
            expectEquals ((int) messages.size(), 2);
            expectEquals (octets (messages[0]), juce::String ("90244B"), "pad 36 : couleur 75"_fr);
            expectEquals (octets (messages[1]), juce::String ("B0557E"), "Play en vert"_fr);
            expectEquals ((int) apres.tousLesMessages().size(), 64 + 128 + 1);     // pads, boutons, réglage de la bande tactile
            expect (apres.messagesDepuis (apres).empty());
        }

        beginTest ("Commandes système exclusives, comme dans le manuel d'Ableton"_fr);
        {
            expectEquals (octets (sysex::modeMidi (1)), juce::String ("F000211D01010A01F7"));
            expectEquals (octets (sysex::entreeDePalette (125, juce::Colour (0, 0, 255), 126)), juce::String ("F000211D0101037D000000007F017E00F7"));
            expectEquals (octets (sysex::reappliquerPalette()), juce::String ("F000211D010105F7"));
            expectEquals (octets (sysex::configurationBande (0x68)), juce::String ("F000211D01011768F7"));
            expectEquals ((int) palette::entreesProgrammees().size(), 32);

            for (auto& [index, couleur] : palette::entreesProgrammees())
                expect (index >= 64 && index < 96, "les entrées 16, 48 et 122 à 127 restent celles d'Ableton"_fr);
        }

        beginTest ("Format de l'écran : couleurs, lignes de 2048 octets, brouillage"_fr);
        {
            expectEquals ((int) formatEcran::pixel (juce::Colours::white), 0xFFFF);
            expectEquals ((int) formatEcran::pixel (juce::Colour (255, 0, 0)), 0x001F);
            expectEquals ((int) formatEcran::pixel (juce::Colour (0, 255, 0)), 0x07E0);
            expectEquals ((int) formatEcran::pixel (juce::Colour (0, 0, 255)), 0xF800);

            juce::Image image (juce::Image::RGB, 960, 160, true);
            image.setPixelAt (0, 0, juce::Colour (255, 0, 0));
            image.setPixelAt (959, 159, juce::Colours::white);
            std::vector<juce::uint8> tampon ((size_t) formatEcran::tailleImage);
            formatEcran::convertir (image, tampon.data());

            expectEquals ((int) tampon.size(), 160 * 2048);
            expectEquals ((int) tampon[0], 0x1F ^ 0xE7, "rouge, octet faible d'abord"_fr);
            expectEquals ((int) tampon[1], 0x00 ^ 0xF3);
            expectEquals ((int) tampon[2], 0xE7, "pixel noir brouillé"_fr);
            expectEquals ((int) tampon[3], 0xFF);
            const auto dernier = (size_t) (159 * 2048 + 959 * 2);
            expectEquals ((int) tampon[dernier], 0xFF ^ 0xE7);
            expectEquals ((int) tampon[dernier + 1], 0xFF ^ 0xFF);
            expectEquals ((int) tampon[2048 - 1], 0xFF, "bourrage brouillé lui aussi"_fr);
            expectEquals ((int) tampon[1920], 0xE7);
        }

        auto& engine = *te::Engine::getEngines()[0];

        beginTest ("Dessiner et convertir l'écran prend moins de 5 ms");
        {
            CarteSimulee carte (engine, 2);
            Moteur moteur (engine, {}, &banqueEssai());
            Surface surface;
            Pilote pilote (moteur, surface);
            juce::Image image (juce::Image::RGB, 960, 160, true);
            std::vector<juce::uint8> tampon ((size_t) formatEcran::tailleImage);

            const auto debut = juce::Time::getMillisecondCounterHiRes();

            for (int i = 0; i < 300; ++i)
            {
                pilote.dessinerEcran (image);
                formatEcran::convertir (image, tampon.data());
            }

            const auto parImage = (juce::Time::getMillisecondCounterHiRes() - debut) / 300.0;
            logMessage ("  " + juce::String (parImage, 2) + " ms par image");
            expect (parImage < 5.0, juce::String (parImage, 2) + " ms par image");
        }

        beginTest ("Les contrôles du Push règlent le morceau");
        {
            CarteSimulee carte (engine, 2);
            Moteur moteur (engine, morceauAvec (120.0, Mesure::standard (4, 4)), &banqueEssai());
            Surface surface;
            Pilote pilote (moteur, surface);
            auto bouton = [&] (Bouton b) { surface.recevoir ({ T::bouton, (int) b, 1, true }); surface.recevoir ({ T::bouton, (int) b, 0, false }); };
            auto tourner = [&] (int encodeur, int pas) { surface.recevoir ({ T::encodeurTourne, encodeur, pas, false }); };

            bouton (Bouton::setup);
            expect (pilote.getPage() == Pilote::Page::clic, "Setup : page des réglages du clic"_fr);

            bouton (Bouton::play);
            expect (moteur.enLecture(), "Play lance la lecture"_fr);
            expectEquals (pilote.calculerEtat().boutons[(size_t) Bouton::play], palette::vert);

            bouton (Bouton::metronome);
            expect (! moteur.getMorceau().clic.actif, "Metronome coupe le clic");
            expectEquals (pilote.calculerEtat().boutons[(size_t) Bouton::metronome], palette::blancFaible);
            bouton (Bouton::metronome);
            expect (moteur.getMorceau().clic.actif);

            tourner ((int) Encodeur::tempo, 3);
            expectEquals (moteur.getMorceau().tempo, 123.0);
            tourner (0, 4);
            expectEquals (moteur.getMorceau().tempo, 124.0, "encodeur 1 : un BPM tous les 4 pas"_fr);

            surface.recevoir ({ T::bouton, (int) Bouton::shift, 1, true });
            tourner ((int) Encodeur::tempo, 5);
            surface.recevoir ({ T::bouton, (int) Bouton::shift, 0, false });
            expectWithinAbsoluteError (moteur.getMorceau().tempo, 124.5, 1.0e-9, "Shift : réglage fin"_fr);

            tourner (3, 7);
            expectEquals (moteur.getMorceau().clic.decompte, 0, "7 pas ne suffisent pas"_fr);
            tourner (3, 1);
            expectEquals (moteur.getMorceau().clic.decompte, 1, "8 pas : une valeur de plus");
            tourner (4, 8);
            expect (moteur.getMorceau().clic.subdivisions);
            tourner ((int) Encodeur::master, -8);
            expectWithinAbsoluteError (moteur.getMorceau().volumeGeneralDb, -2.0f, 1.0e-6f);

            bouton (Bouton::play);
            expect (! moteur.enLecture(), "Play arrête la lecture"_fr);

            bouton (Bouton::session);
            expectEquals (pilote.getAnnonce().titre, juce::String ("Session"), "bouton reconnu, annoncé à l'écran"_fr);

            // L'annonce reste dans le bandeau du haut : les 8 colonnes de réglages restent visibles, même en tournant un encodeur
            bouton (Bouton::mix);
            tourner (0, 2);
            juce::Image image (juce::Image::RGB, EtatSurface::largeurEcran, EtatSurface::hauteurEcran, true);
            pilote.dessinerEcran (image);

            for (int i = 0; i < 8; ++i)
            {
                bool barre = false;
                const int x0 = i * EtatSurface::largeurEcran / 8;

                for (int x = x0 + 16; x < x0 + 30 && ! barre; ++x)
                    barre = image.getPixelAt (x, 140) != juce::Colour (0xff101216);

                expect (barre, "colonne " + juce::String (i + 1) + " visible pendant l'annonce");
            }

            if (auto chemin = juce::SystemStats::getEnvironmentVariable ("GROOVEBOX_CAPTURE", {}); chemin.isNotEmpty())
            {
                juce::FileOutputStream sortie (juce::File (chemin).getNonexistentSibling());
                juce::PNGImageFormat().writeImageToStream (image, sortie);
            }

            // Toutes les touches sont éclairées, au moins faiblement
            const auto etat = pilote.calculerEtat();

            for (int b : { 3, 9, 28, 29, 30, 31, 35, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63,
                           85, 86, 87, 88, 89, 90, 110, 111, 112, 113, 116, 117, 118, 119 })
                expect (etat.boutons[(size_t) b] != palette::noir, nomDuBouton (b) + " éclairé"_fr);
        }

        beginTest ("Session au Push : pads, scènes, effacer, dupliquer, annuler"_fr);
        {
            CarteSimulee carte (engine, 2);
            Moteur moteur (engine, morceauDeDemonstration(), &banqueEssai());
            Surface surface;
            Pilote pilote (moteur, surface);
            auto appui = [&] (Bouton b) { surface.recevoir ({ T::bouton, (int) b, 1, true }); };
            auto relache = [&] (Bouton b) { surface.recevoir ({ T::bouton, (int) b, 0, false }); };
            auto pad = [&] (int piste, int scene) { surface.recevoir (Evenement::pad (7 - scene, piste, 100)); surface.recevoir (Evenement::pad (7 - scene, piste, 0)); };

            expect (pilote.getPage() == Pilote::Page::mix, "page Mix par défaut"_fr);
            expectEquals (pilote.calculerEtat().pads[(size_t) (7 * 8 + 0)], palette::indexDePiste (0, 1), "clip présent : couleur faible"_fr);
            expectEquals (pilote.calculerEtat().pads[(size_t) (7 * 8 + 1)], palette::noir, "case vide : éteinte"_fr);
            expectEquals (pilote.calculerEtat().boutons[43], palette::grisClair, "scène 1 avec des clips"_fr);

            // Bouton de scène 2 (cc 42) : la scène part, avec le décompte
            appui ((Bouton) 42); relache ((Bouton) 42);
            expect (moteur.enLecture() && moteur.etatCase (0, 1).departEnAttente, "la scène Groove attend la mesure 1"_fr);

            for (int i = 0; i < 30; ++i) { carte.jouer (uneSeconde / 10); moteur.avancer(); }

            expect (moteur.etatCase (0, 1).joue && moteur.etatCase (1, 1).joue, "la scène joue"_fr);
            expectEquals (pilote.calculerEtat().boutons[42], palette::vert, "scène qui joue : verte"_fr);

            // Un pad lance un autre clip sur sa piste
            pad (0, 2);
            expect (moteur.etatCase (0, 2).departEnAttente);

            // Delete + pad efface ; Undo le rend
            appui (Bouton::deleteBouton); pad (3, 2); relache (Bouton::deleteBouton);
            expect (moteur.getMorceau().clip (3, 2) == nullptr, "clip effacé"_fr);
            appui (Bouton::undo); relache (Bouton::undo);
            expect (moteur.getMorceau().clip (3, 2) != nullptr, "Undo le rend"_fr);

            // Duplicate + pad : copie dans la première case vide en dessous
            appui (Bouton::duplicate); pad (3, 2); relache (Bouton::duplicate);
            expect (moteur.getMorceau().clip (3, 3) != nullptr && *moteur.getMorceau().clip (3, 3) == *moteur.getMorceau().clip (3, 2), "clip dupliqué en dessous"_fr);

            // Duplicate tenu, source puis destination
            appui (Bouton::duplicate); pad (0, 1); pad (7, 6); relache (Bouton::duplicate);
            expect (moteur.getMorceau().clip (7, 6) != nullptr && moteur.getMorceau().clip (7, 6)->nom == moteur.getMorceau().clip (0, 1)->nom, "copie d'une case à l'autre");
            expect (moteur.getMorceau().clip (0, 2) != nullptr, "la copie n'a rien effacé"_fr);

            // Mute + bouton de piste
            appui (Bouton::mute); appui ((Bouton) (premierBoutonDuBas + 1)); relache ((Bouton) (premierBoutonDuBas + 1)); relache (Bouton::mute);
            expect (moteur.getMorceau().pistes[1].mute);
            expectEquals (pilote.calculerEtat().boutons[(size_t) (premierBoutonDuBas + 1)], palette::noir, "piste coupée : bouton éteint"_fr);

            // Quantize fait tourner le calage ; Double Loop double le clip choisi
            appui (Bouton::quantize); relache (Bouton::quantize);
            expect (moteur.getMorceau().calage == Calage::deuxMesures);
            appui (Bouton::select); pad (4, 2); relache (Bouton::select);
            appui (Bouton::doubleLoop); relache (Bouton::doubleLoop);
            expectEquals (moteur.getMorceau().clip (4, 2)->longueurMesures, 2);

            // Stop Clip + piste, puis Shift + Stop Clip
            appui (Bouton::stopClip); appui ((Bouton) premierBoutonDuBas); relache ((Bouton) premierBoutonDuBas); relache (Bouton::stopClip);
            expect (moteur.etatCase (0, 1).arretEnAttente || moteur.etatCase (0, 2).departEnAttente || ! moteur.etatCase (0, 1).joue);

            // Bas fait défiler les scènes quand il y en a plus de 8
            moteur.modifier ([] (Morceau& m) { m.assurerScenes (12); });
            appui (Bouton::bas); relache (Bouton::bas);
            expectEquals (pilote.getPremiereScene(), 1);
            expectEquals (pilote.sceneDeLaLigne (7), 1);
        }

        beginTest ("Mode Note au Push : pads de batterie, pas à pas, Device, Browse"_fr);
        {
            CarteSimulee carte (engine, 2);
            auto m = morceauAvec (120.0, Mesure::standard (4, 4));
            m.clic.actif = false;
            m.pistes[0].kit = kitsDeDepart::kit ("Analogique");
            Moteur moteur (engine, m, &banqueEssai());
            Surface surface;
            Pilote pilote (moteur, surface);
            auto appui = [&] (Bouton b) { surface.recevoir ({ T::bouton, (int) b, 1, true }); };
            auto relache = [&] (Bouton b) { surface.recevoir ({ T::bouton, (int) b, 0, false }); };
            auto bouton = [&] (Bouton b) { appui (b); relache (b); };
            auto padDeNote = [&] (int ligne, int colonne, int v = 100) { surface.recevoir (Evenement::pad (ligne, colonne, v)); surface.recevoir (Evenement::pad (ligne, colonne, 0)); };
            auto pas = [&] (int n) { padDeNote (7 - n / 8, n % 8); };

            // Piste 2 (basse) : le mode Note joue en gamme
            surface.recevoir ({ T::bouton, premierBoutonDuBas + 1, 1, true });
            surface.recevoir ({ T::bouton, premierBoutonDuBas + 1, 0, false });
            bouton (Bouton::note);
            expect (pilote.getMode() == Pilote::Mode::note, "mode Note en gamme sur la basse"_fr);
            bouton (Bouton::session);

            surface.recevoir ({ T::bouton, premierBoutonDuBas, 1, true });
            surface.recevoir ({ T::bouton, premierBoutonDuBas, 0, false });
            bouton (Bouton::note);
            expect (pilote.getMode() == Pilote::Mode::note && pilote.getPage() == Pilote::Page::device);

            // Un pad de batterie sonne et devient la voix choisie (pad 6 : charleston fermé, 2e ligne, 3e colonne)
            carte.jouer (256);
            surface.recevoir (Evenement::pad (1, 2, 120));
            expectEquals (pilote.getPadChoisi(), 6);
            expect (crete (carte.jouer (4800), 0) > 0.05f, "le pad sonne"_fr);
            surface.recevoir (Evenement::pad (1, 2, 0));

            // Pas à pas : pas 0 et pas 4 (2e temps), puis Accent sur le pas 10 ; un clip d'une mesure est créé
            pas (0);
            pas (4);
            appui (Bouton::accent);
            pas (10);
            relache (Bouton::accent);
            const auto* clip = moteur.getMorceau().clip (0, 0);
            expect (clip != nullptr, "clip créé pour le pas à pas"_fr);

            if (clip != nullptr)
            {
                expectEquals ((int) clip->notes.size(), 3);
                expectEquals (pilote.nombreDePas(), 16);
                expectEquals (clip->notes[1].debut, 1.0, "pas 4 = 2e temps"_fr);
                expectEquals (clip->notes[1].hauteur, 42);
                expectEquals (clip->notes[2].velocite, 127, "Accent : vélocité maximale"_fr);
            }

            expectEquals (pilote.calculerEtat().pads[(size_t) (7 * 8 + 4)], palette::indexDePiste (0, 2), "pas allumé"_fr);
            expectEquals (pilote.calculerEtat().pads[(size_t) (6 * 8 + 0)], palette::indexDePiste (0, 0), "pas vide de la mesure : couleur faible"_fr);
            expectEquals (pilote.calculerEtat().pads[(size_t) (5 * 8 + 0)], palette::noir, "au-delà des 16 pas : éteint"_fr);

            pas (4);
            expectEquals ((int) moteur.getMorceau().clip (0, 0)->notes.size(), 2, "rappuyer efface le pas"_fr);

            // Device : la hauteur du pad choisi
            surface.recevoir ({ T::encodeurTourne, 0, 20, false });
            expectWithinAbsoluteError (moteur.getMorceau().pistes[0].kit.pads[6].hauteur, 2.0f, 1.0e-4f);
            surface.recevoir ({ T::encodeurTourne, 6, 40, false });
            expectWithinAbsoluteError (moteur.getMorceau().pistes[0].kit.compression, kitsDeDepart::kit ("Analogique").compression + 0.2f, 1.0e-4f, "compression du kit"_fr);

            // Browse : catégorie Charlestons, deuxième son ; puis un kit entier
            bouton (Bouton::browse);
            expect (pilote.getPage() == Pilote::Page::browse);
            const auto categories = banqueEssai().categories();
            const int indexCharlestons = categories.indexOf ("Charlestons") + 1;     // « Kits » vient en premier
            for (int i = 0; i < indexCharlestons; ++i)
                surface.recevoir ({ T::encodeurTourne, 0, 8, false });     // un cran tous les 8 pas

            surface.recevoir ({ T::encodeurTourne, 1, 8, false });
            expectEquals (moteur.getMorceau().pistes[0].kit.pads[6].son, banqueEssai().sons ("Charlestons")[1]);
            for (int i = 0; i < 20; ++i)
                surface.recevoir ({ T::encodeurTourne, 0, -8, false });

            surface.recevoir ({ T::encodeurTourne, 1, 8, false });
            surface.recevoir ({ T::encodeurTourne, 1, 8, false });
            expectEquals (moteur.getMorceau().pistes[0].kit.nom, juce::String ("Dub"), "Kits : le troisième est Dub"_fr);
            bouton (Bouton::undo);
            expect (moteur.getMorceau().pistes[0].kit.nom != "Dub", "Undo rend le kit d'avant"_fr);

            bouton (Bouton::session);
            expect (pilote.getMode() == Pilote::Mode::session);
        }

        beginTest ("Pas à pas en 7/8 : croches, 7 pas par mesure, pages au-delà de 32 pas"_fr);
        {
            CarteSimulee carte (engine, 2);
            auto m = morceauAvec (120.0, Mesure::standard (7, 8));
            m.assurerScenes (8);
            m.scenes[0].clips[0] = Clip { "Long", 8, {} };
            Moteur moteur (engine, m, &banqueEssai());
            Surface surface;
            Pilote pilote (moteur, surface);
            surface.recevoir ({ T::bouton, (int) Bouton::note, 1, true });
            surface.recevoir ({ T::bouton, (int) Bouton::note, 0, false });
            expectEquals (pilote.longueurDUnPas(), 1.0, "une croche par pas"_fr);
            expectEquals (pilote.nombreDePas(), 56);
            surface.recevoir ({ T::bouton, (int) Bouton::pageDroite, 1, true });
            surface.recevoir ({ T::bouton, (int) Bouton::pageDroite, 0, false });
            surface.recevoir (Evenement::pad (7, 0, 100));
            expectEquals (moteur.getMorceau().clip (0, 0)->notes.front().debut, 32.0, "page 2 : premier pas = 33e croche"_fr);
            expectEquals (pilote.calculerEtat().pads[(size_t) (4 * 8 + 0)], palette::noir, "page 2 : seulement 24 pas"_fr);
        }

        beginTest ("Le Push et le simulateur reçoivent le même état");
        {
            CarteSimulee carte (engine, 2);
            Moteur moteur (engine, morceauDeDemonstration(), &banqueEssai());
            Surface surface;
            ClientEssai push, simulateur;
            surface.ajouterClient (&push);
            surface.ajouterClient (&simulateur);
            Pilote pilote (moteur, surface);

            surface.recevoir (Evenement::pad (7, 0, 100));
            expect (push.etat == simulateur.etat);
            expect (push.ecran == simulateur.ecran, "même image d'écran"_fr);
            expect (moteur.etatCase (0, 0).departEnAttente, "le pad a lancé le clip"_fr);
            expect (push.etat.pads[56] == palette::indexDePiste (0, 3) || push.etat.pads[56] == palette::indexDePiste (0, 0), "le pad clignote en attendant son départ"_fr);
            expect (push.nombre > 1);

            surface.retirerClient (&push);
            surface.retirerClient (&simulateur);
        }

        beginTest ("Sans Push branché, rien ne casse"_fr);
        {
            bool pushPresent = false;

            for (auto& d : juce::MidiInput::getAvailableDevices())
                pushPresent = pushPresent || PushMidi::estUnPortPush (d.name);

            Surface surface;
            {
                EcranPush ecran;
                PushMidi midi (surface);
                surface.ajouterClient (&ecran);
                surface.ajouterClient (&midi);
                surface.publier (EtatSurface {}, juce::Image (juce::Image::RGB, 960, 160, true));
                laisserTravailler (300);

                if (! pushPresent)
                    expect (! midi.estConnecte());

                surface.retirerClient (&ecran);
                surface.retirerClient (&midi);
            }

            logMessage (pushPresent ? "  (un Push est branché : essai fait avec lui)"_fr : "  (aucun Push branché)"_fr);
        }
    }
};

//==============================================================================
class EssaisSession  : public juce::UnitTest
{
public:
    EssaisSession()  : juce::UnitTest ("Session", "groovebox") {}

    /** Morceau à 120 BPM en 4/4, sans décompte : une grosse caisse au premier temps de chaque mesure sur la piste 1,
        un clip de nappe sur la piste 3. Le clic part sur 3-4, la musique sur 1-2 (le casque n'en reçoit presque rien). */
    static Morceau morceauDEssai (Calage calage = Calage::mesure)
    {
        auto m = morceauAvec (120.0, Mesure::standard (4, 4));
        m.calage = calage;
        m.musiqueDansCasqueDb = -60.0f;
        m.assurerScenes (8);
        m.scenes[0].clips[0] = Clip { "GC", 1, { { 0.0, 0.25, notesBatterie::grosseCaisse, 120 } } };
        m.scenes[1].clips[0] = Clip { "GC2", 1, { { 2.0, 0.25, notesBatterie::grosseCaisse, 120 } } };
        m.scenes[0].clips[2] = Clip { "Nappe", 2, { { 0.0, 7.5, 60, 100 } } };
        return m;
    }

    /** Coups de la musique (sorties 1-2) et du clic (sortie 3), mesurés ensemble. */
    struct Ecoute
    {
        DetecteurDeCoups musique, clic;

        void analyser (const juce::AudioBuffer<float>& audio)
        {
            musique.analyser (audio, 0);
            clic.analyser (audio, 2);
        }

        /** Coups du clic qui ouvrent une mesure : un sur quatre en 4/4, depuis le tout premier (la lecture part sur une mesure). */
        std::vector<juce::int64> premiersTemps() const
        {
            std::vector<juce::int64> p;

            for (size_t k = 0; k < clic.coups.size(); k += 4)
                p.push_back (clic.coups[k].echantillon);

            return p;
        }
    };

    static bool proche (juce::int64 a, juce::int64 b, int tolerance = 3)  { return std::abs (a - b) <= tolerance; }

    void runTest() override
    {
        auto& engine = *te::Engine::getEngines()[0];

        beginTest ("Lancer une scène à l'arrêt : les clips partent à la mesure 1, ensemble"_fr);
        {
            CarteSimulee carte (engine, 4);
            auto m = morceauDEssai();
            m.clic.decompte = 1;
            Moteur moteur (engine, m, &banqueEssai());

            moteur.lancerScene (0);
            expect (moteur.etatCase (0, 0).departEnAttente, "la case attend son départ"_fr);
            Ecoute ecoute;

            for (int i = 0; i < 4 * 48; ++i)
            {
                ecoute.analyser (carte.jouer (1000));
                moteur.avancer();
            }

            expect (moteur.etatCase (0, 0).joue && moteur.etatCase (2, 0).joue, "les deux clips jouent"_fr);
            expectEquals (moteur.sceneJouee (0), 0);

            const auto mesures = ecoute.premiersTemps();
            expectEquals ((int) mesures.size(), 2, "une mesure de décompte, puis la mesure 1"_fr);
            expect (! ecoute.musique.coups.empty(), "la grosse caisse ne sonne pas"_fr);

            if (mesures.size() >= 2 && ! ecoute.musique.coups.empty())
                expect (proche (ecoute.musique.coups.front().echantillon, mesures[1]),
                        "la grosse caisse doit tomber sur la mesure 1 (après la mesure de décompte) : "_fr
                        + juce::String (ecoute.musique.coups.front().echantillon) + " au lieu de " + juce::String (mesures[1]));

            auto debut = [&] (int piste) { return moteur.getEdit().getCurrentPlaybackContext() != nullptr
                                                  ? moteur.etatCase (piste, 0).joue : false; };
            expect (debut (0) && debut (2));
        }

        beginTest ("Un clip boucle sans dérive pendant une minute");
        {
            CarteSimulee carte (engine, 4);
            Moteur moteur (engine, morceauDEssai(), &banqueEssai());
            moteur.lancerClip (0, 0);
            Ecoute ecoute;

            // Comme dans l'application, le moteur cale les clips quelques millisecondes après le départ
            ecoute.analyser (carte.jouer (128));
            moteur.avancer();

            for (int i = 0; i < 60; ++i)
                ecoute.analyser (carte.jouer (uneSeconde));

            const auto& coups = ecoute.musique.coups;
            expect (coups.size() >= 29 && coups.size() <= 31, "nombre de coups : "_fr + juce::String ((int) coups.size()));
            double ecartMax = 0.0;

            for (size_t k = 1; k < coups.size(); ++k)
                ecartMax = std::max (ecartMax, std::abs ((double) (coups[k].echantillon - coups[0].echantillon) - (double) k * 96000.0));

            expect (ecartMax <= 1.5, "dérive de la boucle : "_fr + juce::String (ecartMax, 2) + " échantillons"_fr);

            const auto mesures = ecoute.premiersTemps();
            expect (! mesures.empty() && proche (coups.front().echantillon, mesures.front()), "première grosse caisse sur la mesure 1 : "_fr
                    + juce::String (coups.empty() ? -1 : coups.front().echantillon) + " / clic " + juce::String (mesures.empty() ? -1 : mesures.front())
                    + " (" + juce::String ((int) ecoute.clic.coups.size()) + " clics)");
            logMessage ("  " + juce::String ((int) coups.size()) + " boucles, écart maximal "_fr + juce::String (ecartMax, 2) + " échantillon(s)"_fr);
        }

        for (auto [calage, attendu] : { std::pair { Calage::temps, 1 }, std::pair { Calage::mesure, 4 },
                                        std::pair { Calage::deuxMesures, 8 }, std::pair { Calage::quatreMesures, 16 } })
        {
            beginTest ("Lancement en pleine lecture, "_fr + nomDuCalage (calage).toLowerCase());
            CarteSimulee carte (engine, 4);
            Moteur moteur (engine, morceauDEssai (calage), &banqueEssai());
            demarrer (moteur, carte);
            Ecoute ecoute;

            // On attend 5,3 s (mesure 3, deuxième temps), puis on lance la grosse caisse décalée (au troisième temps de son clip)
            for (int i = 0; i < 53; ++i)
                ecoute.analyser (carte.jouer (uneSeconde / 10));

            const auto instantDuLancement = (juce::int64) 128 + 53 * (uneSeconde / 10);
            moteur.lancerClip (0, 1);

            for (int i = 0; i < 120; ++i)
                ecoute.analyser (carte.jouer (uneSeconde / 10));

            // Le clic donne la grille : chaque temps du clic est une limite possible
            const auto& temps = ecoute.clic.coups;
            juce::int64 premierTempsApres = -1;
            int indexDuTemps = -1;

            for (size_t k = 0; k < temps.size(); ++k)
                if (temps[k].echantillon > instantDuLancement)
                {
                    premierTempsApres = temps[k].echantillon;
                    indexDuTemps = (int) k;
                    break;
                }

            // Temps 0 du clic = mesure 1, temps 1. Prochaine limite du calage, comptée depuis la mesure 1
            int limite = indexDuTemps;

            while (limite % attendu != 0)
                ++limite;

            expect (limite < (int) temps.size(), "grille du clic incomplète"_fr);
            const auto departAttendu = temps[(size_t) limite].echantillon + 2 * 24000;   // la note est au 3e temps du clip
            expect (! ecoute.musique.coups.empty() && proche (ecoute.musique.coups.front().echantillon, departAttendu),
                    "départ à "_fr + (ecoute.musique.coups.empty() ? juce::String ("rien") : juce::String (ecoute.musique.coups.front().echantillon))
                    + " au lieu de " + juce::String (departAttendu) + " (temps " + juce::String (limite) + ")");
            juce::ignoreUnused (premierTempsApres);
        }

        beginTest ("Arrêter une piste : elle se tait à la prochaine mesure");
        {
            CarteSimulee carte (engine, 4);
            Moteur moteur (engine, morceauDEssai(), &banqueEssai());
            moteur.lancerClip (0, 0);
            Ecoute ecoute;

            for (int i = 0; i < 45; ++i)
            {
                ecoute.analyser (carte.jouer (uneSeconde / 10));
                moteur.avancer();
            }

            moteur.arreterPiste (0);
            expect (moteur.etatCase (0, 0).arretEnAttente, "l'arrêt attend la mesure"_fr);
            const auto avant = ecoute.musique.coups.size();

            for (int i = 0; i < 60; ++i)
                ecoute.analyser (carte.jouer (uneSeconde / 10));

            expect (ecoute.musique.coups.size() == avant, "plus aucun coup après l'arrêt : "_fr + juce::String ((int) (ecoute.musique.coups.size() - avant)));
            expect (! moteur.etatCase (0, 0).joue);
            expect (moteur.enLecture(), "la lecture continue"_fr);
        }

        beginTest ("Le réglage « musique dans le casque » vaut pour toutes les pistes"_fr);
        {
            CarteSimulee carte (engine, 4);
            auto m = morceauDEssai();
            m.clic.actif = false;
            m.musiqueDansCasqueDb = 0.0f;
            m.compressionDeSortie = 0.0f;   // la sono a son compresseur léger, pas le casque
            Moteur moteur (engine, m, &banqueEssai());
            moteur.lancerClip (0, 0);

            for (int i = 0; i < 10; ++i) { carte.jouer (uneSeconde / 10); moteur.avancer(); }

            auto plein = carte.jouer (2 * uneSeconde);
            expectWithinAbsoluteError (niveauEfficace (plein, 2), niveauEfficace (plein, 0), 0.002f, "à 0 dB, le casque reçoit la musique comme la sono"_fr);

            moteur.modifier ([] (Morceau& mm) { mm.musiqueDansCasqueDb = -20.0f; });
            carte.jouer (uneSeconde / 2);
            auto bas = carte.jouer (2 * uneSeconde);
            expectWithinAbsoluteError (niveauEfficace (bas, 2) / niveauEfficace (bas, 0), 0.1f, 0.01f, "à -20 dB, dix fois moins fort"_fr);
        }

        beginTest ("Mute et Solo");
        {
            CarteSimulee carte (engine, 4);
            Moteur moteur (engine, morceauDEssai(), &banqueEssai());
            moteur.lancerClip (0, 0);

            for (int i = 0; i < 10; ++i) { carte.jouer (uneSeconde / 10); moteur.avancer(); }

            auto niveau = [&] { carte.jouer (uneSeconde / 2); laisserTravailler (20); return niveauEfficace (carte.jouer (2 * uneSeconde), 0); };
            expect (niveau() > 0.01f, "la grosse caisse sonne"_fr);

            moteur.modifier ([] (Morceau& m) { m.pistes[0].mute = true; });
            expect (niveau() < 1.0e-4f, "piste coupée : silence"_fr);

            moteur.modifier ([] (Morceau& m) { m.pistes[0].mute = false; m.pistes[1].solo = true; });
            expect (niveau() < 1.0e-4f, "une autre piste en solo : silence"_fr);

            moteur.modifier ([] (Morceau& m) { m.pistes[0].solo = true; });
            expect (niveau() > 0.01f, "deux pistes en solo : la grosse caisse revient"_fr);
        }

        beginTest ("En coupe, la voix B a son instrument et la copie des clips, mais ne joue pas"_fr);
        {
            CarteSimulee carte (engine, 4);
            Moteur moteur (engine, morceauDeDemonstration(), &banqueEssai());
            moteur.lancerScene (2);

            for (int i = 0; i < 30; ++i) { carte.jouer (uneSeconde / 10); moteur.avancer(); }

            for (int p = 0; p < nombreDePistes; ++p)
            {
                auto b = moteur.voix (p, 1);
                expect (b != nullptr && b->pluginList.getPluginsOfType<InstrumentProvisoire>().size() == 1, "instrument de la voix B"_fr);

                auto casesA = moteur.voix (p, 0)->getClipSlotList().getClipSlots();
                auto casesB = b->getClipSlotList().getClipSlots();

                for (int s = 0; s < casesB.size(); ++s)
                {
                    expect ((casesB[s]->getClip() == nullptr) == (casesA[s]->getClip() == nullptr), "même clip sur les deux voix"_fr);

                    if (auto c = casesB[s]->getClip())
                        expect (c->getLaunchHandle()->getPlayingStatus() != te::LaunchHandle::PlayState::playing, "rien ne joue sur la voix B"_fr);
                }
            }

            expect (moteur.etatCase (3, 2).joue, "la scène Plein joue");
        }

        beginTest ("Annuler et rétablir, mouvements d'encodeur compris"_fr);
        {
            CarteSimulee carte (engine, 2);
            const auto depart = morceauDeDemonstration();
            Moteur moteur (engine, depart, &banqueEssai());

            for (int i = 0; i < 12; ++i)
                moteur.modifier ([] (Morceau& m) { m.tempo += 1.0; }, "tempo");

            moteur.modifier ([] (Morceau& m) { m.scenes[1].clips[1].reset(); });
            moteur.modifier ([] (Morceau& m) { m.pistes[2].mute = true; });
            moteur.modifier ([] (Morceau& m) { m.scenes[0].nom = "Ouverture"; });
            const auto fin = moteur.getMorceau();

            int etapes = 0;

            while (moteur.annuler())
                ++etapes;

            expectEquals (etapes, 4, "12 crans d'encodeur = une seule étape"_fr);
            expect (moteur.getMorceau() == moteur.getMorceau() && moteur.getMorceau().tempo == 120.0);
            expect (moteur.getMorceau().clip (1, 1) != nullptr, "le clip effacé revient"_fr);

            while (moteur.retablir()) {}

            expect (moteur.getMorceau() == fin, "tout est rétabli"_fr);
            expect (moteur.getMorceau().clip (1, 1) == nullptr);
        }

        beginTest ("Bibliothèque : dossiers, copies de secours, renommage, plantage"_fr);
        {
            auto racine = juce::File::createTempFile ("groovebox-morceaux");
            {
                Bibliotheque bib (racine);
                expect (! bib.marquerOuverture(), "première ouverture : pas de plantage"_fr);

                auto m = morceauDeDemonstration();
                expect (bib.enregistrer (m));
                expect (bib.existe ("Démo"_fr));
                expect (bib.ouvrir ("Démo"_fr).has_value() && *bib.ouvrir ("Démo"_fr) == m);
                expectEquals (bib.copiesDeSecours ("Démo"_fr).size(), 1, "une copie de secours"_fr);
                expect (bib.enregistrer (m));
                expectEquals (bib.copiesDeSecours ("Démo"_fr).size(), 1, "pas une copie à chaque sauvegarde"_fr);

                expect (bib.renommer ("Démo"_fr, "Concert"));
                expect (bib.existe ("Concert") && ! bib.existe ("Démo"_fr));
                expectEquals (bib.nomLibre ("Morceau"), juce::String ("Morceau 1"));
                bib.retenirDernier ("Concert");
                expectEquals (bib.dernierOuvert(), juce::String ("Concert"));
            }
            {
                Bibliotheque bib (racine);
                expect (bib.marquerOuverture(), "fermeture brusque repérée"_fr);
                bib.marquerFermeture();
            }
            {
                Bibliotheque bib (racine);
                expect (! bib.marquerOuverture(), "fermeture normale"_fr);
            }
            racine.deleteRecursively();
        }

        beginTest ("Sauvegarder pendant la lecture ne fait aucune coupure");
        {
            CarteSimulee carte (engine, 2);
            Moteur moteur (engine, morceauDeDemonstration(), &banqueEssai());
            auto racine = juce::File::createTempFile ("groovebox-morceaux");
            Bibliotheque bib (racine);
            moteur.lancerScene (2);

            for (int i = 0; i < 30; ++i) { carte.jouer (uneSeconde / 10); moteur.avancer(); }

            float plusFaible = 1.0f;

            for (int i = 0; i < 40; ++i)
            {
                bib.enregistrer (moteur.getMorceau());
                plusFaible = std::min (plusFaible, niveauEfficace (carte.jouer (uneSeconde / 10), 0));
            }

            expect (plusFaible > 0.01f, "le son reste présent pendant les sauvegardes"_fr);
            racine.deleteRecursively();
        }

        beginTest ("Doubler un clip recopie ses notes");
        {
            Clip c { "x", 1, { { 0.0, 0.5, 60, 100 }, { 2.0, 0.5, 62, 100 } } };
            expect (c.doubler (4));
            expectEquals (c.longueurMesures, 2);
            expectEquals ((int) c.notes.size(), 4);
            expectEquals (c.notes[3].debut, 6.0);
            c.longueurMesures = 16;
            expect (! c.doubler (4), "16 mesures au plus");
        }

        beginTest ("Le morceau de démonstration se sauvegarde et se recharge à l'identique"_fr);
        {
            const auto demo = morceauDeDemonstration();
            const auto relu = Morceau::depuisJson (juce::JSON::parse (juce::JSON::toString (demo.versJson())));
            expect (relu == demo);
            expect (demo.scenes.size() >= 8);
            expect (demo.clip (0, 1) != nullptr && demo.clip (3, 2) != nullptr);
        }
    }
};

//==============================================================================
class EssaisBatterie  : public juce::UnitTest
{
public:
    EssaisBatterie()  : juce::UnitTest ("Batterie", "groovebox") {}

    /** Un sinus de 440 Hz d'une seconde, pour mesurer la hauteur et la décroissance. */
    static juce::File fichierSinus()
    {
        auto f = banqueEssai().getRacine().getChildFile ("Essais/Sinus 440.wav");

        if (! f.existsAsFile())
        {
            f.getParentDirectory().createDirectory();
            juce::AudioBuffer<float> b (1, 48000);

            for (int i = 0; i < 48000; ++i)
                b.setSample (0, i, 0.5f * (float) std::sin (juce::MathConstants<double>::twoPi * 440.0 * i / 48000.0));

            juce::WavAudioFormat wav;
            auto flux = f.createOutputStream();
            std::unique_ptr<juce::AudioFormatWriter> e (wav.createWriterFor (flux.release(), 48000.0, 1, 24, {}, 0));
            e->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
        }

        return f;
    }

    /** Morceau dont la piste 1 a un kit où le pad 0 joue le sinus. */
    static Morceau morceauSinus (PadBatterie reglage = {})
    {
        auto m = morceauAvec (120.0, Mesure::standard (4, 4));
        m.clic.actif = false;
        reglage.son = "Essais/Sinus 440.wav";
        m.pistes[0].kit = kitsDeDepart::kit ("Analogique");
        m.pistes[0].kit.pads[0] = reglage;
        m.pistes[0].kit.largeur = 0.0f;
        m.pistes[0].kit.compression = 0.0f;
        return m;
    }

    static juce::AudioBuffer<float> frapper (te::Engine& engine, const Morceau& m, int note, int velocite, int duree = uneSeconde)
    {
        CarteSimulee carte (engine, 2);
        Moteur moteur (engine, m, &banqueEssai());
        carte.jouer (256);
        moteur.jouerNote (0, note, velocite);
        return carte.jouer (duree);
    }

    void runTest() override
    {
        auto& engine = *te::Engine::getEngines()[0];
        fichierSinus();

        beginTest ("Les kits de départ sont fabriqués : 4 kits de 16 sons lisibles"_fr);
        {
            int lus = 0;

            for (auto& nom : kitsDeDepart::noms())
                for (auto& pad : kitsDeDepart::kit (nom).pads)
                {
                    auto* e = banqueEssai().charger (pad.son);
                    expect (e != nullptr, "son manquant : "_fr + pad.son);

                    if (e != nullptr)
                    {
                        expect (e->audio.getNumSamples() > 4800 && e->audio.getMagnitude (0, 0, e->audio.getNumSamples()) > 0.5f, pad.son);
                        expectEquals (e->frequence, 48000.0);
                        ++lus;
                    }
                }

            expectEquals (lus, 64);
            expect (banqueEssai().categories().contains ("Grosses caisses"));
            expect (banqueEssai().sons ("Charlestons").size() >= 8);
        }

        beginTest ("Un pad sonne quand on le frappe, plus fort si on frappe fort");
        {
            const auto fort = frapper (engine, morceauSinus(), 36, 127);
            const auto doux = frapper (engine, morceauSinus(), 36, 40);
            expect (niveauEfficace (fort, 0, 0, 24000) > 0.1f, "le pad sonne"_fr);
            expect (niveauEfficace (doux, 0) < 0.4f * niveauEfficace (fort, 0), "frappe douce : moins fort"_fr);
            expectWithinAbsoluteError (frequenceMesuree (fort, 0, 0, 24000) / 2.0, 440.0, 8.0, "hauteur d'origine");
        }

        beginTest ("Hauteur : +12 demi-tons double la fréquence"_fr);
        {
            PadBatterie p;
            p.hauteur = 12.0f;
            const auto audio = frapper (engine, morceauSinus (p), 36, 127);
            expectWithinAbsoluteError (frequenceMesuree (audio, 0, 0, 12000) / 2.0, 880.0, 15.0);
        }

        beginTest ("Décroissance : le son raccourcit"_fr);
        {
            PadBatterie court;
            court.decroissance = 0.2f;
            const auto plein = frapper (engine, morceauSinus(), 36, 127);
            const auto raccourci = frapper (engine, morceauSinus (court), 36, 127);
            expect (niveauEfficace (plein, 0, 24000, 36000) > 0.1f, "son entier : encore là à 0,5 s"_fr);
            expect (crete (raccourci, 0, 24000, 36000) < 0.001f, "son raccourci : éteint à 0,5 s"_fr);
        }

        beginTest ("Saturation et couleur changent le son sans le faire exploser");
        {
            PadBatterie sature, sombre;
            sature.saturation = 1.0f;
            sombre.couleur = -1.0f;
            const auto a = frapper (engine, morceauSinus (sature), 36, 127);
            const auto b = frapper (engine, morceauSinus (sombre), 36, 127);
            expect (crete (a, 0) <= 1.5f && crete (a, 0) > 0.3f, "saturation contenue"_fr);
            expect (niveauEfficace (b, 0, 0, 24000) < 0.5f * niveauEfficace (frapper (engine, morceauSinus(), 36, 127), 0, 0, 24000), "couleur sombre : le 440 Hz est atténué"_fr);
        }

        beginTest ("Étouffement : le charleston fermé coupe l'ouvert"_fr);
        {
            CarteSimulee carte (engine, 2);
            auto m = morceauAvec (120.0, Mesure::standard (4, 4));
            m.clic.actif = false;
            m.pistes[0].kit = kitsDeDepart::kit ("Analogique");
            m.pistes[0].kit.pads[42 - 36].son = "Essais/Sinus 440.wav";     // le « fermé » devient un sinus très long : facile à repérer
            m.pistes[0].kit.pads[46 - 36].son = "Essais/Sinus 440.wav";
            Moteur moteur (engine, m, &banqueEssai());
            carte.jouer (256);
            moteur.jouerNote (0, 46, 127);
            carte.jouer (4800);
            const auto avant = niveauEfficace (carte.jouer (2400), 0);

            // Un pad hors du groupe ne coupe rien
            moteur.jouerNote (0, 36, 1);
            carte.jouer (2400);
            expect (niveauEfficace (carte.jouer (2400), 0) > 0.5f * avant, "un pad hors du groupe ne coupe pas"_fr);

            moteur.modifier ([] (Morceau& mm) { mm.pistes[0].kit.pads[42 - 36].volumeDb = -40.0f; });
            moteur.jouerNote (0, 42, 127);
            carte.jouer (960);
            expect (niveauEfficace (carte.jouer (2400), 0) < 0.05f * avant, "le fermé (presque muet ici) a coupé l'ouvert"_fr);
        }

        beginTest ("Changer les sons pendant la lecture ne coupe pas le son");
        {
            CarteSimulee carte (engine, 2);
            Moteur moteur (engine, morceauDeDemonstration(), &banqueEssai());
            moteur.lancerScene (2);

            for (int i = 0; i < 30; ++i) { carte.jouer (uneSeconde / 10); moteur.avancer(); }

            float plusFaible = 1.0f, plusFort = 0.0f;

            for (int i = 0; i < 40; ++i)
            {
                const auto kit = kitsDeDepart::noms()[i % 4];
                moteur.modifier ([kit] (Morceau& mm) { mm.pistes[0].kit = kitsDeDepart::kit (kit); });
                auto bloc = carte.jouer (uneSeconde / 10);
                plusFaible = std::min (plusFaible, niveauEfficace (bloc, 0));
                plusFort = std::max (plusFort, crete (bloc, 0));
            }

            expect (plusFaible > 0.005f, "le son reste présent"_fr);
            expect (plusFort < 2.0f && std::isfinite (plusFort), "pas d'explosion du niveau"_fr);
        }

        beginTest ("Le kit se sauvegarde avec le morceau");
        {
            auto m = morceauDeDemonstration();
            m.pistes[0].kit.pads[3].hauteur = -5.5f;
            m.pistes[0].kit.pads[3].son = "Claps/Dub - Clap.wav";
            m.pistes[0].kit.compression = 0.7f;
            const auto relu = Morceau::depuisJson (juce::JSON::parse (juce::JSON::toString (m.versJson())));
            expect (relu == m);
            expect (relu.pistes[0].kit.pads[42 - 36].etouffement == 1);
        }

        beginTest ("Position dans le clip qui joue, pour le pas à pas");
        {
            CarteSimulee carte (engine, 2);
            Moteur moteur (engine, morceauDeDemonstration(), &banqueEssai());
            moteur.modifier ([] (Morceau& mm) { mm.clic.decompte = 0; });
            moteur.lancerClip (1, 1);       // basse, 2 mesures
            carte.jouer (128);
            moteur.avancer();
            expect (! moteur.positionDansLeClip (2).has_value(), "rien ne joue sur la piste 3"_fr);
            carte.jouer (uneSeconde * 5);   // 10 temps : 2 temps dans la 2e boucle... la boucle fait 8 temps
            const auto pos = moteur.positionDansLeClip (1);
            expect (pos.has_value());
            expectWithinAbsoluteError (pos.value_or (-1.0), 2.0, 0.2, "10 temps joués dans une boucle de 8"_fr);
        }
    }
};

//==============================================================================
class EssaisGenerateurs  : public juce::UnitTest
{
public:
    EssaisGenerateurs()  : juce::UnitTest ("Générateurs"_fr, "groovebox") {}

    static juce::String motif (const std::vector<bool>& m)
    {
        juce::String s;

        for (auto b : m)
            s << (b ? "x" : ".");

        return s;
    }

    static int compter (const std::vector<Note>& notes, int hauteur)
    {
        return (int) std::count_if (notes.begin(), notes.end(), [hauteur] (const Note& n) { return n.hauteur == hauteur; });
    }

    void runTest() override
    {
        using namespace generateurs;
        auto& engine = *te::Engine::getEngines()[0];

        beginTest ("Euclidien : les motifs connus");
        expectEquals (motif (euclide (3, 8)), juce::String ("x..x..x."));
        expectEquals (motif (euclide (5, 8)), juce::String ("x.x.xx.x"), "5 sur 8 (une rotation de x.xx.xx.)");
        expectEquals (motif (euclide (4, 16)), juce::String ("x...x...x...x..."));
        expectEquals (motif (euclide (3, 8, 1)), juce::String (".x..x..x"), "rotation d'un pas"_fr);
        expectEquals (motif (euclide (0, 7)), juce::String ("......."));
        expectEquals (motif (euclide (7, 7)), juce::String ("xxxxxxx"));

        beginTest ("Carte de grooves : fidèle aux nœuds de Grids"_fr);
        {
            // Comme dans Grids, le mélange divise par 256 : même sans mélange, 145 ressort à 143
            auto commeGrids = [] (int a) { return (((a * 255) >> 8) * 255) >> 8; };

            // X = Y = 0 : le nœud du coin (10)
            for (int pas = 0; pas < 32; ++pas)
                for (int v = 0; v < 3; ++v)
                    expectEquals (niveauGrooves (pas, 0.0f, 0.0f)[(size_t) v], commeGrids (carteDeGrooves::noeuds[10][(size_t) (v * 32 + pas)]));

            // Au milieu exact de la carte (X = Y = 128) : le nœud central (4)
            expectEquals (niveauGrooves (0, 128.0f / 255.0f, 128.0f / 255.0f)[0], commeGrids (carteDeGrooves::noeuds[4][0]));

            // À mi-chemin entre deux nœuds en X (X = 32) : la moyenne pondérée de Grids
            const int a = carteDeGrooves::noeuds[10][5 * 0 + 6], b = carteDeGrooves::noeuds[15][6];
            const int ab = (a * (255 - 128) + b * 128) >> 8;
            expectEquals (niveauGrooves (6, 32.0f / 255.0f, 0.0f)[0], (ab * 255) >> 8);
            expect (carteDeGroovesDisponible (Mesure::standard (4, 4)) && ! carteDeGroovesDisponible (Mesure::standard (7, 8)));
        }

        beginTest ("Même réglages, même graine : même rythme ; le chaos dépend de la graine");
        {
            auto g = Generateur::parDefaut (MoteurRythmique::grooves, Mesure::standard (4, 4));
            g.chaos = 0.8f;
            const auto a = generer (g, Mesure::standard (4, 4), 4), b = generer (g, Mesure::standard (4, 4), 4);
            expect (a == b, "reproductible"_fr);
            auto g2 = g;
            g2.graine = 77;
            expect (generer (g2, Mesure::standard (4, 4), 4) != a, "autre graine, autre rythme avec du chaos"_fr);
            g.chaos = g2.chaos = 0.0f;
            expect (generer (g, Mesure::standard (4, 4), 4) == generer (g2, Mesure::standard (4, 4), 4), "sans chaos, la graine ne change rien"_fr);
        }

        beginTest ("Densités : 0 = silence, 1 = beaucoup de coups"_fr);
        {
            auto g = Generateur::parDefaut (MoteurRythmique::grooves, Mesure::standard (4, 4));
            g.densites = { 0.0f, 0.0f, 0.0f };
            expect (generer (g, Mesure::standard (4, 4), 1).empty());
            g.densites = { 1.0f, 1.0f, 1.0f };
            expect (compter (generer (g, Mesure::standard (4, 4), 1), 42) >= 8);
        }

        for (auto& mesure : Mesure::mesuresProposees())
            for (auto moteurR : { MoteurRythmique::euclidien, MoteurRythmique::maison, MoteurRythmique::grooves })
            {
                if (moteurR == MoteurRythmique::grooves && ! carteDeGroovesDisponible (mesure))
                    continue;

                beginTest ("Rythme jouable dès le départ : "_fr + nomDuMoteur (moteurR) + " en " + mesure.nom());
                const auto notes = generer (Generateur::parDefaut (moteurR, mesure), mesure, 2);
                const auto longueur = 2.0 * mesure.numerateur;
                expect (compter (notes, 36) >= 2, "grosse caisse"_fr);
                expect (compter (notes, 38) >= 1 || compter (notes, 39) >= 1, "caisse claire"_fr);
                expect (compter (notes, 42) >= 2, "charleston"_fr);
                expect (std::any_of (notes.begin(), notes.end(), [] (const Note& n) { return n.hauteur == 36 && n.debut == 0.0; }), "grosse caisse sur le premier temps"_fr);

                bool surLaGrille = true, dansLeClip = true;

                for (auto& n : notes)
                {
                    const auto indice = n.debut / pas (mesure);
                    surLaGrille = surLaGrille && std::abs (indice - std::round (indice)) < 1.0e-9;
                    dansLeClip = dansLeClip && n.debut >= 0.0 && n.debut < longueur;
                }

                expect (surLaGrille, "notes sur la grille des double-croches"_fr);
                expect (dansLeClip, "notes dans le clip"_fr);
            }

        beginTest ("Carte maison : 9 rythmes de référence distincts dans chaque mesure");
        for (auto& mesure : Mesure::mesuresProposees())
        {
            std::set<juce::String> rythmes;

            for (float x : { 0.0f, 0.5f, 1.0f })
                for (float y : { 0.0f, 0.5f, 1.0f })
                {
                    auto g = Generateur::parDefaut (MoteurRythmique::maison, mesure);
                    g.x = x;
                    g.y = y;
                    juce::String empreinte;

                    for (auto& n : generer (g, mesure, 1))
                        empreinte << n.hauteur << "@" << n.debut << " ";

                    rythmes.insert (empreinte);
                }

            expect (rythmes.size() >= 8, mesure.nom() + " : " + juce::String ((int) rythmes.size()) + " rythmes distincts");
        }

        beginTest ("Humanisation : jamais en avance, vélocités variées"_fr);
        {
            auto g = Generateur::parDefaut (MoteurRythmique::euclidien, Mesure::standard (4, 4));
            const auto droit = generer (g, Mesure::standard (4, 4), 1);
            g.humanisation = 1.0f;
            const auto humain = generer (g, Mesure::standard (4, 4), 1);
            expectEquals ((int) humain.size(), (int) droit.size());
            bool jamaisEnAvance = true, varie = false;

            for (size_t i = 0; i < humain.size(); ++i)
            {
                const auto grille = std::floor (humain[i].debut / 0.25 + 1.0e-9) * 0.25;
                jamaisEnAvance = jamaisEnAvance && humain[i].debut >= grille && humain[i].debut < grille + 0.25;
                varie = varie || humain[i].velocite != droit[i].velocite;
            }

            expect (jamaisEnAvance && varie);
        }

        beginTest ("Roulement toutes les 2 mesures");
        {
            auto g = Generateur::parDefaut (MoteurRythmique::euclidien, Mesure::standard (4, 4));
            g.roulement = 2;
            const auto notes = generer (g, Mesure::standard (4, 4), 4);
            auto caissesDansLaFin = [&] (int mesure) { return (int) std::count_if (notes.begin(), notes.end(), [mesure] (const Note& n) {
                return n.hauteur == 38 && n.debut > mesure * 4 + 3.0 && n.debut < mesure * 4 + 4.0; }); };   // après la caisse claire du 4e temps
            expectEquals (caissesDansLaFin (0), 0);
            expectEquals (caissesDansLaFin (1), 3, "roulement en double-croches à la fin de la 2e mesure"_fr);
            expectEquals (caissesDansLaFin (2), 0);
            expectEquals (caissesDansLaFin (3), 3);
        }

        beginTest ("Varier : à zéro rien ne change ; sinon le rythme bouge, toujours dans le clip");
        {
            const auto m = Mesure::standard (4, 4);
            const auto depart = generer (Generateur::parDefaut (MoteurRythmique::grooves, m), m, 1);
            expect (varier (depart, { 0.0f, 0.0f, 0, false }, 5, m, 1) == depart, "variation nulle"_fr);
            const auto varie = varier (depart, { 0.5f, 0.3f, 0, false }, 5, m, 1);
            expect (varie != depart);
            expect (varier (depart, { 0.5f, 0.3f, 0, false }, 5, m, 1) == varie, "même graine, même variation"_fr);
            expect (std::any_of (varie.begin(), varie.end(), [] (const Note& n) { return n.hauteur == 36 && n.debut == 0.0; }), "le premier temps reste");
            const auto tourne = varier (depart, { 0.0f, 0.0f, 4, false }, 5, m, 1);
            expect (std::all_of (tourne.begin(), tourne.end(), [] (const Note& n) { return n.debut < 4.0; }));
        }

        beginTest ("Le générateur se sauvegarde avec le morceau ; figer garde les notes"_fr);
        {
            auto m = morceauDeDemonstration();
            Clip c { "Euclidien", 2, {} };
            c.generateur = Generateur::parDefaut (MoteurRythmique::euclidien, m.mesure);
            c.generateur->voix[2].probabilite = 0.7f;
            c.regenerer (m.mesure, m.gamme);
            m.scenes[6].clips[0] = c;
            m.swing = 0.3f;
            m.pistes[0].swing = 0.5f;
            const auto relu = Morceau::depuisJson (juce::JSON::parse (juce::JSON::toString (m.versJson())));
            expect (relu == m);

            auto fige = c;
            fige.figer();
            expect (! fige.generateur && fige.notes == c.notes);
        }

        beginTest ("Swing : les double-croches du contretemps sont retardées à la lecture seulement");
        {
            CarteSimulee carte (engine, 2);
            auto m = morceauAvec (120.0, Mesure::standard (4, 4));
            m.assurerScenes (8);
            Clip c { "Charleston", 1, {} };

            for (int i = 0; i < 16; ++i)
                c.notes.push_back ({ i * 0.25, 0.1, 42, 90 });

            m.scenes[0].clips[0] = c;
            Moteur moteur (engine, m, &banqueEssai());
            expectEquals (moteur.debutsJoues (0, 0)[1], 0.25, "sans swing"_fr);

            moteur.modifier ([] (Morceau& mm) { mm.swing = 0.4f; });
            const auto avecSwing = moteur.debutsJoues (0, 0);
            expectWithinAbsoluteError (avecSwing[1], 0.25 + 0.4 * 0.25, 1.0e-6, "contretemps retardé"_fr);
            expectEquals (avecSwing[2], 0.5, "temps fort inchangé"_fr);
            expectEquals (moteur.getMorceau().clip (0, 0)->notes[1].debut, 0.25, "le morceau garde la grille"_fr);

            moteur.modifier ([] (Morceau& mm) { mm.pistes[0].swing = 0.0f; });
            expectEquals (moteur.debutsJoues (0, 0)[1], 0.25, "swing propre à la piste : 0"_fr);
        }

        beginTest ("Au Push : trois encodeurs donnent un rythme, dans chaque mesure");
        for (auto& mesure : Mesure::mesuresProposees())
        {
            CarteSimulee carte (engine, 2);
            Moteur moteur (engine, morceauAvec (120.0, mesure), &banqueEssai());
            Surface surface;
            Pilote pilote (moteur, surface);
            auto bouton = [&] (Bouton b) { surface.recevoir ({ Evenement::Type::bouton, (int) b, 1, true }); surface.recevoir ({ Evenement::Type::bouton, (int) b, 0, false }); };

            bouton (Bouton::note);
            bouton (Bouton::device);    // page Générateur
            expect (pilote.getPage() == Pilote::Page::generateur);

            surface.recevoir ({ Evenement::Type::encodeurTourne, 0, 30, false });   // X
            surface.recevoir ({ Evenement::Type::encodeurTourne, 1, -20, false });  // Y
            surface.recevoir ({ Evenement::Type::encodeurTourne, 4, 15, false });   // densité du charleston

            const auto* clip = moteur.getMorceau().clip (0, 0);
            expect (clip != nullptr && clip->generateur.has_value(), mesure.nom() + " : clip généré"_fr);

            if (clip != nullptr)
            {
                expect (compter (clip->notes, 36) >= 1 && compter (clip->notes, 42) >= 2, mesure.nom() + " : rythme jouable"_fr);
                expectEquals (pilote.nombreDePas(), mesure.numerateur * mesure.grilleParDefaut() / mesure.denominateur);
            }

            // Convert fige ; Shift + Duplicate duplique en variant
            bouton (Bouton::convert);
            expect (! moteur.getMorceau().clip (0, 0)->generateur.has_value(), "figé"_fr);
            surface.recevoir ({ Evenement::Type::bouton, (int) Bouton::shift, 1, true });
            bouton (Bouton::duplicate);
            surface.recevoir ({ Evenement::Type::bouton, (int) Bouton::shift, 0, false });
            const auto* copie = moteur.getMorceau().clip (0, 1);
            expect (copie != nullptr && copie->nom.endsWith ("var."), "dupliqué et varié en dessous"_fr);
        }
    }
};

//==============================================================================
class EssaisGammes  : public juce::UnitTest
{
public:
    EssaisGammes()  : juce::UnitTest ("Gammes", "groovebox") {}

    void runTest() override
    {
        auto& engine = *te::Engine::getEngines()[0];

        beginTest ("Notes des gammes");
        {
            Gamme doMajeur { 0, Gamme::Mode::majeur };
            expectEquals (doMajeur.note (0, 3), 48, "do 3 = MIDI 48"_fr);
            expectEquals (doMajeur.note (7, 3), 60, "le 8e degré monte d'une octave"_fr);
            expectEquals (doMajeur.note (-1, 3), 47, "en dessous : si 2"_fr);
            Gamme laMineur { 9, Gamme::Mode::mineur };
            expectEquals (laMineur.note (2, 2), 48, "la mineur : 3e degré = do"_fr);
            expect (laMineur.contient (60) && ! laMineur.contient (61));
            Gamme penta { 2, Gamme::Mode::pentatoniqueMineure };
            expectEquals (penta.nombreDeDegres(), 5);
            expectEquals (penta.note (5, 0), 26, "ré pentatonique : la 6e note est le ré suivant"_fr);
            expectEquals (Gamme { 7, Gamme::Mode::dorien }.nom(), juce::String ("Sol dorien"));

            for (int f = 0; f < 12; ++f)
                for (int mode = 0; mode < 7; ++mode)
                {
                    Gamme g { f, (Gamme::Mode) mode };

                    for (int d = -10; d < 30; ++d)
                        expect (g.contient (g.note (d, 3)), g.nom() + " : degré " + juce::String (d));
                }
        }

        beginTest ("Jeu en gamme au Push : aucune fausse note, fondamentales en couleur"_fr);
        {
            CarteSimulee carte (engine, 2);
            auto m = morceauAvec (120.0, Mesure::standard (4, 4));
            m.gamme = { 2, Gamme::Mode::dorien };
            Moteur moteur (engine, m, &banqueEssai());
            Surface surface;
            Pilote pilote (moteur, surface);
            auto bouton = [&] (Bouton b) { surface.recevoir ({ Evenement::Type::bouton, (int) b, 1, true }); surface.recevoir ({ Evenement::Type::bouton, (int) b, 0, false }); };

            bouton ((Bouton) (premierBoutonDuBas + 1));    // la basse
            bouton (Bouton::note);
            expect (pilote.getMode() == Pilote::Mode::note);

            const auto gamme = moteur.getMorceau().gamme;
            std::set<int> notes;

            for (int ligne = 0; ligne < 8; ++ligne)
                for (int colonne = 0; colonne < 8; ++colonne)
                {
                    const int note = pilote.noteDuPad (ligne, colonne);
                    expect (gamme.contient (note), "pad hors gamme"_fr);
                    notes.insert (note);
                    const auto attendu = palette::indexDePiste (1, gamme.estFondamentale (note) ? 3 : 1);
                    expectEquals (pilote.calculerEtat().pads[(size_t) (ligne * 8 + colonne)], attendu);
                }

            expectEquals (pilote.noteDuPad (0, 0), gamme.note (0, pilote.getOctave()), "en bas à gauche : la fondamentale"_fr);
            expectEquals (pilote.noteDuPad (1, 0), pilote.noteDuPad (0, 3), "une rangée plus haut : une quarte (3 degrés)"_fr);
            expectEquals ((int) notes.size(), 29, "8 sur 8 en quartes : 29 degrés, de la fondamentale à 4 octaves plus haut"_fr);

            const int avant = pilote.noteDuPad (0, 0);
            bouton (Bouton::octaveHaut);
            expectEquals (pilote.noteDuPad (0, 0), avant + 12);

            // Un pad joue sa note (son provisoire de la basse en attendant Surge)
            carte.jouer (256);
            surface.recevoir (Evenement::pad (0, 0, 110));
            expect (crete (carte.jouer (9600), 0) > 0.01f, "la note sonne"_fr);
            expectEquals (pilote.calculerEtat().pads[0], palette::blanc, "pad tenu en blanc"_fr);
            surface.recevoir (Evenement::pad (0, 0, 0));

            // Scale : la fondamentale change pour tout le morceau
            bouton (Bouton::scale);
            expect (pilote.getPage() == Pilote::Page::gamme);
            surface.recevoir ({ Evenement::Type::encodeurTourne, 0, 8, false });
            expectEquals (moteur.getMorceau().gamme.fondamentale, 3);
            surface.recevoir ({ Evenement::Type::encodeurTourne, 1, -8, false });
            expect (moteur.getMorceau().gamme.mode == Gamme::Mode::mineur);
        }
    }
};

//==============================================================================
class EssaisSurge  : public juce::UnitTest
{
public:
    EssaisSurge()  : juce::UnitTest ("Surge XT", "groovebox") {}

    /** Brillance : part de l'énergie dans les variations rapides du signal. Plus c'est haut, plus le son est brillant. */
    static double brillance (const juce::AudioBuffer<float>& audio, int debut, int fin)
    {
        double energie = 0.0, variations = 0.0;
        auto d = audio.getReadPointer (0);

        for (int i = debut + 1; i < fin; ++i)
        {
            energie += d[i] * d[i];
            variations += (d[i] - d[i - 1]) * (d[i] - d[i - 1]);
        }

        return energie > 0.0 ? variations / energie : 0.0;
    }

    /** Joue une note d'une seconde sur la basse avec un son donné, macros de départ. */
    static juce::AudioBuffer<float> jouer (te::Engine& engine, const juce::String& son, int note = 48)
    {
        CarteSimulee carte (engine, 2);
        auto m = morceauAvec (120.0, Mesure::standard (4, 4));
        m.clic.actif = false;
        m.pistes[1].sonDeSynthe = son;

        if (auto s = sonsDeSynthe::trouver (son))
            for (int k = 0; k < 8; ++k)
                m.pistes[1].macros[(size_t) k] = s->macros[(size_t) k].depart;

        Moteur moteur (engine, m, &banqueEssai());
        carte.jouer (2048);
        moteur.jouerNote (1, note, 100);
        auto audio = carte.jouer (uneSeconde);
        moteur.relacherNote (1, note);
        return audio;
    }

    void runTest() override
    {
        auto& engine = *te::Engine::getEngines()[0];

        surge::autoriser (true);

        if (! surge::trouver (engine))
        {
            logMessage ("  Surge XT n'est pas installé : essais de Surge sautés"_fr);
            surge::autoriser (false);
            return;
        }

        beginTest ("Les pistes de synthé jouent avec Surge XT, le Surge de la voix libre se repose"_fr);
        {
            CarteSimulee carte (engine, 2);
            Moteur moteur (engine, morceauDeDemonstration(), &banqueEssai());

            for (int p = 0; p < nombreDePistes; ++p)
            {
                const bool synthe = ! moteur.getMorceau().pistes[(size_t) p].estUneBatterie();
                expect (moteur.joueAvecSurge (p) == synthe, moteur.getMorceau().pistes[(size_t) p].nom);

                if (synthe)
                {
                    auto surges = moteur.voix (p, 1)->pluginList.getPluginsOfType<te::ExternalPlugin>();
                    expect (surges.size() == 1 && ! surges[0]->isEnabled(), "Surge de la voix B au repos");
                }
            }

            expectEquals (moteur.getMorceau().pistes[1].sonDeSynthe, juce::String ("Sub dub"), "son par défaut de la basse"_fr);
        }

        beginTest ("Fondu sur une piste de synthé : le Surge de la voix B prend le relais avec le même son"_fr);
        {
            CarteSimulee carte (engine, 2);
            auto m = morceauAvec (120.0, Mesure::standard (4, 4));
            m.clic.actif = false;
            m.assurerScenes (2);
            m.scenes[0].clips[2] = Clip { "Do", 1, { Note { 0.0, 4.0, 60, 100 } } };
            m.scenes[1].clips[2] = Clip { "Sol", 1, { Note { 0.0, 4.0, 67, 100 } } };
            Moteur moteur (engine, m, &banqueEssai());
            auto surgeB = moteur.voix (2, 1)->pluginList.getPluginsOfType<te::ExternalPlugin>();
            expect (surgeB.size() == 1);

            moteur.lancerScene (0);

            for (int i = 0; i < 100; ++i) { carte.jouer (1200); moteur.avancer(); }

            const auto avant = carte.jouer (uneSeconde / 2);
            Enchainement e;
            e.type = TypeEnchainement::fondu;
            e.mesures = 1;
            moteur.lancerScene (1, e);
            expect (surgeB.size() == 1 && surgeB[0]->isEnabled(), "le Surge de la voix B se réveille");

            for (int i = 0; i < 160; ++i) { carte.jouer (1200); moteur.avancer(); }

            expectEquals (moteur.voixCourante (2), 1);
            expect (moteur.joueAvecSurge (2), "la piste joue avec le Surge de la voix B");
            const auto apres = carte.jouer (uneSeconde / 2);
            logMessage ("  niveau avant " + juce::String (niveauEfficace (avant, 0), 3) + ", après "_fr + juce::String (niveauEfficace (apres, 0), 3));
            expect (niveauEfficace (apres, 0) > 0.5f * niveauEfficace (avant, 0), "le nouveau clip sonne aussi fort, avec Surge");

            auto surgeA = moteur.voix (2, 0)->pluginList.getPluginsOfType<te::ExternalPlugin>();
            expect (surgeA.size() == 1 && ! surgeA[0]->isEnabled(), "le Surge de la voix A se repose à son tour");
        }

        beginTest ("Les 24 sons de départ s'appliquent sans réglage manquant"_fr);
        {
            CarteSimulee carte (engine, 2);
            Moteur moteur (engine, morceauAvec (120.0, Mesure::standard (4, 4)), &banqueEssai());
            auto* pilote = moteur.piloteSurge (1);
            expect (pilote != nullptr);
            expectEquals ((int) sonsDeSynthe::tous().size(), 24);
            expectEquals (sonsDeSynthe::styles().size(), 8);

            if (pilote != nullptr)
                for (auto& son : sonsDeSynthe::tous())
                {
                    std::array<float, 8> macros {};

                    for (int k = 0; k < 8; ++k)
                        macros[(size_t) k] = son.macros[(size_t) k].depart;

                    expect (pilote->appliquerSon (son, macros), son.nom);

                    for (auto& macro : son.macros)
                        for (auto& l : macro.liaisons)
                            expect (pilote->valeurPourNombre (l.parametre, l.de).has_value(), son.nom + " : " + l.parametre);
                }
        }

        beginTest ("Une macro parcourt sa plage : l'ouverture de l'acid va de 120 à 3000 Hz");
        {
            CarteSimulee carte (engine, 2);
            auto m = morceauAvec (120.0, Mesure::standard (4, 4));
            m.pistes[1].sonDeSynthe = "Acid 303";
            Moteur moteur (engine, m, &banqueEssai());
            auto* pilote = moteur.piloteSurge (1);
            expectEquals (pilote->texteActuel ("A Filter 1 Type"), juce::String ("LP Diode Ladder"));

            moteur.modifier ([] (Morceau& mm) { mm.pistes[1].macros[0] = 0.0f; });
            expectWithinAbsoluteError (pilote->texteActuel ("A Filter 1 Cutoff").getDoubleValue(), 120.0, 6.0);
            moteur.modifier ([] (Morceau& mm) { mm.pistes[1].macros[0] = 1.0f; });
            expectWithinAbsoluteError (pilote->texteActuel ("A Filter 1 Cutoff").getDoubleValue(), 3000.0, 100.0);
            moteur.modifier ([] (Morceau& mm) { mm.pistes[1].macros[0] = 0.5f; });
            const auto milieu = pilote->texteActuel ("A Filter 1 Cutoff").getDoubleValue();
            expect (milieu > 500.0 && milieu < 700.0, "à mi-course, à mi-chemin à l'oreille (en demi-tons) : "_fr + juce::String (milieu));
        }

        beginTest ("Un son de Surge joue, et une basse sub est plus ronde qu'une reese");
        {
            const auto sub = jouer (engine, "Sub dub");
            const auto reese = jouer (engine, "Reese");
            expect (niveauEfficace (sub, 0, 4800, 24000) > 0.02f, "la basse sub sonne"_fr);
            expect (niveauEfficace (reese, 0, 4800, 24000) > 0.02f, "la reese sonne"_fr);
            const auto bSub = brillance (sub, 4800, 24000), bReese = brillance (reese, 4800, 24000);
            expect (bReese > 1.5 * bSub, "reese plus brillante (l écart varie, Surge désaccorde au hasard) : "_fr + juce::String (bReese, 5) + " contre " + juce::String (bSub, 5));
            logMessage ("  brillance : sub " + juce::String (bSub, 5) + ", reese " + juce::String (bReese, 5));
        }

        beginTest ("Le son de synthé et ses macros se sauvegardent avec le morceau");
        {
            auto m = morceauDeDemonstration();
            m.pistes[1].sonDeSynthe = "Reese";
            m.pistes[1].macros[3] = 0.77f;
            const auto relu = Morceau::depuisJson (juce::JSON::parse (juce::JSON::toString (m.versJson())));
            expectEquals (relu.pistes[1].sonDeSynthe, juce::String ("Reese"));
            expectWithinAbsoluteError (relu.pistes[1].macros[3], 0.77f, 1.0e-6f);
        }

        beginTest ("Au Push : Device montre les macros du son, Browse change de son");
        {
            CarteSimulee carte (engine, 2);
            Moteur moteur (engine, morceauAvec (120.0, Mesure::standard (4, 4)), &banqueEssai());
            Surface surface;
            Pilote pilote (moteur, surface);
            auto bouton = [&] (Bouton b) { surface.recevoir ({ Evenement::Type::bouton, (int) b, 1, true }); surface.recevoir ({ Evenement::Type::bouton, (int) b, 0, false }); };

            bouton ((Bouton) (premierBoutonDuBas + 1));
            bouton (Bouton::device);
            expect (pilote.getPage() == Pilote::Page::macros);
            pilote.rafraichir();
            const auto avant = moteur.getMorceau().pistes[1].macros[0];
            surface.recevoir ({ Evenement::Type::encodeurTourne, 0, 20, false });
            expectWithinAbsoluteError (moteur.getMorceau().pistes[1].macros[0], avant + 0.1f, 1.0e-5f, "macro 1 tournée"_fr);

            bouton (Bouton::browse);
            const int indexAcid = sonsDeSynthe::styles().indexOf ("Acid");

            for (int i = 0; i < indexAcid; ++i)
                surface.recevoir ({ Evenement::Type::encodeurTourne, 0, 8, false });

            surface.recevoir ({ Evenement::Type::encodeurTourne, 1, 8, false });
            expectEquals (moteur.getMorceau().pistes[1].sonDeSynthe, sonsDeSynthe::sonsDuStyle ("Acid")[1]);
            expectEquals (moteur.piloteSurge (1)->texteActuel ("A Play Mode"), juce::String ("Mono (Fingered Portamento)"));
            bouton (Bouton::undo);
            expectEquals (moteur.getMorceau().pistes[1].sonDeSynthe, juce::String ("Sub dub"), "Undo rend le son d'avant"_fr);
        }

        beginTest ("Temps de calcul : la démo avec ses six synthés Surge");
        {
            CarteSimulee carte (engine, 2);
            Moteur moteur (engine, morceauDeDemonstration(), &banqueEssai());
            moteur.modifier ([] (Morceau& mm) { mm.clic.decompte = 0; });
            moteur.lancerScene (4);
            carte.jouer (128);
            moteur.avancer();
            carte.jouer (uneSeconde);

            const auto debut = juce::Time::getMillisecondCounterHiRes();
            const auto audio = carte.jouer (10 * uneSeconde);
            const auto duree = juce::Time::getMillisecondCounterHiRes() - debut;
            expect (niveauEfficace (audio, 0) > 0.01f, "la scène sonne"_fr);
            expect (duree < 3000.0, "10 s de son calculées en " + juce::String (duree / 1000.0, 2) + " s");
            logMessage ("  10 s de la scène Retour calculées en " + juce::String (duree / 1000.0, 2) + " s, soit "
                        + juce::String (100.0 * duree / 10000.0, 1) + " % du temps réel, sur un seul fil"_fr);
            logMessage ("  crête de la démo : " + juce::String (juce::Decibels::gainToDecibels (crete (audio, 0)), 1) + " dB");
            expect (crete (audio, 0) < 1.0f, "la démo ne doit pas saturer : crête "_fr + juce::String (juce::Decibels::gainToDecibels (crete (audio, 0)), 1) + " dB");
        }

        surge::autoriser (false);
    }
};

//==============================================================================
class EssaisMelodies  : public juce::UnitTest
{
public:
    EssaisMelodies()  : juce::UnitTest ("Mélodies"_fr, "groovebox") {}

    void runTest() override
    {
        using namespace melodies;
        auto& engine = *te::Engine::getEngines()[0];
        const auto quatre = Mesure::standard (4, 4);
        const Gamme doMineur { 0, Gamme::Mode::mineur };

        beginTest ("Même graine, même phrase ; autre graine, autre phrase");
        {
            auto g = GenerateurMelodique::parDefaut (StyleMelodique::basseDnb);
            g.repetition = 0.0f;
            const auto a = generer (g, quatre, doMineur, 4);
            expect (a == generer (g, quatre, doMineur, 4));
            g.graine = 99;
            expect (generer (g, quatre, doMineur, 4) != a);
        }

        for (int style = 0; style < 7; ++style)
            for (auto& mesure : Mesure::mesuresProposees())
            {
                beginTest (nomDuStyle ((StyleMelodique) style) + " en " + mesure.nom() + " : dans la gamme, dans le registre, dans le clip");
                const auto g = GenerateurMelodique::parDefaut ((StyleMelodique) style);

                for (auto gamme : { doMineur, Gamme { 7, Gamme::Mode::dorien }, Gamme { 2, Gamme::Mode::pentatoniqueMineure } })
                {
                    const auto notes = generer (g, mesure, gamme, 2);
                    expect (notes.size() >= 2, "au moins deux notes"_fr);

                    for (auto& n : notes)
                    {
                        expect (gamme.contient (n.hauteur), "fausse note"_fr);
                        expect (n.hauteur >= gamme.note (-3, g.registre) && n.hauteur <= gamme.note (2 * gamme.nombreDeDegres() + 6, g.registre), "hors du registre"_fr);
                        expect (n.debut >= 0.0 && n.debut < 2.0 * mesure.numerateur && n.duree > 0.0, "hors du clip"_fr);
                    }
                }
            }

        beginTest ("Basse dub : fondamentale et quinte, notes longues, sur le premier temps");
        {
            const auto notes = generer (GenerateurMelodique::parDefaut (StyleMelodique::basseDub), quatre, doMineur, 4);
            int fondaOuQuinte = 0;
            double dureeMoyenne = 0.0;

            for (auto& n : notes)
            {
                const int classe = n.hauteur % 12;
                fondaOuQuinte += (classe == 0 || classe == 7) ? 1 : 0;
                dureeMoyenne += n.duree / (double) notes.size();
            }

            expectEquals (fondaOuQuinte, (int) notes.size(), "que des do et des sol"_fr);
            expect (dureeMoyenne > 0.75, "notes longues : "_fr + juce::String (dureeMoyenne, 2) + " temps");
            expect (notes.front().debut == 0.0 && notes.front().hauteur % 12 == 0, "fondamentale sur le premier temps"_fr);
        }

        beginTest ("Basse hard techno : jamais sur le temps (place à la grosse caisse)");
        for (auto& n : generer (GenerateurMelodique::parDefaut (StyleMelodique::hardTechno), quatre, doMineur, 2))
            expect (std::abs (n.debut - std::round (n.debut)) > 1.0e-6);

        beginTest ("Arpège electro : une note par double-croche, dans l'accord"_fr);
        {
            const auto notes = generer (GenerateurMelodique::parDefaut (StyleMelodique::arpegeElectro), quatre, doMineur, 1);
            expectEquals ((int) notes.size(), 16);

            for (auto& n : notes)
                expect (n.hauteur % 12 == 0 || n.hauteur % 12 == 3 || n.hauteur % 12 == 7, "do, mi bémol ou sol"_fr);
        }

        beginTest ("Stabs minimal : des accords de trois notes");
        {
            const auto notes = generer (GenerateurMelodique::parDefaut (StyleMelodique::stabsMinimal), quatre, doMineur, 2);
            expectEquals ((int) notes.size() % 3, 0);
            expectEquals (notes[0].debut, notes[2].debut, "trois notes ensemble"_fr);
        }

        beginTest ("Ligne acid : des glissés (notes qui se chevauchent)");
        {
            const auto notes = generer (GenerateurMelodique::parDefaut (StyleMelodique::acid), quatre, doMineur, 2);
            int chevauchements = 0;

            for (size_t k = 0; k + 1 < notes.size(); ++k)
                if (notes[k].debut + notes[k].duree > notes[k + 1].debut + 1.0e-9)
                    ++chevauchements;

            expect (chevauchements >= 1, "au moins un glissé"_fr);
            auto sansGlisse = GenerateurMelodique::parDefaut (StyleMelodique::acid);
            sansGlisse.glisses = 0.0f;
            const auto droites = generer (sansGlisse, quatre, doMineur, 2);

            for (size_t k = 0; k + 1 < droites.size(); ++k)
                expect (droites[k].debut + droites[k].duree <= droites[k + 1].debut + 1.0e-9, "sans glissé : aucune note ne déborde"_fr);
        }

        beginTest ("Répétition : à fond, le même motif à chaque mesure ; densité 0, silence"_fr);
        {
            auto g = GenerateurMelodique::parDefaut (StyleMelodique::basseDnb);
            g.repetition = 0.7f;   // motif d'une mesure
            const auto notes = generer (g, quatre, doMineur, 4);
            std::vector<Note> premiere, troisieme;

            for (auto n : notes)
            {
                if (n.debut < 4.0)                      premiere.push_back (n);
                if (n.debut >= 8.0 && n.debut < 12.0)   { n.debut -= 8.0; troisieme.push_back (n); }
            }

            expectEquals ((int) premiere.size(), (int) troisieme.size());

            for (size_t k = 0; k < std::min (premiere.size(), troisieme.size()); ++k)
                expect (premiere[k].hauteur == troisieme[k].hauteur && std::abs (premiere[k].debut - troisieme[k].debut) < 1.0e-9);

            g.densite = 0.0f;
            expect (generer (g, quatre, doMineur, 4).empty());
        }

        beginTest ("Changer la gamme du morceau transpose les clips générés");
        {
            CarteSimulee carte (engine, 2);
            auto m = morceauAvec (120.0, quatre);
            m.assurerScenes (8);
            Clip c { "Basse dub", 2, {} };
            c.melodie = GenerateurMelodique::parDefaut (StyleMelodique::basseDub);
            c.regenerer (m.mesure, m.gamme);
            m.scenes[0].clips[1] = c;
            Moteur moteur (engine, m, &banqueEssai());
            moteur.modifier ([] (Morceau& mm) { mm.gamme = { 9, Gamme::Mode::mineur }; });

            for (auto& n : moteur.getMorceau().clip (1, 0)->notes)
                expect (n.hauteur % 12 == 9 || n.hauteur % 12 == 4, "la mineur : des la et des mi"_fr);
        }

        beginTest ("Au Push : trois encodeurs donnent une basse, Convert la fige, Shift + Duplicate la varie");
        {
            CarteSimulee carte (engine, 2);
            Moteur moteur (engine, morceauAvec (120.0, quatre), &banqueEssai());
            Surface surface;
            Pilote pilote (moteur, surface);
            auto bouton = [&] (Bouton b) { surface.recevoir ({ Evenement::Type::bouton, (int) b, 1, true }); surface.recevoir ({ Evenement::Type::bouton, (int) b, 0, false }); };

            bouton ((Bouton) (premierBoutonDuBas + 1));
            bouton (Bouton::note);
            bouton (Bouton::device);    // macros
            bouton (Bouton::device);    // mélodie
            expect (pilote.getPage() == Pilote::Page::melodie);

            surface.recevoir ({ Evenement::Type::encodeurTourne, 0, 20, false });   // densité
            surface.recevoir ({ Evenement::Type::encodeurTourne, 2, 15, false });   // mouvement
            surface.recevoir ({ Evenement::Type::encodeurTourne, 5, -10, false });  // répétition

            const auto* clip = moteur.getMorceau().clip (1, 0);
            expect (clip != nullptr && clip->melodie.has_value(), "clip mélodique créé"_fr);

            if (clip != nullptr && clip->melodie)
            {
                expect (clip->melodie->style == StyleMelodique::basseDub, "le son Sub dub donne une basse dub"_fr);
                expect (clip->notes.size() >= 2);
            }

            bouton (Bouton::convert);
            expect (! moteur.getMorceau().clip (1, 0)->estGenere(), "figé"_fr);
            surface.recevoir ({ Evenement::Type::bouton, (int) Bouton::shift, 1, true });
            bouton (Bouton::duplicate);
            surface.recevoir ({ Evenement::Type::bouton, (int) Bouton::shift, 0, false });
            expect (moteur.getMorceau().clip (1, 1) != nullptr, "dupliqué et varié en dessous"_fr);
        }

        beginTest ("Le générateur mélodique se sauvegarde avec le morceau"_fr);
        {
            auto m = morceauDeDemonstration();
            Clip c { "Acid", 2, {} };
            c.melodie = GenerateurMelodique::parDefaut (StyleMelodique::acid);
            c.melodie->graine = 1234;
            c.regenerer (m.mesure, m.gamme);
            m.scenes[6].clips[1] = c;
            const auto relu = Morceau::depuisJson (juce::JSON::parse (juce::JSON::toString (m.versJson())));
            expect (relu == m);
        }
    }
};

//==============================================================================
class EssaisEffets  : public juce::UnitTest
{
public:
    EssaisEffets()  : juce::UnitTest ("Effets", "groovebox") {}

    /** Morceau d'essai : la piste Batterie joue un sinus de 440 Hz tenu (pad 0), le clic est coupé. */
    static Morceau morceauSinus()
    {
        auto m = morceauAvec (120.0, Mesure::standard (4, 4));
        m.clic.actif = false;
        m.pistes[0].kit = kitsDeDepart::kit ("Analogique");
        m.pistes[0].kit.pads[0].son = "Essais/Sinus 440.wav";
        m.pistes[0].kit.largeur = 0.0f;
        return m;
    }

    /** Un coup sec (grosse caisse) sur la piste 1 : pour mesurer les échos. */
    static Morceau morceauCoup()
    {
        auto m = morceauAvec (120.0, Mesure::standard (4, 4));
        m.clic.actif = false;
        m.pistes[0].kit = kitsDeDepart::kit ("Minimal");
        return m;
    }

    void runTest() override
    {
        auto& engine = *te::Engine::getEngines()[0];
        EssaisBatterie::fichierSinus();

        auto niveauAvec = [&] (std::optional<Effet> effet, int duree = uneSeconde / 2)
        {
            CarteSimulee carte (engine, 2);
            auto m = morceauSinus();
            m.pistes[0].effets[0] = effet;
            Moteur moteur (engine, m, &banqueEssai());
            carte.jouer (256);
            moteur.jouerNote (0, 36, 127);
            carte.jouer (2400);
            return niveauEfficace (carte.jouer (duree), 0);
        };

        beginTest ("Filtre : neutre au milieu, il coupe le 440 Hz en passe-bas comme en passe-haut");
        {
            const auto sans = niveauAvec (std::nullopt);
            Effet f = Effet::parDefaut (TypeEffet::filtre);
            expectWithinAbsoluteError (niveauAvec (f) / sans, 1.0f, 0.05f, "neutre"_fr);
            f.reglages[0] = 0.1f;   // passe-bas vers 240 Hz
            expect (niveauAvec (f) < 0.5f * sans, "passe-bas"_fr);
            f.reglages[0] = 0.9f;   // passe-haut vers 1,6 kHz
            expect (niveauAvec (f) < 0.3f * sans, "passe-haut"_fr);
            expect (FiltrePlugin::coupures (0.5f).first > 19000.0 && FiltrePlugin::coupures (0.0f).first < 100.0);
        }

        beginTest ("Saturation et effets Airwindows : ils sonnent, sans exploser");
        for (int t = 1; t < nombreDeTypesDEffet; ++t)
        {
            auto e = Effet::parDefaut ((TypeEffet) t);
            const auto niveau = niveauAvec (e);
            expect (niveau > 0.01f && niveau < 2.0f && std::isfinite (niveau), nomDeLEffet ((TypeEffet) t) + " : " + juce::String (niveau));
        }

        beginTest ("Distorsion : de la saturation douce à l'écrêtage dur puis au repliement"_fr);
        {
            expectWithinAbsoluteError (DistorsionPlugin::formeDeLaDistorsion (2.0f, 0.0f), std::tanh (2.0f), 1.0e-6f, "douce"_fr);
            expectEquals (DistorsionPlugin::formeDeLaDistorsion (2.0f, 0.5f), 1.0f, "dure : écrêtée à 1"_fr);
            expect (DistorsionPlugin::formeDeLaDistorsion (2.0f, 1.0f) < 0.1f, "repliée : le haut de l'onde redescend"_fr);
            expectWithinAbsoluteError (DistorsionPlugin::formeDeLaDistorsion (0.1f, 0.3f), 0.1f, 0.01f, "petit signal : presque intact"_fr);
        }

        beginTest ("Distorsion à fond : beaucoup de matière, et le niveau reste tenu");
        {
            CarteSimulee carte (engine, 2);
            auto m = morceauSinus();
            Effet dist = Effet::parDefaut (TypeEffet::distorsion);
            dist.reglages = { 1.0f, 0.8f, 1.0f, 1.0f };
            m.pistes[0].effets[0] = dist;
            m.compressionDeSortie = 0.0f;
            Moteur moteur (engine, m, &banqueEssai());
            carte.jouer (256);
            moteur.jouerNote (0, 36, 127);
            carte.jouer (2400);
            const auto a = carte.jouer (uneSeconde / 4);
            expect (sautMax (a, 0) > 0.1f, "harmoniques fortes : "_fr + juce::String (sautMax (a, 0), 3));
            expect (crete (a, 0) <= LimiteurPlugin::plafond + 1.0e-4f && niveauEfficace (a, 0) > 0.05f, "fort mais tenu"_fr);
        }

        beginTest ("Grave : un sinus de 60 Hz ressort renforcé, avec des harmoniques");
        {
            auto fichier = banqueEssai().getRacine().getChildFile ("Essais/Sinus 60.wav");

            if (! fichier.existsAsFile())
            {
                juce::AudioBuffer<float> b (1, 48000);

                for (int i = 0; i < 48000; ++i)
                    b.setSample (0, i, 0.3f * (float) std::sin (juce::MathConstants<double>::twoPi * 60.0 * i / 48000.0));

                juce::WavAudioFormat wav;
                std::unique_ptr<juce::AudioFormatWriter> e (wav.createWriterFor (fichier.createOutputStream().release(), 48000.0, 1, 24, {}, 0));
                e->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
            }

            auto jouer = [&] (std::optional<Effet> effet)
            {
                CarteSimulee carte (engine, 2);
                auto m = morceauSinus();
                m.pistes[0].kit.pads[0].son = "Essais/Sinus 60.wav";
                m.pistes[0].effets[0] = effet;
                m.compressionDeSortie = 0.0f;
                Moteur moteur (engine, m, &banqueEssai());
                carte.jouer (256);
                moteur.jouerNote (0, 36, 127);
                carte.jouer (4800);
                return carte.jouer (uneSeconde / 4);
            };

            Effet grave = Effet::parDefaut (TypeEffet::grave);
            grave.reglages = { 1.0f, 0.6f, 0.6f, 0.0f };
            const auto sans = jouer (std::nullopt), avec = jouer (grave);
            expect (niveauEfficace (avec, 0) > 1.5f * niveauEfficace (sans, 0), "plus de grave : "_fr + juce::String (niveauEfficace (avec, 0) / niveauEfficace (sans, 0), 2) + " fois");
            // Les harmoniques d'un grave à 60 Hz tombent au-dessus de 150 Hz, là où jouent les petites enceintes
            auto auDessusDe150Hz = [] (const juce::AudioBuffer<float>& audio)
            {
                const float coef = 1.0f - std::exp (-juce::MathConstants<float>::twoPi * 150.0f / 48000.0f);
                float bas = 0.0f;
                double energie = 0.0;
                auto d = audio.getReadPointer (0);

                for (int i = 0; i < audio.getNumSamples(); ++i)
                {
                    bas += coef * (d[i] - bas);
                    energie += (d[i] - bas) * (d[i] - bas);
                }

                return std::sqrt (energie / audio.getNumSamples());
            };

            expect (auDessusDe150Hz (avec) > 2.0 * auDessusDe150Hz (sans), "des harmoniques au-dessus de 150 Hz : "_fr
                    + juce::String (auDessusDe150Hz (avec) / auDessusDe150Hz (sans), 2) + " fois plus");
        }

        beginTest ("Punch : l'attaque d'un coup ressort davantage");
        {
            auto jouer = [&] (std::optional<Effet> effet)
            {
                CarteSimulee carte (engine, 2);
                auto m = morceauCoup();
                m.pistes[0].effets[0] = effet;
                m.compressionDeSortie = 0.0f;
                Moteur moteur (engine, m, &banqueEssai());
                carte.jouer (256);
                moteur.jouerNote (0, 36, 100);
                return carte.jouer (uneSeconde / 2);
            };

            Effet punch = Effet::parDefaut (TypeEffet::punch);
            punch.reglages = { 1.0f, 0.3f, 0.0f, 0.0f };
            const auto sans = jouer (std::nullopt), avec = jouer (punch);
            auto rapport = [] (const juce::AudioBuffer<float>& a) { return crete (a, 0, 0, 2400) / juce::jmax (1.0e-6f, niveauEfficace (a, 0, 4800, 24000)); };
            expect (rapport (avec) > 1.3f * rapport (sans), "attaque plus marquée que la queue : "_fr + juce::String (rapport (avec), 2) + " contre " + juce::String (rapport (sans), 2));
        }

        beginTest ("Lo-fi et saturation changent vraiment le son");
        {
            CarteSimulee carte (engine, 2);
            auto m = morceauSinus();
            Effet sat = Effet::parDefaut (TypeEffet::saturation);
            sat.reglages[0] = 1.0f;
            m.pistes[0].effets[0] = sat;
            Moteur moteur (engine, m, &banqueEssai());
            carte.jouer (256);
            moteur.jouerNote (0, 36, 127);
            carte.jouer (2400);
            const auto a = carte.jouer (uneSeconde / 4);
            // Un sinus saturé gagne des harmoniques : il varie plus vite d'un échantillon à l'autre
            expect (sautMax (a, 0) > 0.05f, "saturation : "_fr + juce::String (sautMax (a, 0), 4));
        }

        beginTest ("Delay dub : le premier écho tombe à une croche pointée, au tempo");
        {
            CarteSimulee carte (engine, 2);
            auto m = morceauCoup();
            m.pistes[0].envoiDelayDb = 0.0f;
            m.delay.retour = 0.3f;
            Moteur moteur (engine, m, &banqueEssai());
            carte.jouer (256);
            moteur.jouerNote (0, 37, 127);     // un rimshot : bien plus court que l'écho
            DetecteurDeCoups coups;
            coups.analyser (carte.jouer (2 * uneSeconde), 0);
            expect (coups.coups.size() >= 2, "le coup et son écho"_fr);

            if (coups.coups.size() >= 2)
            {
                const auto ecart = coups.coups[1].echantillon - coups.coups[0].echantillon;
                expectWithinAbsoluteError ((double) ecart, 0.375 * frequence, 0.002 * frequence, "croche pointée à 120 BPM = 375 ms"_fr);
            }
        }

        beginTest ("Delay dub : au-delà de 100 %, l'écho s'emballe sans jamais dépasser la protection ; puis il retombe"_fr);
        {
            CarteSimulee carte (engine, 2);
            auto m = morceauCoup();
            m.delay.retour = 1.2f;
            m.delay.division = 2;   // croche
            Moteur moteur (engine, m, &banqueEssai());
            carte.jouer (256);

            // Envoi momentané : on tient, on frappe, on relâche
            moteur.envoiMomentane (0, true);
            moteur.jouerNote (0, 36, 127);
            carte.jouer (uneSeconde / 4);
            moteur.envoiMomentane (0, false);

            float plusFort = 0.0f;

            for (int i = 0; i < 8; ++i)
                plusFort = std::max (plusFort, crete (carte.jouer (uneSeconde), 0));

            const auto pendant = niveauEfficace (carte.jouer (uneSeconde), 0);
            expect (pendant > 0.02f, "l'écho s'emballe : encore fort après 9 s"_fr);
            expect (plusFort <= LimiteurPlugin::plafond + 1.0e-4f, "jamais au-dessus du plafond : "_fr + juce::String (juce::Decibels::gainToDecibels (plusFort), 1) + " dB");

            // On redescend le retour : l'écho retombe jusqu'au silence
            moteur.modifier ([] (Morceau& mm) { mm.delay.retour = 0.4f; });

            for (int i = 0; i < 6; ++i)
                carte.jouer (uneSeconde);

            expect (niveauEfficace (carte.jouer (uneSeconde), 0) < 0.001f, "l'écho est retombé"_fr);
        }

        beginTest ("Limiteur : tout à fond, la sortie ne dépasse jamais -1 dB, le casque non plus");
        {
            CarteSimulee carte (engine, 4);
            auto m = morceauDeDemonstration();
            m.volumeGeneralDb = 0.0f;
            m.musiqueDansCasqueDb = 0.0f;

            for (auto& p : m.pistes)
            {
                p.volumeDb = 6.0f;
                p.envoiDelayDb = 6.0f;
                p.envoiReverbDb = 6.0f;
                p.effets[0] = Effet::parDefaut (TypeEffet::saturation);
                p.effets[0]->reglages = { 1.0f, 0.8f, 1.0f, 0.0f };
            }

            m.delay.retour = 1.2f;
            Moteur moteur (engine, m, &banqueEssai());
            moteur.modifier ([] (Morceau& mm) { mm.clic.decompte = 0; });
            moteur.lancerScene (2);
            carte.jouer (128);
            moteur.avancer();
            float sortie = 0.0f, casque = 0.0f;

            for (int i = 0; i < 6; ++i)
            {
                const auto a = carte.jouer (uneSeconde);
                sortie = std::max ({ sortie, crete (a, 0), crete (a, 1) });
                casque = std::max ({ casque, crete (a, 2), crete (a, 3) });
            }

            expect (sortie > 0.5f, "ça joue fort"_fr);
            expect (sortie <= LimiteurPlugin::plafond + 1.0e-4f, "sono : "_fr + juce::String (juce::Decibels::gainToDecibels (sortie), 2) + " dB");
            expect (casque <= LimiteurPlugin::plafond + 1.0e-4f, "casque : "_fr + juce::String (juce::Decibels::gainToDecibels (casque), 2) + " dB");
        }

        beginTest ("Ajouter, sauter, enlever un effet en pleine lecture : pas de coupure");
        {
            CarteSimulee carte (engine, 2);
            Moteur moteur (engine, morceauDeDemonstration(), &banqueEssai());
            moteur.modifier ([] (Morceau& mm) { mm.clic.decompte = 0; });
            moteur.lancerScene (2);
            carte.jouer (128);
            moteur.avancer();
            carte.jouer (uneSeconde);
            float plusFaible = 1.0f;

            for (int i = 0; i < 36; ++i)
            {
                const int k = i % 3;
                moteur.modifier ([i, k] (Morceau& mm)
                {
                    auto& e = mm.pistes[1].effets[(size_t) k];

                    if (i % 6 < 3)
                        e = Effet::parDefaut ((TypeEffet) (i % nombreDeTypesDEffet));
                    else if (e)
                        e->actif = ! e->actif;
                });
                plusFaible = std::min (plusFaible, niveauEfficace (carte.jouer (uneSeconde / 10), 0));
            }

            moteur.modifier ([] (Morceau& mm) { for (auto& e : mm.pistes[1].effets) e.reset(); });
            expect (moteur.effetDeLaPiste (1, 0) == nullptr && moteur.effetDeLaPiste (1, 2) == nullptr, "effets retirés"_fr);
            expect (plusFaible > 0.005f, "le son reste présent"_fr);
        }

        beginTest ("Effets, envois et effets de départ se sauvegardent avec le morceau"_fr);
        {
            auto m = morceauDeDemonstration();
            m.pistes[2].effets[1] = Effet::parDefaut (TypeEffet::chorus);
            m.pistes[2].effets[1]->actif = false;
            m.pistes[2].envoiDelayDb = -9.0f;
            m.pistes[3].envoiReverbDb = -3.0f;
            m.delay = { 3, 1.1f, 0.2f, 0.6f };
            m.reverb = { 0.8f, 0.3f, 0.7f };
            m.compressionDeSortie = 0.55f;
            const auto relu = Morceau::depuisJson (juce::JSON::parse (juce::JSON::toString (m.versJson())));
            expect (relu == m);
        }

        beginTest ("Au Push : envoi momentané, Add Device, page d'effet, pages Mix, bande tactile");
        {
            CarteSimulee carte (engine, 2);
            Moteur moteur (engine, morceauDeDemonstration(), &banqueEssai());
            Surface surface;
            Pilote pilote (moteur, surface);
            auto appui = [&] (int b) { surface.recevoir ({ Evenement::Type::bouton, b, 1, true }); };
            auto relache = [&] (int b) { surface.recevoir ({ Evenement::Type::bouton, b, 0, false }); };
            auto bouton = [&] (Bouton b) { appui ((int) b); relache ((int) b); };

            appui (premierBoutonDuHaut + 6);
            expect (moteur.envoiMomentaneTenu (6), "envoi momentané tenu"_fr);
            expectEquals (pilote.calculerEtat().boutons[(size_t) (premierBoutonDuHaut + 6)], palette::indexDePiste (6, 3));
            relache (premierBoutonDuHaut + 6);
            expect (! moteur.envoiMomentaneTenu (6));

            // Add Device : un filtre sur la basse
            bouton ((Bouton) (premierBoutonDuBas + 1));
            bouton (Bouton::addDevice);
            expect (pilote.getPage() == Pilote::Page::ajoutEffet);
            bouton (Bouton::addDevice);
            expect (moteur.getMorceau().pistes[1].effets[0].has_value() && moteur.getMorceau().pistes[1].effets[0]->type == TypeEffet::filtre);
            expect (pilote.getPage() == Pilote::Page::effet);
            surface.recevoir ({ Evenement::Type::encodeurTourne, 0, -40, false });
            expectWithinAbsoluteError (moteur.getMorceau().pistes[1].effets[0]->reglages[0], 0.3f, 1.0e-4f, "filtre vers le passe-bas"_fr);
            surface.recevoir ({ Evenement::Type::encodeurTourne, 7, -8, false });
            expect (! moteur.getMorceau().pistes[1].effets[0]->actif, "encodeur 8 : effet sauté"_fr);

            // Delete + Device retire l'effet
            appui ((int) Bouton::deleteBouton);
            bouton (Bouton::device);
            relache ((int) Bouton::deleteBouton);
            expect (! moteur.getMorceau().pistes[1].effets[0].has_value(), "effet retiré"_fr);

            // Mix : volumes, envois delay, envois réverbération, effets de départ
            bouton (Bouton::mix);
            bouton (Bouton::pageDroite);
            expect (pilote.getPage() == Pilote::Page::envoisDelay);
            surface.recevoir ({ Evenement::Type::encodeurTourne, 4, 100, false });
            expectWithinAbsoluteError (moteur.getMorceau().pistes[4].envoiDelayDb, -30.0f, 1.0e-3f);
            bouton (Bouton::pageDroite);
            bouton (Bouton::pageDroite);
            expect (pilote.getPage() == Pilote::Page::departs);
            surface.recevoir ({ Evenement::Type::encodeurTourne, 1, 100, false });
            expectWithinAbsoluteError (moteur.getMorceau().delay.retour, 1.2f, 1.0e-4f, "retour du delay jusqu'à 120 %"_fr);

            // Bande tactile : filtre global
            surface.recevoir ({ Evenement::Type::bandeDeplacee, 0, 2000, false });
            expect (pilote.getAnnonce().valeur.startsWith ("Passe-bas"));
        }

        beginTest ("Temps de calcul : 8 pistes avec 3 effets, delay et réverbération");
        {
            CarteSimulee carte (engine, 2);
            auto m = morceauDeDemonstration();

            for (auto& p : m.pistes)
            {
                p.effets = { Effet::parDefaut (TypeEffet::filtre), Effet::parDefaut (TypeEffet::saturation), Effet::parDefaut (TypeEffet::chorus) };
                p.envoiDelayDb = -12.0f;
                p.envoiReverbDb = -12.0f;
            }

            Moteur moteur (engine, m, &banqueEssai());
            moteur.modifier ([] (Morceau& mm) { mm.clic.decompte = 0; });
            moteur.lancerScene (4);
            carte.jouer (128);
            moteur.avancer();
            const auto debut = juce::Time::getMillisecondCounterHiRes();
            carte.jouer (10 * uneSeconde);
            const auto duree = juce::Time::getMillisecondCounterHiRes() - debut;
            expect (duree < 3000.0, juce::String (duree / 1000.0, 2) + " s pour 10 s");
            logMessage ("  10 s calculées en " + juce::String (duree / 1000.0, 2) + " s (" + juce::String (100.0 * duree / 10000.0, 1) + " % du temps réel, sons provisoires)"_fr);
        }
    }
};

//==============================================================================
class EssaisEnchainements  : public juce::UnitTest
{
public:
    EssaisEnchainements()  : juce::UnitTest ("Enchaînements"_fr, "groovebox") {}

    static Clip clipTenu (const char* nom, int note)
    {
        return Clip { nom, 1, { Note { 0.0, 4.0, note, 100 } } };
    }

    /** 120 BPM en 4/4, sans décompte ni clic : la mesure 1 part 80 ms après le lancement et dure 2 secondes. */
    static Morceau morceauDEnchainement()
    {
        auto m = morceauAvec (120.0, Mesure::standard (4, 4));
        m.clic.actif = false;
        m.assurerScenes (4);
        return m;
    }

    static Enchainement enchainement (TypeEnchainement type, int mesures, float intensite = 0.7f)
    {
        Enchainement e;
        e.type = type;
        e.mesures = mesures;
        e.intensite = intensite;
        return e;
    }

    /** Joue n échantillons par tranches de 25 ms, en laissant le moteur suivre ses enchaînements entre deux. */
    static juce::AudioBuffer<float> jouerEtAvancer (Moteur& moteur, CarteSimulee& carte, int n)
    {
        juce::AudioBuffer<float> tout (2, n);

        for (int debut = 0; debut < n; debut += 1200)
        {
            const int k = std::min (1200, n - debut);
            auto morceau = carte.jouer (k);

            for (int c = 0; c < 2; ++c)
                tout.copyFrom (c, debut, morceau, c, 0, k);

            moteur.avancer();
        }

        return tout;
    }

    /** Joue jusqu'à un instant (secondes depuis le lancement), en accumulant ce qui sort. */
    struct Enregistrement
    {
        juce::AudioBuffer<float> audio { 2, 0 };
        int position = 0;

        void jusqua (Moteur& moteur, CarteSimulee& carte, double secondes)
        {
            const int fin = (int) (secondes * uneSeconde);

            if (fin <= position)
                return;

            auto suite = jouerEtAvancer (moteur, carte, fin - position);
            audio.setSize (2, fin, true, true);

            for (int c = 0; c < 2; ++c)
                audio.copyFrom (c, position, suite, c, 0, suite.getNumSamples());

            position = fin;
        }

        int ech (double secondes) const     { return (int) (secondes * uneSeconde); }

        float rms (double de, double a) const        { return niveauEfficace (audio, 0, ech (de), ech (a)); }

        /** Plus petit niveau efficace sur des fenêtres de 100 ms. */
        float rmsMin (double de, double a) const
        {
            float m = 1.0e9f;

            for (double t = de; t + 0.1 <= a; t += 0.05)
                m = juce::jmin (m, rms (t, t + 0.1));

            return m;
        }

        float saut (double de, double a) const
        {
            juce::AudioBuffer<float> extrait (1, ech (a) - ech (de));
            extrait.copyFrom (0, 0, audio, 0, ech (de), extrait.getNumSamples());
            return sautMax (extrait, 0);
        }

        /** Amplitude d'une fréquence (Goertzel). */
        float amplitude (double de, double a, double hz) const
        {
            const int d = ech (de), f = ech (a);
            const double w = juce::MathConstants<double>::twoPi * hz / frequence;
            const double k = 2.0 * std::cos (w);
            double s1 = 0.0, s2 = 0.0;

            for (int i = d; i < f; ++i)
            {
                const double s = audio.getSample (0, i) + k * s1 - s2;
                s2 = s1;
                s1 = s;
            }

            return (float) (std::sqrt (s1 * s1 + s2 * s2 - k * s1 * s2) * 2.0 / (f - d));
        }
    };

    static double frequenceDeLaNote (int note)     { return 440.0 * std::pow (2.0, (note - 69) / 12.0); }

    void runTest() override
    {
        auto& engine = *te::Engine::getEngines()[0];

        beginTest ("Formes des enchaînements"_fr);
        {
            for (int t = 0; t < nombreDeTypesDEnchainement; ++t)
            {
                const auto type = (TypeEnchainement) t;
                const auto nom = nomDeLEnchainement (type);
                const auto sortieAuDepart = formeDeLaVoix (type, false, 0.0f, 0.7f);
                const auto sortieALaFin = formeDeLaVoix (type, false, 1.0f, 0.7f);
                const auto entreeALaFin = formeDeLaVoix (type, true, 1.0f, 0.7f);

                expectWithinAbsoluteError (sortieAuDepart.gain, 1.0f, 1.0e-4f, nom + " : l'ancien joue au départ"_fr);
                expect (sortieAuDepart.estNeutre(), nom + " : l'ancien n'est pas filtré au départ"_fr);
                expectWithinAbsoluteError (sortieALaFin.gain, 0.0f, 1.0e-4f, nom + " : l'ancien s'est tu à la fin");
                expectWithinAbsoluteError (entreeALaFin.gain, 1.0f, 1.0e-4f, nom + " : le nouveau joue à fond à la fin");
                expect (entreeALaFin.estNeutre(), nom + " : le nouveau n'est plus filtré à la fin");
                expectEquals (entreeALaFin.envoi, 0.0f);
            }

            for (float p = 0.0f; p <= 1.0f; p += 0.01f)
            {
                const auto s = formeDeLaVoix (TypeEnchainement::fondu, false, p, 0.7f);
                const auto e = formeDeLaVoix (TypeEnchainement::fondu, true, p, 0.7f);
                expectWithinAbsoluteError (s.gain * s.gain + e.gain * e.gain, 1.0f, 1.0e-4f, "fondu à puissance constante"_fr);

                const auto bs = formeDeLaVoix (TypeEnchainement::echangeDesBasses, false, p, 0.7f);
                const auto be = formeDeLaVoix (TypeEnchainement::echangeDesBasses, true, p, 0.7f);
                expect (bs.sansGraves + be.sansGraves >= 1.0f, "jamais deux basses en même temps");
            }

            Morceau m = morceauDEnchainement();
            m.enchainement = enchainement (TypeEnchainement::filtre, 8, 0.4f);
            m.enchainement.batterieNette = false;
            m.scenes[2].entree = enchainement (TypeEnchainement::queueDEcho, 2);
            const auto relu = Morceau::depuisJson (juce::JSON::parse (juce::JSON::toString (m.versJson())));
            expect (relu == m, "enchaînements sauvegardés et relus");
            expect (! relu.scenes[1].entree.has_value());
        }

        beginTest ("Le réglage de voix bascule à l'échantillon près et coupe les graves"_fr);
        {
            CarteSimulee carte (engine, 2);
            Moteur moteur (engine, morceauDEnchainement(), &banqueEssai());
            auto plugin = moteur.getEdit().getPluginCache().createNewPlugin (VoixPlugin::xmlTypeName, {});
            auto* voix = dynamic_cast<VoixPlugin*> (plugin.get());
            expect (voix != nullptr);
            voix->initialise ({ te::TimePosition(), frequence, 2048 });

            auto traiter = [voix] (juce::AudioBuffer<float>& b, double debut)
            {
                te::PluginRenderContext rc (&b, juce::AudioChannelSet::stereo(), 0, b.getNumSamples(), nullptr, 0.0,
                                            te::TimeRange (te::TimePosition::fromSeconds (debut), te::TimeDuration::fromSeconds (b.getNumSamples() / frequence)),
                                            true, false, false, false);
                voix->applyToBuffer (rc);
            };

            // Coupe de l'ancien à 1 s pile
            voix->programmer ({ 1.0, 1.0, 0.0f, 1.0f, TypeEnchainement::coupe, false, 0.7f });
            juce::AudioBuffer<float> uns (2, 2400);

            for (int c = 0; c < 2; ++c)
                juce::FloatVectorOperations::fill (uns.getWritePointer (c), 1.0f, 2400);

            traiter (uns, 0.99);
            const int bascule = 480;
            expect (uns.getSample (0, bascule - 20) > 0.999f, "rien ne change avant la bascule");
            expect (uns.getSample (0, bascule + 48) < 0.9f, "la bascule commence à l'instant prévu"_fr);
            expect (uns.getSample (0, bascule + 720) < 0.01f, "15 ms plus tard, la voix s'est tue");
            expect (sautMax (uns, 0) < 0.01f, "sans saut");

            // Graves coupés (échange des basses, ancien juste après la mi-parcours)
            voix->programmer ({ 0.0, 100.0, 0.5f, 0.5f, TypeEnchainement::echangeDesBasses, false, 0.7f });

            auto sinus = [] (double hz)
            {
                juce::AudioBuffer<float> b (2, uneSeconde);

                for (int i = 0; i < uneSeconde; ++i)
                    for (int c = 0; c < 2; ++c)
                        b.setSample (c, i, (float) std::sin (juce::MathConstants<double>::twoPi * hz * i / frequence));

                return b;
            };

            auto grave = sinus (65.0);
            traiter (grave, 10.0);
            expect (niveauEfficace (grave, 0, uneSeconde / 2, uneSeconde) < 0.03f, "graves coupés : " + juce::String (niveauEfficace (grave, 0, uneSeconde / 2, uneSeconde)));
            auto medium = sinus (1000.0);
            traiter (medium, 11.0);
            expect (niveauEfficace (medium, 0, uneSeconde / 2, uneSeconde) > 0.65f, "le reste passe");

            voix->reposer (1.0f);
            auto libre = sinus (65.0);
            traiter (libre, 12.0);
            traiter (libre, 13.0);
            expectWithinAbsoluteError (niveauEfficace (libre, 0, uneSeconde / 2, uneSeconde), 0.707f, 0.01f, "au repos, la voix laisse tout passer");
        }

        beginTest ("Fondu : sans trou, sans saut, calé sur la mesure"_fr);
        {
            CarteSimulee carte (engine, 2);
            auto m = morceauDEnchainement();
            m.scenes[0].clips[2] = clipTenu ("Do", 60);
            m.scenes[1].clips[2] = clipTenu ("Sol", 67);
            Moteur moteur (engine, m, &banqueEssai());
            Enregistrement r;

            moteur.lancerScene (0);
            r.jusqua (moteur, carte, 2.5);
            moteur.lancerScene (1, enchainement (TypeEnchainement::fondu, 2));

            const auto e = moteur.enchainementDeLaPiste (2);
            expect (e && e->enAttente && e->sceneDe == 0 && e->sceneVers == 1, "enchaînement prévu à la mesure suivante"_fr);
            expect (moteur.etatCase (2, 1).departEnAttente);

            r.jusqua (moteur, carte, 6.08);
            const auto milieu = moteur.enchainementDeLaPiste (2);
            expect (milieu && std::abs (milieu->avancee - 0.5f) < 0.05f, "à mi-chemin au milieu des deux mesures : " + juce::String (milieu ? milieu->avancee : -1.0f));
            expect (moteur.etatCase (2, 0).joue && moteur.etatCase (2, 1).joue, "les deux clips jouent ensemble");
            expect (moteur.etatCase (2, 0).arretEnAttente, "l'ancien est montré comme en train de partir");

            r.jusqua (moteur, carte, 10.0);
            const float avant = r.rmsMin (1.5, 4.0);
            const float pendant = r.rmsMin (4.0, 8.2);
            const float apres = r.rmsMin (8.2, 10.0);
            logMessage ("  niveau avant " + juce::String (avant, 3) + ", pendant " + juce::String (pendant, 3) + ", après "_fr + juce::String (apres, 3));
            expect (pendant > 0.7f * juce::jmin (avant, apres), "pas de trou pendant le fondu");

            const float sautNormal = juce::jmax (r.saut (1.5, 4.0), r.saut (8.2, 10.0));
            expect (r.saut (4.0, 8.2) < 1.3f * sautNormal + 0.005f, "pas de saut : " + juce::String (r.saut (4.0, 8.2), 4) + " pour " + juce::String (sautNormal, 4));

            expect (! moteur.enchainementDeLaPiste (2).has_value(), "enchaînement terminé"_fr);
            expectEquals (moteur.sceneJouee (2), 1);
            expect (! moteur.etatCase (2, 0).joue, "l'ancien s'est arrêté");
            expectEquals (moteur.voixCourante (2), 1);
            expect (r.amplitude (8.5, 10.0, frequenceDeLaNote (60)) < 0.1f * r.amplitude (8.5, 10.0, frequenceDeLaNote (67)), "il ne reste que le nouveau");

            // Et retour par un second fondu, sur la voix A
            moteur.lancerScene (0, enchainement (TypeEnchainement::fondu, 1));
            r.jusqua (moteur, carte, 14.5);
            expectEquals (moteur.sceneJouee (2), 0);
            expectEquals (moteur.voixCourante (2), 0);
            expect (r.rmsMin (10.0, 14.5) > 0.7f * avant, "pas de trou au retour");
        }

        beginTest ("Échange des basses : jamais deux basses ensemble"_fr);
        {
            CarteSimulee carte (engine, 2);
            auto m = morceauDEnchainement();
            // Clips de deux mesures : la bascule des graves (à mi-parcours) ne tombe pas sur une reprise de note
            m.scenes[0].clips[1] = Clip { "Do grave", 2, { Note { 0.0, 8.0, 36, 100 } } };
            m.scenes[1].clips[1] = Clip { "Fa grave", 2, { Note { 0.0, 8.0, 41, 100 } } };
            Moteur moteur (engine, m, &banqueEssai());
            Enregistrement r;

            moteur.lancerScene (0);
            r.jusqua (moteur, carte, 2.5);
            moteur.lancerScene (1, enchainement (TypeEnchainement::echangeDesBasses, 2));
            r.jusqua (moteur, carte, 10.0);

            const double ancienne = frequenceDeLaNote (36), nouvelle = frequenceDeLaNote (41);
            const float a1 = r.amplitude (5.0, 5.9, ancienne), n1 = r.amplitude (5.0, 5.9, nouvelle);
            const float a2 = r.amplitude (6.3, 7.2, ancienne), n2 = r.amplitude (6.3, 7.2, nouvelle);
            logMessage ("  première moitié : ancienne " + juce::String (a1, 4) + ", nouvelle " + juce::String (n1, 4)
                        + " ; seconde : ancienne " + juce::String (a2, 4) + ", nouvelle " + juce::String (n2, 4));
            expect (n1 < 0.15f * a1, "première moitié : seule l'ancienne basse a ses graves"_fr);
            expect (a2 < 0.15f * n2, "seconde moitié : seule la nouvelle basse a ses graves"_fr);
            expect (r.rms (5.0, 5.9) > 0.5f * r.rms (1.5, 4.0), "le nouveau s'entend déjà (sans ses graves)"_fr);

            const float sautNormal = juce::jmax (r.saut (4.5, 5.9), r.saut (6.3, 7.8));
            logMessage ("  saut à la bascule " + juce::String (r.saut (5.9, 6.3), 4) + ", autour " + juce::String (sautNormal, 4));
            expect (r.saut (5.9, 6.3) < 1.3f * sautNormal + 0.005f, "pas de craquement à la bascule des graves");
            expectEquals (moteur.sceneJouee (1), 1);
        }

        beginTest ("Annuler un enchaînement en cours : retour à l'envers, sans trou"_fr);
        {
            CarteSimulee carte (engine, 2);
            auto m = morceauDEnchainement();
            m.scenes[0].clips[2] = clipTenu ("Do", 60);
            m.scenes[1].clips[2] = clipTenu ("Sol", 67);
            Moteur moteur (engine, m, &banqueEssai());
            Enregistrement r;

            moteur.lancerScene (0);
            r.jusqua (moteur, carte, 2.5);
            moteur.lancerScene (1, enchainement (TypeEnchainement::fondu, 4));
            r.jusqua (moteur, carte, 6.08);

            const auto avantRetour = moteur.enchainementDeLaPiste (2);
            expect (avantRetour && std::abs (avantRetour->avancee - 0.25f) < 0.05f);

            moteur.lancerScene (0, enchainement (TypeEnchainement::fondu, 4));
            const auto retour = moteur.enchainementDeLaPiste (2);
            expect (retour && retour->annulation && retour->sceneVers == 0, "retour vers l'ancienne scène");
            expectEquals (moteur.voixCourante (2), 0);

            r.jusqua (moteur, carte, 7.0);
            const auto enRoute = moteur.enchainementDeLaPiste (2);
            expect (enRoute && enRoute->avancee > 0.3f && enRoute->avancee < 0.7f, "le retour prend le temps déjà fait : " + juce::String (enRoute ? enRoute->avancee : -1.0f));

            r.jusqua (moteur, carte, 10.0);
            expect (! moteur.enchainementDeLaPiste (2).has_value());
            expectEquals (moteur.sceneJouee (2), 0);
            expect (! moteur.etatCase (2, 1).joue, "le nouveau s'est arrêté"_fr);
            expect (r.rmsMin (4.0, 10.0) > 0.7f * r.rmsMin (1.5, 4.0), "pas de trou");
            const float sautNormal = r.saut (1.5, 4.0);
            expect (r.saut (5.5, 8.5) < 1.3f * sautNormal + 0.005f, "pas de saut au retour");
            expect (r.amplitude (8.5, 10.0, frequenceDeLaNote (67)) < 0.1f * r.amplitude (8.5, 10.0, frequenceDeLaNote (60)), "il ne reste que l'ancien");

            // Annuler avant même le départ : rien ne change
            r.jusqua (moteur, carte, 10.15);
            moteur.lancerScene (1, enchainement (TypeEnchainement::filtre, 2));
            r.jusqua (moteur, carte, 10.3);
            moteur.lancerScene (0, enchainement (TypeEnchainement::filtre, 2));
            expect (! moteur.enchainementDeLaPiste (2).has_value(), "enchaînement oublié"_fr);
            expect (! moteur.etatCase (2, 1).departEnAttente, "le nouveau clip ne partira pas");
            r.jusqua (moteur, carte, 13.0);
            expectEquals (moteur.sceneJouee (2), 0);
            expect (! moteur.etatCase (2, 1).joue);
            expect (r.rmsMin (10.0, 13.0) > 0.9f * r.rmsMin (1.5, 4.0), "aucun changement de volume");
        }

        beginTest ("Batterie nette et clips communs aux deux scènes"_fr);
        {
            CarteSimulee carte (engine, 2);
            auto m = morceauDEnchainement();
            m.scenes[0].clips[0] = Clip { "Kick", 1, { Note { 0.0, 0.25, 36, 110 }, Note { 2.0, 0.25, 36, 110 } } };
            m.scenes[1].clips[0] = Clip { "Kick et caisse", 1, { Note { 0.0, 0.25, 36, 110 }, Note { 1.0, 0.25, 38, 110 } } };
            m.scenes[0].clips[1] = clipTenu ("Basse commune", 36);
            m.scenes[1].clips[1] = clipTenu ("Basse commune", 36);
            m.scenes[0].clips[2] = clipTenu ("Do", 60);
            m.scenes[1].clips[2] = clipTenu ("Sol", 67);
            Moteur moteur (engine, m, &banqueEssai());
            Enregistrement r;

            moteur.lancerScene (0);
            r.jusqua (moteur, carte, 2.5);
            moteur.lancerScene (1, enchainement (TypeEnchainement::fondu, 2));

            expect (! moteur.enchainementDeLaPiste (0).has_value(), "la batterie bascule net");
            expect (moteur.etatCase (0, 1).departEnAttente);
            expect (! moteur.enchainementDeLaPiste (1).has_value(), "la basse commune continue");
            expect (! moteur.etatCase (1, 1).departEnAttente);
            expect (moteur.enchainementDeLaPiste (2).has_value(), "la nappe se fond");

            r.jusqua (moteur, carte, 4.3);
            expectEquals (moteur.sceneJouee (0), 1);
            expect (! moteur.etatCase (0, 0).joue, "l'ancienne batterie s'est arrêtée sur la mesure");
            expectEquals (moteur.sceneJouee (1), 0);
            expect (moteur.etatCase (2, 0).joue && moteur.etatCase (2, 1).joue);

            // Sans batterie nette, la batterie se fond aussi
            auto e = enchainement (TypeEnchainement::fondu, 1);
            e.batterieNette = false;
            moteur.lancerScene (0, e);
            expect (moteur.enchainementDeLaPiste (0).has_value(), "la batterie se fond");
        }

        beginTest ("Queue d'écho : l'ancien s'éteint dans le delay par-dessus le nouveau"_fr);
        {
            CarteSimulee carte (engine, 2);
            auto m = morceauDEnchainement();
            m.scenes[0].clips[2] = clipTenu ("Do", 60);
            m.scenes[1].clips[2] = Clip { "Silence", 1, {} };
            Moteur moteur (engine, m, &banqueEssai());
            Enregistrement r;

            moteur.lancerScene (0);
            r.jusqua (moteur, carte, 2.5);
            moteur.lancerScene (1, enchainement (TypeEnchainement::queueDEcho, 1, 1.0f));
            r.jusqua (moteur, carte, 14.0);

            const float avant = r.rms (2.0, 3.9);
            const float queue = r.rms (6.5, 7.0);
            const float fin = r.rms (13.0, 14.0);
            logMessage ("  avant " + juce::String (avant, 3) + ", queue " + juce::String (queue, 4) + ", fin " + juce::String (fin, 5));
            expect (r.rms (4.5, 5.5) > 0.1f * avant, "l'écho s'entend après la bascule"_fr);
            expect (queue > 0.01f * avant, "la queue continue après l'arrêt de l'ancien clip"_fr);
            expect (fin < 0.3f * queue, "puis elle s'éteint");
            expect (crete (r.audio, 0) < 0.95f, "sans dépasser la protection");
            expect (! moteur.etatCase (2, 0).joue);
            expect (r.saut (3.9, 4.3) < 1.3f * r.saut (2.0, 3.9) + 0.005f, "la coupe du son direct ne craque pas");
        }

        beginTest ("Coupe pendant un enchaînement, changement de tempo, clic intact"_fr);
        {
            CarteSimulee carte (engine, 4);
            auto m = morceauDEnchainement();
            m.clic.actif = true;
            m.musiqueDansCasqueDb = -60.0f;
            m.scenes[0].clips[2] = clipTenu ("Do", 60);
            m.scenes[1].clips[2] = clipTenu ("Sol", 67);
            m.scenes[2].clips[2] = clipTenu ("Mi", 64);
            Moteur moteur (engine, m, &banqueEssai());

            moteur.lancerScene (0);
            jouerEtAvancer (moteur, carte, (int) (2.5 * uneSeconde));
            moteur.lancerScene (1, enchainement (TypeEnchainement::fondu, 4));
            jouerEtAvancer (moteur, carte, 2 * uneSeconde);
            moteur.modifier ([] (Morceau& mm) { mm.tempo = 130.0; });
            jouerEtAvancer (moteur, carte, uneSeconde);
            expect (moteur.enchainementDeLaPiste (2).has_value());

            // Coupe vers une troisième scène : l'enchaînement se termine à la mesure suivante
            moteur.lancerScene (2, enchainement (TypeEnchainement::coupe, 1));
            jouerEtAvancer (moteur, carte, 3 * uneSeconde);
            expect (! moteur.enchainementDeLaPiste (2).has_value(), "enchaînement terminé par la coupe"_fr);
            expectEquals (moteur.sceneJouee (2), 2);
            expect (! moteur.etatCase (2, 0).joue && ! moteur.etatCase (2, 1).joue);

            // Le clic du casque pendant un enchaînement : rien ne change
            moteur.modifier ([] (Morceau& mm) { mm.tempo = 120.0; });
            moteur.lancerScene (0, enchainement (TypeEnchainement::filtre, 2));
            jouerEtAvancer (moteur, carte, 3 * uneSeconde);
            auto audio = carte.jouer (2 * uneSeconde);
            DetecteurDeCoups detecteur;
            detecteur.analyser (audio, 2);
            expect (detecteur.coups.size() == 4, "4 coups de clic en 2 secondes : " + juce::String ((int) detecteur.coups.size()));
            expect (moteur.enchainementDeLaPiste (2).has_value());
        }

        beginTest ("Page Enchaînement sur le bouton User"_fr);
        {
            using T = Evenement::Type;
            CarteSimulee carte (engine, 2);
            auto m = morceauDEnchainement();
            m.scenes[0].clips[2] = clipTenu ("Do", 60);
            m.scenes[1].clips[2] = clipTenu ("Sol", 67);
            Moteur moteur (engine, m, &banqueEssai());
            Surface surface;
            Pilote pilote (moteur, surface);
            auto appui = [&] (Bouton b, bool bas) { surface.recevoir ({ T::bouton, (int) b, bas ? 127 : 0, bas }); };
            auto bouton = [&] (Bouton b) { appui (b, true); appui (b, false); };
            auto tourner = [&] (int encodeur, int pas) { surface.recevoir ({ T::encodeurTourne, encodeur, pas, false }); };

            bouton (Bouton::user);
            expect (pilote.getPage() == Pilote::Page::enchainement, "User : page Enchaînement"_fr);
            expect (pilote.calculerEtat().boutons[(size_t) Bouton::user] == palette::blancFort);

            tourner (0, 8);
            expect (moteur.getMorceau().enchainement.type == TypeEnchainement::fondu, "encodeur 1 : le type");
            tourner (1, -8);
            expectEquals (moteur.getMorceau().enchainement.mesures, 2, "encodeur 2 : la durée"_fr);
            tourner (2, -8);
            expect (! moteur.getMorceau().enchainement.batterieNette, "encodeur 3 : batterie nette");
            tourner (3, -20);
            expectWithinAbsoluteError (moteur.getMorceau().enchainement.intensite, 0.5f, 0.001f, "encodeur 4 : l'intensité"_fr);

            // User tenu + scène : retenir pour la scène, puis retirer
            appui (Bouton::user, true);
            bouton (Bouton::scene2);
            appui (Bouton::user, false);
            expect (moteur.getMorceau().scenes[1].entree == moteur.getMorceau().enchainement, "enchaînement retenu pour la scène 2"_fr);
            appui (Bouton::user, true);
            bouton (Bouton::scene2);
            appui (Bouton::user, false);
            expect (! moteur.getMorceau().scenes[1].entree.has_value(), "retiré au second appui");

            // Lancer depuis la page : avec ce qu'elle montre
            bouton (Bouton::scene1);
            jouerEtAvancer (moteur, carte, (int) (2.5 * uneSeconde));
            bouton (Bouton::scene2);
            const auto e = moteur.enchainementDeLaPiste (2);
            expect (e && e->type == TypeEnchainement::fondu, "scène lancée en fondu depuis la page"_fr);

            juce::Image image (juce::Image::RGB, EtatSurface::largeurEcran, EtatSurface::hauteurEcran, true);
            pilote.dessinerEcran (image);
            jouerEtAvancer (moteur, carte, 3 * uneSeconde);
            pilote.dessinerEcran (image);

            // Ailleurs que sur la page, une scène sans enchaînement retenu prend celui du morceau ; une scène qui en a un, le sien
            bouton (Bouton::mix);
            moteur.modifier ([] (Morceau& mm) { mm.scenes[0].entree = enchainement (TypeEnchainement::coupe, 1); });
            jouerEtAvancer (moteur, carte, 3 * uneSeconde);
            bouton (Bouton::scene1);
            expect (! moteur.enchainementDeLaPiste (2).has_value(), "la scène 1 entre en coupe, comme retenu");
            expect (moteur.etatCase (2, 0).departEnAttente);
        }

        beginTest ("À la main : la scène préparée part en silence, la bande la fait monter"_fr);
        {
            CarteSimulee carte (engine, 2);
            auto m = morceauDEnchainement();
            m.scenes[0].clips[0] = Clip { "Kick", 1, { Note { 0.0, 0.25, 36, 110 }, Note { 2.0, 0.25, 36, 110 } } };
            m.scenes[1].clips[0] = Clip { "Kick et caisse", 1, { Note { 0.0, 0.25, 36, 110 }, Note { 1.0, 0.25, 38, 110 } } };
            m.scenes[0].clips[2] = clipTenu ("Do", 60);
            m.scenes[1].clips[2] = clipTenu ("Sol", 67);
            Moteur moteur (engine, m, &banqueEssai());
            Enregistrement r;
            const double ancienne = frequenceDeLaNote (60), nouvelle = frequenceDeLaNote (67);

            moteur.lancerScene (0);
            r.jusqua (moteur, carte, 2.5);
            moteur.preparerScene (1, enchainement (TypeEnchainement::fondu, 4));
            expect (moteur.enchainementALaMain(), "enchaînement à la main préparé"_fr);
            expectEquals (moteur.sceneALaMain(), 1);
            expect (moteur.enchainementDeLaPiste (0).has_value(), "à la main, la batterie suit aussi la bande");

            r.jusqua (moteur, carte, 5.0);
            expect (moteur.etatCase (2, 1).joue, "la nouvelle scène tourne déjà, calée sur la mesure"_fr);
            expect (r.amplitude (4.3, 5.0, nouvelle) < 0.05f * r.amplitude (4.3, 5.0, ancienne), "mais en silence");

            // La bande monte peu à peu jusqu'au milieu
            for (int k = 1; k <= 25; ++k)
            {
                moteur.melangerALaMain (0.02f * k);
                r.jusqua (moteur, carte, 5.0 + 0.04 * k);
            }

            expectEquals (moteur.sceneJouee (0), 1, "la batterie a basculé au milieu de la bande");
            expect (! moteur.etatCase (0, 0).joue || moteur.enchainementDeLaPiste (0).has_value());
            r.jusqua (moteur, carte, 7.0);
            const float a = r.amplitude (6.2, 7.0, ancienne), n = r.amplitude (6.2, 7.0, nouvelle);
            expect (n > 0.5f * a && a > 0.5f * n, "à mi-bande, les deux s'entendent");
            expect (moteur.enchainementALaMain(), "rien ne se termine tant qu'on ne va pas au bout");

            for (int k = 26; k <= 50; ++k)
            {
                moteur.melangerALaMain (0.02f * k);
                r.jusqua (moteur, carte, 7.0 + 0.04 * (k - 25));
            }

            expect (! moteur.enchainementALaMain(), "arrivé en haut : la nouvelle scène prend la main"_fr);
            expectEquals (moteur.sceneJouee (2), 1);
            expectEquals (moteur.voixCourante (2), 1);
            r.jusqua (moteur, carte, 10.0);
            expect (! moteur.etatCase (2, 0).joue, "l'ancienne s'est arrêtée");
            expect (r.amplitude (8.5, 10.0, ancienne) < 0.05f * r.amplitude (8.5, 10.0, nouvelle));
            expect (r.saut (4.0, 10.0) < 1.3f * r.saut (1.5, 4.0) + 0.02f, "pas de saut en bougeant la bande");
        }

        beginTest ("À la main : redescendre annule, relancer la scène préparée finit seul, une autre scène annule"_fr);
        {
            CarteSimulee carte (engine, 2);
            auto m = morceauDEnchainement();
            m.scenes[0].clips[2] = clipTenu ("Do", 60);
            m.scenes[1].clips[2] = clipTenu ("Sol", 67);
            m.scenes[2].clips[2] = clipTenu ("Mi", 64);
            Moteur moteur (engine, m, &banqueEssai());
            Enregistrement r;

            moteur.lancerScene (0);
            r.jusqua (moteur, carte, 2.5);
            moteur.preparerScene (1, enchainement (TypeEnchainement::filtre, 2));
            r.jusqua (moteur, carte, 4.5);
            moteur.melangerALaMain (0.4f);
            r.jusqua (moteur, carte, 5.0);
            moteur.melangerALaMain (0.0f);
            expect (! moteur.enchainementALaMain(), "redescendu en bas : annulé"_fr);
            r.jusqua (moteur, carte, 6.0);
            expectEquals (moteur.sceneJouee (2), 0);
            expectEquals (moteur.voixCourante (2), 0);
            expect (! moteur.etatCase (2, 1).joue);
            expect (r.rmsMin (5.2, 6.0) > 0.8f * r.rmsMin (1.5, 4.0), "l'ancienne revient à son volume");

            // Bande lâchée à 30 %, puis la scène préparée relancée : le mélange finit seul (2 mesures × 70 % = 2,8 s)
            moteur.preparerScene (1, enchainement (TypeEnchainement::fondu, 2));
            r.jusqua (moteur, carte, 8.3);
            moteur.melangerALaMain (0.3f);
            r.jusqua (moteur, carte, 8.5);
            moteur.lancerScene (1);
            expect (! moteur.enchainementALaMain());
            const auto e = moteur.enchainementDeLaPiste (2);
            expect (e && std::abs (e->avancee - 0.3f) < 0.05f, "le mélange reprend là où était la bande"_fr);
            r.jusqua (moteur, carte, 10.5);
            expect (moteur.enchainementDeLaPiste (2).has_value(), "encore en route");
            r.jusqua (moteur, carte, 11.6);
            expect (! moteur.enchainementDeLaPiste (2).has_value(), "terminé");
            expectEquals (moteur.sceneJouee (2), 1);

            // Une autre scène pendant le mélange : il s'annule, puis la scène part normalement
            moteur.preparerScene (0, enchainement (TypeEnchainement::fondu, 2));
            r.jusqua (moteur, carte, 12.5);
            moteur.melangerALaMain (0.5f);
            moteur.lancerScene (2, enchainement (TypeEnchainement::coupe, 1));
            expect (! moteur.enchainementALaMain());
            r.jusqua (moteur, carte, 14.5);
            expectEquals (moteur.sceneJouee (2), 2);
            expect (! moteur.etatCase (2, 0).joue && ! moteur.etatCase (2, 1).joue);
        }

        beginTest ("Montée : le filtre et la réverbération grimpent sur la dernière mesure, puis retombent à la bascule"_fr);
        {
            CarteSimulee carte (engine, 2);
            auto m = morceauDEnchainement();
            m.scenes[0].clips[1] = Clip { "Do grave", 2, { Note { 0.0, 8.0, 36, 100 } } };
            m.scenes[1].clips[1] = Clip { "Fa grave", 2, { Note { 0.0, 8.0, 41, 100 } } };
            Moteur moteur (engine, m, &banqueEssai());
            Enregistrement r;

            moteur.lancerScene (0);
            r.jusqua (moteur, carte, 2.5);
            auto e = enchainement (TypeEnchainement::coupe, 1, 1.0f);
            e.montee = true;
            moteur.lancerScene (1, e);
            expect (moteur.monteeEnCours(), "montée prévue jusqu'à la bascule"_fr);
            r.jusqua (moteur, carte, 4.0);
            expect (moteur.monteeEnCours());
            r.jusqua (moteur, carte, 5.0);
            expect (! moteur.monteeEnCours(), "montée finie à la bascule"_fr);

            const double grave = frequenceDeLaNote (36);
            const float avant = r.amplitude (1.2, 2.0, grave);
            const float finDeMontee = r.amplitude (3.7, 4.05, grave);
            const float apres = r.amplitude (4.4, 5.0, frequenceDeLaNote (41));
            logMessage ("  graves avant " + juce::String (avant, 4) + ", fin de montée "_fr + juce::String (finDeMontee, 4) + ", après la bascule "_fr + juce::String (apres, 4));
            expect (finDeMontee < 0.3f * avant, "en fin de montée, les graves sont filtrés"_fr);
            expect (apres > 0.5f * avant, "à la bascule, tout revient d'un coup"_fr);
            // La bascule fait partir une nouvelle note : on la compare au tout premier départ de la basse
            logMessage ("  saut à la bascule "_fr + juce::String (r.saut (4.0, 4.3), 4) + ", au premier départ "_fr + juce::String (r.saut (0.0, 0.5), 4));
            expect (r.saut (4.0, 4.3) < 1.3f * r.saut (0.0, 0.5) + 0.01f, "sans craquement");

            // Relancer la scène qu'on quitte n'en fait pas ; une nouvelle scène remplace la montée prévue
            e.type = TypeEnchainement::fondu;
            moteur.lancerScene (0, e);
            expect (moteur.monteeEnCours());
            moteur.lancerScene (1, e);
            expect (! moteur.monteeEnCours(), "pas de montée en annulant"_fr);
        }

        beginTest ("Push : Shift + scène prépare, la bande devient le curseur puis redevient le filtre"_fr);
        {
            using T = Evenement::Type;
            CarteSimulee carte (engine, 2);
            auto m = morceauDEnchainement();
            m.scenes[0].clips[2] = clipTenu ("Do", 60);
            m.scenes[1].clips[2] = clipTenu ("Sol", 67);
            Moteur moteur (engine, m, &banqueEssai());
            Surface surface;
            Pilote pilote (moteur, surface);
            auto appui = [&] (Bouton b, bool bas) { surface.recevoir ({ T::bouton, (int) b, bas ? 127 : 0, bas }); };
            auto bouton = [&] (Bouton b) { appui (b, true); appui (b, false); };
            auto bande = [&] (float x) { surface.recevoir ({ T::bandeDeplacee, 0, juce::roundToInt (x * 16383.0f), false }); };

            bouton (Bouton::scene1);
            jouerEtAvancer (moteur, carte, (int) (2.5 * uneSeconde));
            expect (! pilote.calculerEtat().bandeSansRetour);

            appui (Bouton::shift, true);
            bouton (Bouton::scene2);
            appui (Bouton::shift, false);
            expect (moteur.enchainementALaMain(), "Shift + scène : mélange à la main"_fr);
            expect (pilote.calculerEtat().bandeSansRetour, "la bande ne revient plus au centre");

            const auto messages = pilote.calculerEtat().messagesDepuis (EtatSurface {});
            expect (std::any_of (messages.begin(), messages.end(), [] (auto& mm) { return mm.isSysEx(); }), "le Push reçoit la nouvelle configuration de la bande");

            jouerEtAvancer (moteur, carte, 2 * uneSeconde);
            surface.recevoir ({ T::bandeTouchee, 0, 127, true });

            for (int k = 0; k <= 20; ++k)
            {
                bande (0.05f * k);
                jouerEtAvancer (moteur, carte, 1200);
            }

            expect (! moteur.enchainementALaMain(), "en haut : la nouvelle scène a pris la main");
            expectEquals (moteur.sceneJouee (2), 1);
            expect (! pilote.calculerEtat().bandeSansRetour, "la bande redevient le filtre global");

            // Le doigt encore posé en haut ne pousse pas le filtre : il attend qu'on lâche
            bande (1.0f);
            auto audio = jouerEtAvancer (moteur, carte, uneSeconde / 2);
            expect (niveauEfficace (audio, 0, uneSeconde / 4, uneSeconde / 2) > 0.01f, "pas de passe-haut à fond par mégarde");
            surface.recevoir ({ T::bandeTouchee, 0, 0, false });

            bouton (Bouton::user);
            surface.recevoir ({ T::encodeurTourne, 4, 8, false });
            expect (moteur.getMorceau().enchainement.montee, "encodeur 5 : la montée"_fr);
        }
    }
};

//==============================================================================
class EssaisPrises  : public juce::UnitTest
{
public:
    EssaisPrises()  : juce::UnitTest ("Prises", "groovebox") {}

    static juce::File dossierDEssai()
    {
        auto d = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("groovebox-essais-prises");
        d.deleteRecursively();
        d.createDirectory();
        return d;
    }

    static std::unique_ptr<juce::AudioFormatReader> lireWav (const juce::File& f)
    {
        juce::WavAudioFormat wav;
        return std::unique_ptr<juce::AudioFormatReader> (wav.createReaderFor (new juce::FileInputStream (f), true));
    }

    void runTest() override
    {
        auto& engine = *te::Engine::getEngines()[0];
        using G = EvenementDePrise::Genre;

        beginTest ("Écarts entre deux états du morceau : notés puis réappliqués à l'identique"_fr);
        {
            auto avant = morceauDeDemonstration();
            auto apres = avant;
            apres.tempo = 128.5;
            apres.pistes[2].macros[3] = 0.9f;
            apres.pistes[1].mute = true;
            apres.pistes[4].effets[1] = Effet::parDefaut (TypeEffet::distorsion);
            apres.scenes[1].clips[6] = Clip { "Nouveau", 2, { Note { 1.0, 0.5, 62, 90 } } };
            apres.scenes[0].clips[2].reset();
            apres.scenes[2].entree = EssaisEnchainements::enchainement (TypeEnchainement::filtre, 2);

            const auto changements = ecarts::entre (avant.versJson(), apres.versJson());
            logMessage ("  " + juce::String (changements.size()) + " changements");
            expect (changements.size() > 0 && changements.size() < 40, "seuls les changements sont notés");
            expect (ecarts::entre (avant.versJson(), avant.versJson()).isEmpty(), "rien de noté sans changement");

            const auto refait = Morceau::depuisJson (ecarts::appliquer (avant.versJson(), changements));
            expect (refait == apres, "réappliqués, ils redonnent exactement le morceau");

            const auto json = juce::JSON::parse (juce::JSON::toString (juce::var (changements)));
            expect (Morceau::depuisJson (ecarts::appliquer (avant.versJson(), json)) == apres, "aussi après un passage par le fichier");
        }

        beginTest ("Une prise note les gestes, calés sur les mesures, et enregistre le son de la sortie"_fr);
        {
            const auto racine = dossierDEssai();
            CarteSimulee carte (engine, 2);
            auto m = EssaisEnchainements::morceauDEnchainement();
            m.scenes[0].clips[2] = EssaisEnchainements::clipTenu ("Do", 60);
            m.scenes[1].clips[2] = EssaisEnchainements::clipTenu ("Sol", 67);
            m.scenes[0].clips[1] = EssaisEnchainements::clipTenu ("Basse", 36);
            Moteur moteur (engine, m, &banqueEssai());
            const auto depart = moteur.getMorceau();
            moteur.dossierDesPrises = [racine] { return racine; };
            juce::File dossierTermine;
            moteur.surPriseTerminee = [&] (const juce::File& d) { dossierTermine = d; };

            expect (! moteur.priseEnCours());
            expect (moteur.demarrerPrise(), "prise commencée");
            expect (moteur.enLecture(), "à l'arrêt, la prise lance la lecture");
            expectEquals (moteur.priseCourante()->nom, juce::String ("Prise 1"));
            moteur.lancerScene (0);

            juce::AudioBuffer<float> joue (2, 0);
            auto jouer = [&] (double secondes)
            {
                auto morceau = EssaisEnchainements::jouerEtAvancer (moteur, carte, (int) (secondes * uneSeconde));
                const int avant = joue.getNumSamples();
                joue.setSize (2, avant + morceau.getNumSamples(), true, true);

                for (int c = 0; c < 2; ++c)
                    joue.copyFrom (c, avant, morceau, c, 0, morceau.getNumSamples());
            };

            jouer (2.5);
            moteur.modifier ([] (Morceau& mm) { mm.pistes[2].volumeDb = -6.0f; }, "encodeur 2");
            moteur.modifier ([] (Morceau& mm) { mm.pistes[2].volumeDb = -9.0f; }, "encodeur 2");
            moteur.lancerScene (1, EssaisEnchainements::enchainement (TypeEnchainement::fondu, 1));
            jouer (2.5);
            moteur.filtreGlobal (0.3f);
            moteur.filtreGlobal (0.5f);
            moteur.modifier ([] (Morceau& mm) { mm.pistes[1].mute = true; });
            moteur.jouerNote (3, 64, 100);
            jouer (0.3);
            moteur.relacherNote (3, 64);
            moteur.arreterPiste (1);
            jouer (1.0);
            moteur.arreter();

            expect (! moteur.priseEnCours(), "la prise s'arrête avec la lecture");
            expect (dossierTermine.isDirectory(), "dossier de la prise");

            auto prise = Prise::charger (dossierTermine.getChildFile ("prise.json"));
            expect (prise.has_value(), "journal relu");

            if (! prise)
                return;

            expect (prise->morceauDeDepart == depart, "le morceau de départ est gardé tel quel");
            expectWithinAbsoluteError (prise->debut, moteur.tempsDeLaMesure1(), 1.0e-9, "la prise commence à la mesure 1");
            expectWithinAbsoluteError (prise->dureeEnSecondes(), 6.3, 0.2, "durée");

            juce::StringArray genres;

            for (auto& e : prise->evenements)
                genres.add (juce::String ((int) e.genre));

            const juce::StringArray attendus { juce::String ((int) G::scene), juce::String ((int) G::reglages), juce::String ((int) G::reglages),
                                               juce::String ((int) G::scene), juce::String ((int) G::filtreGlobal), juce::String ((int) G::filtreGlobal),
                                               juce::String ((int) G::reglages), juce::String ((int) G::note), juce::String ((int) G::note), juce::String ((int) G::arret) };
            expectEquals (genres.joinIntoString (" "), attendus.joinIntoString (" "), "les gestes, dans l'ordre");

            if (prise->evenements.size() == 10)
            {
                const auto& e = prise->evenements;
                expect (e[0].scene == 0 && e[0].piste == -1 && e[0].cale && std::abs (*e[0].cale - moteur.tempsDeLaMesure1()) < 1.0e-9, "scène 1 lancée à la mesure 1");
                expect (e[3].cale && std::abs (*e[3].cale - (moteur.tempsDeLaMesure1() + 8.0)) < 1.0e-6, "scène 2 calée sur la mesure 3 : " + juce::String (e[3].cale.value_or (-1.0)));
                expect (e[3].enchainement && e[3].enchainement->type == TypeEnchainement::fondu, "avec son enchaînement");
                expect (e[3].temps > moteur.tempsDeLaMesure1() + 4.0 && e[3].temps < *e[3].cale, "noté au moment du geste");
                expect (juce::JSON::toString (e[1].changements).contains ("pistes/2/volumeDb"), "le réglage tourné est noté");
                expectWithinAbsoluteError (e[4].valeur, 0.3f, 1.0e-6f);
                expect (e[7].piste == 3 && (int) e[7].valeur == 64 && e[7].velocite == 100 && e[8].velocite == 0, "note jouée puis relâchée");
                expect (e[9].piste == 1, "arrêt de la piste");

                // Réappliqués dans l'ordre, les réglages donnent l'état de la fin
                auto json = prise->morceauDeDepart.versJson();

                for (auto& ev : e)
                    if (ev.genre == G::reglages)
                        json = ecarts::appliquer (json, ev.changements);

                expect (Morceau::depuisJson (json) == moteur.getMorceau(), "les réglages notés suffisent à retrouver le morceau");
            }

            // Le son : sortie.wav, identique à ce qui est sorti sur 1-2
            auto lecteur = lireWav (dossierTermine.getChildFile ("sortie.wav"));
            expect (lecteur != nullptr, "sortie.wav");
            expect (! dossierTermine.getChildFile ("micros.wav").exists(), "pas de micros sans entrée");

            if (lecteur != nullptr)
            {
                expectEquals ((int) lecteur->numChannels, 2);
                expectEquals ((int) lecteur->bitsPerSample, 24);
                juce::AudioBuffer<float> fichier (2, (int) lecteur->lengthInSamples);
                lecteur->read (&fichier, 0, fichier.getNumSamples(), 0, true, true);
                logMessage ("  sortie.wav : " + juce::String (fichier.getNumSamples() / frequence, 2) + " s, joué " + juce::String (joue.getNumSamples() / frequence, 2) + " s");
                expect (std::abs (fichier.getNumSamples() - joue.getNumSamples()) < uneSeconde / 10, "toute la prise est dans le fichier");

                // Recaler le fichier sur ce qui a été joué, puis comparer
                int decalage = -1;
                const int repere = uneSeconde;

                for (int d = 0; d < uneSeconde / 4 && decalage < 0; ++d)
                {
                    bool pareil = true;

                    for (int i = 0; i < 512 && pareil; ++i)
                        pareil = std::abs (fichier.getSample (0, repere + i) - joue.getSample (0, repere + i + d)) < 1.0e-4f;

                    if (pareil)
                        decalage = d;
                }

                expect (decalage >= 0, "le fichier retrouve le son joué");

                if (decalage >= 0)
                {
                    float ecart = 0.0f;

                    for (int i = 0; i + decalage < joue.getNumSamples() && i < fichier.getNumSamples(); ++i)
                        ecart = juce::jmax (ecart, std::abs (fichier.getSample (0, i) - joue.getSample (0, i + decalage)));

                    logMessage ("  décalage " + juce::String (decalage) + " échantillons, écart maximal "_fr + juce::String (ecart, 7));
                    expect (ecart < 1.0e-4f, "même son, à la précision du 24 bits près");
                    expect (niveauEfficace (fichier, 0) > 0.005f, "et ce n'est pas du silence");
                }
            }
        }

        beginTest ("En pleine lecture, la prise part de la mesure en cours ; les micros ont leur fichier"_fr);
        {
            const auto racine = dossierDEssai();
            CarteSimulee carte (engine, 2, 2);
            carte.entree = [] (int canal, juce::int64 i) { return 0.25f * (float) std::sin (juce::MathConstants<double>::twoPi * (canal == 0 ? 220.0 : 330.0) * (double) i / frequence); };
            auto m = EssaisEnchainements::morceauDEnchainement();
            m.scenes[0].clips[2] = EssaisEnchainements::clipTenu ("Do", 60);
            Moteur moteur (engine, m, &banqueEssai());
            moteur.dossierDesPrises = [racine] { return racine; };
            expectEquals (moteur.getEntreeDesMicros().canauxDisponibles(), 2, "deux entrées ouvertes");

            moteur.lancerScene (0);
            EssaisEnchainements::jouerEtAvancer (moteur, carte, 5 * uneSeconde);
            expect (moteur.demarrerPrise());
            expectEquals (moteur.priseCourante()->scenesDeDepart[2], 0, "la scène qui joue est notée au départ");
            expectWithinAbsoluteError (moteur.priseCourante()->debut, moteur.tempsDeLaMesure1() + 8.0, 1.0e-9, "début de la mesure 3, en cours");
            EssaisEnchainements::jouerEtAvancer (moteur, carte, 2 * uneSeconde);
            const auto dossier = moteur.arreterPrise();
            expect (moteur.enLecture(), "arrêter la prise ne coupe pas la musique");
            expect (dossier.has_value());

            if (! dossier)
                return;

            auto prise = Prise::charger (dossier->getChildFile ("prise.json"));
            expect (prise && prise->avecMicros);
            auto micros = lireWav (dossier->getChildFile ("micros.wav"));
            expect (micros != nullptr, "micros.wav");

            if (micros != nullptr)
            {
                juce::AudioBuffer<float> audio (2, (int) micros->lengthInSamples);
                micros->read (&audio, 0, audio.getNumSamples(), 0, true, true);
                // Le contenu n'est pas vérifiable ici : la carte simulée donne au moteur la même mémoire pour l'entrée et la sortie,
                // si bien que la musique recouvre l'entrée avant sa lecture (une vraie carte les sépare). Essai du contenu plus bas.
                expectWithinAbsoluteError ((double) audio.getNumSamples() / frequence, 2.0, 0.1, "deux secondes de micros");
            }

            // Une seconde prise : numérotée à la suite, dans son propre dossier
            expect (moteur.demarrerPrise());
            expectEquals (moteur.priseCourante()->nom, juce::String ("Prise 2"));
            EssaisEnchainements::jouerEtAvancer (moteur, carte, uneSeconde / 2);
            const auto seconde = moteur.arreterPrise();
            expect (seconde && *seconde != *dossier);

            // Changer de morceau termine une prise en cours
            expect (moteur.demarrerPrise());
            moteur.chargerMorceau (m);
            expect (! moteur.priseEnCours(), "ouvrir un morceau termine la prise");
            expectEquals ((int) racine.findChildFiles (juce::File::findDirectories, false).size(), 3);
        }

        beginTest ("Les micros : chaque entrée sur son canal, sans perte"_fr);
        {
            const auto racine = dossierDEssai();
            CarteSimulee carte (engine, 2);
            Moteur moteur (engine, EssaisEnchainements::morceauDEnchainement(), &banqueEssai());
            auto plugin = moteur.getEdit().getPluginCache().createNewPlugin (EnregistreurDeSortiePlugin::xmlTypeName, {});
            auto* sortie = dynamic_cast<EnregistreurDeSortiePlugin*> (plugin.get());
            expect (sortie != nullptr);
            EnregistreurDEntree entree;
            EnregistrementAudio enregistrement (*sortie, entree);
            expect (enregistrement.demarrer (racine, frequence, true));

            juce::AudioBuffer<float> entrees (2, 128), sorties (2, 128);

            for (juce::int64 debut = 0; debut < 2 * uneSeconde; debut += 128)
            {
                for (int c = 0; c < 2; ++c)
                    for (int i = 0; i < 128; ++i)
                        entrees.setSample (c, i, 0.25f * (float) std::sin (juce::MathConstants<double>::twoPi * (c == 0 ? 220.0 : 330.0) * (double) (debut + i) / frequence));

                sorties.applyGain (0.0f);
                sorties.setSample (0, 0, 1.0f);
                entree.audioDeviceIOCallbackWithContext (entrees.getArrayOfReadPointers(), 2, sorties.getArrayOfWritePointers(), 2, 128, {});
                expectEquals (sorties.getMagnitude (0, 128), 0.0f);
            }

            enregistrement.arreter();
            auto micros = lireWav (racine.getChildFile ("micros.wav"));
            expect (micros != nullptr);

            if (micros != nullptr)
            {
                juce::AudioBuffer<float> audio (2, (int) micros->lengthInSamples);
                micros->read (&audio, 0, audio.getNumSamples(), 0, true, true);
                expectEquals (audio.getNumSamples(), 2 * uneSeconde, "aucun échantillon perdu");
                expectWithinAbsoluteError (niveauEfficace (audio, 0, uneSeconde, uneSeconde + 4800), 0.177f, 0.005f, "le niveau du micro de gauche");
                // frequenceMesuree double le compte (prévue pour un bip qui sonne la moitié du temps) : on divise par deux
                const auto f1 = frequenceMesuree (audio, 0, uneSeconde, uneSeconde + 24000) / 2.0, f2 = frequenceMesuree (audio, 1, uneSeconde, uneSeconde + 24000) / 2.0;
                expect (std::abs (f1 - 220.0) < 3.0 && std::abs (f2 - 330.0) < 3.0, "chaque micro sur son canal : " + juce::String (f1, 1) + " et " + juce::String (f2, 1) + " Hz");
            }
        }

        beginTest ("L'onglet Prises montre les prises du morceau"_fr);
        {
            const auto racine = dossierDEssai();
            CarteSimulee carte (engine, 2);
            Moteur moteur (engine, EssaisEnchainements::morceauDEnchainement(), &banqueEssai());
            moteur.dossierDesPrises = [racine] { return racine; };

            for (int k = 0; k < 2; ++k)
            {
                moteur.demarrerPrise();
                EssaisEnchainements::jouerEtAvancer (moteur, carte, uneSeconde / 2);
                moteur.arreter();
            }

            PagePrises page (engine, moteur);
            page.setSize (980, 640);
            moteur.demarrerPrise();
            laisserTravailler (400);
            auto image = page.createComponentSnapshot (page.getLocalBounds());
            const auto fichier = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("groovebox-onglet-prises.png");
            fichier.deleteFile();
            juce::FileOutputStream flux (fichier);
            juce::PNGImageFormat().writeImageToStream (image, flux);
            logMessage ("  image de l'onglet : " + fichier.getFullPathName());
            moteur.arreter();
            expect (image.isValid());
        }

        beginTest ("Sans dossier de prises, rien ne s'enregistre"_fr);
        {
            CarteSimulee carte (engine, 2);
            Moteur moteur (engine, EssaisEnchainements::morceauDEnchainement(), &banqueEssai());
            expect (! moteur.demarrerPrise());
            expect (! moteur.priseEnCours() && ! moteur.enLecture());
        }

        beginTest ("Push : Record commence et termine la prise, son bouton clignote en rouge"_fr);
        {
            using T = Evenement::Type;
            const auto racine = dossierDEssai();
            CarteSimulee carte (engine, 2);
            Moteur moteur (engine, EssaisEnchainements::morceauDEnchainement(), &banqueEssai());
            moteur.dossierDesPrises = [racine] { return racine; };
            Surface surface;
            Pilote pilote (moteur, surface);
            auto bouton = [&] (Bouton b) { surface.recevoir ({ T::bouton, (int) b, 127, true }); surface.recevoir ({ T::bouton, (int) b, 0, false }); };

            expectEquals (pilote.calculerEtat().boutons[(size_t) Bouton::record], palette::grisClair);
            bouton (Bouton::record);
            expect (moteur.priseEnCours(), "Record : prise commencée");

            bool rouge = false;

            for (int i = 0; i < 12 && ! rouge; ++i)
            {
                rouge = pilote.calculerEtat().boutons[(size_t) Bouton::record] == palette::rouge;
                juce::Thread::sleep (100);
            }

            expect (rouge, "le bouton Record passe au rouge");
            EssaisEnchainements::jouerEtAvancer (moteur, carte, uneSeconde);

            juce::Image image (juce::Image::RGB, EtatSurface::largeurEcran, EtatSurface::hauteurEcran, true);
            pilote.dessinerEcran (image);
            expect (image.getPixelAt (EtatSurface::largeurEcran - 280, 24).getRed() > 200, "l'écran montre la prise en rouge");

            bouton (Bouton::record);
            expect (! moteur.priseEnCours(), "second appui : prise terminée");
            expect (moteur.enLecture());
            expectEquals ((int) racine.findChildFiles (juce::File::findDirectories, false).size(), 1);
        }
    }
};

//==============================================================================
class EssaisRetouche  : public juce::UnitTest
{
public:
    EssaisRetouche()  : juce::UnitTest ("Retouche", "groovebox") {}

    static juce::String texte (const juce::var& v)      { return juce::JSON::toString (v, true); }

    void runTest() override
    {
        auto& engine = *te::Engine::getEngines()[0];
        using G = EvenementDePrise::Genre;

        // Une prise jouée par le moteur : scène 1, des crans de volume, fondu vers la scène 2, coupe vers la scène 3, un clip seul
        const auto racine = EssaisPrises::dossierDEssai();
        juce::File dossierDeLaPrise;
        Morceau fin;
        double mesure1 = 0.0;

        {
            CarteSimulee carte (engine, 2);
            auto m = EssaisEnchainements::morceauDEnchainement();
            m.scenes[0].nom = "Intro";
            m.scenes[1].nom = "Groove";
            m.scenes[2].nom = "Plein";
            m.scenes[0].clips[2] = EssaisEnchainements::clipTenu ("Do", 60);
            m.scenes[1].clips[2] = EssaisEnchainements::clipTenu ("Sol", 67);
            m.scenes[2].clips[2] = EssaisEnchainements::clipTenu ("Mi", 64);
            m.scenes[1].clips[1] = EssaisEnchainements::clipTenu ("Basse", 36);
            m.scenes[0].clips[3] = EssaisEnchainements::clipTenu ("Lead", 72);
            Moteur moteur (engine, m, &banqueEssai());
            moteur.dossierDesPrises = [racine] { return racine; };
            mesure1 = moteur.tempsDeLaMesure1();

            moteur.demarrerPrise();
            moteur.lancerScene (0);
            EssaisEnchainements::jouerEtAvancer (moteur, carte, uneSeconde);

            for (int k = 1; k <= 6; ++k)
            {
                moteur.modifier ([k] (Morceau& mm) { mm.pistes[2].volumeDb = -2.0f * (float) k; }, "encodeur 2");
                EssaisEnchainements::jouerEtAvancer (moteur, carte, uneSeconde / 4);
            }

            // La mesure dure 2 s : chaque lancement tombe un peu avant une limite de mesure
            EssaisEnchainements::jouerEtAvancer (moteur, carte, uneSeconde);       // 3,5 s
            moteur.lancerScene (1, EssaisEnchainements::enchainement (TypeEnchainement::fondu, 1));
            EssaisEnchainements::jouerEtAvancer (moteur, carte, uneSeconde * 4);   // 7,5 s
            moteur.lancerScene (2, EssaisEnchainements::enchainement (TypeEnchainement::coupe, 1));
            EssaisEnchainements::jouerEtAvancer (moteur, carte, uneSeconde * 2);   // 9,5 s
            moteur.lancerClip (3, 0);
            EssaisEnchainements::jouerEtAvancer (moteur, carte, uneSeconde * 2);   // 11,5 s
            fin = moteur.getMorceau();
            dossierDeLaPrise = *moteur.arreterPrise();
            moteur.arreter();
        }

        const auto prise = *Prise::charger (dossierDeLaPrise.getChildFile ("prise.json"));
        const auto deroule = retouche::decouper (prise, dossierDeLaPrise.getFileName());

        beginTest ("Découper une prise : une section par scène jouée"_fr);
        {
            expectEquals ((int) deroule.sections.size(), 3);

            if (deroule.sections.size() == 3)
            {
                const auto& s = deroule.sections;
                expect (s[0].scene == 0 && s[1].scene == 1 && s[2].scene == 2, "les scènes, dans l'ordre"_fr);
                expect (s[0].nom == "Intro" && s[1].nom == "Groove" && s[2].nom == "Plein", "nommées comme les scènes");
                expectWithinAbsoluteError (s[0].longueur, 8.0, 1.0e-6, "Intro : 2 mesures");
                expectWithinAbsoluteError (s[1].longueur, 8.0, 1.0e-6, "Groove : 2 mesures (le calage tombe sur la mesure 5)");
                expectWithinAbsoluteError (s[0].mesures(), 2.0, 1.0e-6);
                expectWithinAbsoluteError (deroule.longueur(), prise.fin - prise.debut, 1.0e-6, "toute la prise");
                expect (s[1].entree && s[1].entree->type == TypeEnchainement::fondu, "Groove entre en fondu");
                expect (s[2].entree && s[2].entree->type == TypeEnchainement::coupe, "Plein entre en coupe");
                expect (s[1].avance > 0.5 && s[1].avance < 4.0, "lancé un peu avant la mesure : " + juce::String (s[1].avance, 2));
                expectEquals (s[1].scenesDeDepart[2], 1, "au début de Groove, la nappe joue Groove (la scène qui ouvre la section)");
                expectEquals (s[1].scenesDeDepart[3], 0, "le lead d'Intro continue (case vide dans Groove)");
                expectEquals (s[2].scenesDeDepart[1], 1);
                expectWithinAbsoluteError (s[1].debutDansLaPrise, mesure1 + 8.0, 1.0e-6);

                int reglages = 0, lancements = 0;

                for (auto& e : s[0].evenements)
                    reglages += e.genre == G::reglages ? 1 : 0;

                for (auto& e : s[2].evenements)
                    if (e.genre == G::scene && e.piste == 3)
                    {
                        ++lancements;
                        expect (e.cale && std::abs (*e.cale - 4.0) < 1.0e-6, "le clip seul, calé sur la 2e mesure de Plein : " + juce::String (e.cale.value_or (-1.0)));
                    }

                expectEquals (reglages, 6, "les crans de volume sont dans Intro");
                expectEquals (lancements, 1, "le clip seul est dans Plein");

                // L'état de chaque section = l'état de la précédente plus ses réglages ; à la fin, le morceau de la fin
                for (size_t i = 0; i < s.size(); ++i)
                {
                    auto json = s[i].etat;

                    for (auto& e : s[i].evenements)
                        if (e.genre == G::reglages)
                            json = ecarts::appliquer (json, e.changements);

                    const auto suivant = i + 1 < s.size() ? Morceau::depuisJson (s[i + 1].etat) : fin;
                    expect (Morceau::depuisJson (json) == suivant, "les sections s'enchaînent sans rien perdre (" + juce::String ((int) i) + ")");
                }

                expectWithinAbsoluteError (Morceau::depuisJson (s[1].etat).pistes[2].volumeDb, -12.0f, 1.0e-4f, "Groove part du volume laissé par Intro");
            }
        }

        beginTest ("Couper, supprimer, déplacer, répéter"_fr);
        {
            auto d = deroule;
            expect (retouche::couper (d, 0, 4.0), "couper Intro à la 2e mesure");
            expectEquals ((int) d.sections.size(), 4);
            expectWithinAbsoluteError (d.sections[0].longueur, 4.0, 1.0e-6);
            expectWithinAbsoluteError (d.sections[1].longueur, 4.0, 1.0e-6);
            expect (! d.sections[1].entree.has_value() && d.sections[1].scene == 0, "la seconde moitié est une suite d'Intro");
            expectWithinAbsoluteError (d.longueur(), deroule.longueur(), 1.0e-6, "rien n'est perdu");
            expectWithinAbsoluteError (d.sections[1].debutDansLaPrise, deroule.sections[0].debutDansLaPrise + 4.0, 1.0e-6, "le son de la suite au bon endroit");

            // L'état de la suite : celui d'Intro plus les crans de la première mesure et demie
            auto json = d.sections[0].etat;

            for (auto& e : d.sections[0].evenements)
                if (e.genre == G::reglages)
                    json = ecarts::appliquer (json, e.changements);

            expect (Morceau::depuisJson (json) == Morceau::depuisJson (d.sections[1].etat), "la suite part de là où on a coupé");
            expect (! retouche::couper (d, 0, 0.0) && ! retouche::couper (d, 0, 4.0), "pas de coupe au bord");

            expect (retouche::deplacer (d, 3, -1));
            expect (d.sections[2].scene == 2 && d.sections[3].scene == 1, "Plein avant Groove");
            expect (! retouche::deplacer (d, 0, -1) && ! retouche::deplacer (d, 3, 1), "rien au-delà des bords");

            expect (retouche::repeter (d, 0));
            expectEquals ((int) d.sections.size(), 5);
            expect (texte (d.sections[0].versJson()) == texte (d.sections[1].versJson()), "la répétition est une copie");

            expect (retouche::supprimer (d, 1));
            expectEquals ((int) d.sections.size(), 4);
            expect (! retouche::supprimer (d, 9));
        }

        beginTest ("Courbes : lister, lisser, effacer"_fr);
        {
            auto d = deroule;
            auto& intro = d.sections[0];
            const auto courbes = retouche::courbes (intro, 2);
            expect (courbes.contains ("/pistes/2/volumeDb"), "le volume de la nappe est une courbe");
            expect (retouche::courbes (intro, 1).isEmpty(), "rien ne bouge sur la basse");
            expectEquals (retouche::nomDuReglage ("/pistes/2/volumeDb"), juce::String ("Volume"));
            expectEquals (retouche::nomDuReglage ("/pistes/4/macros/3"), juce::String ("Macro 4"));
            expectEquals (retouche::nomDuReglage ("/tempo"), juce::String ("Tempo"));

            const auto avant = retouche::points (intro, "/pistes/2/volumeDb");
            expectEquals ((int) avant.size(), 7, "valeur de départ et 6 crans");
            expectWithinAbsoluteError (avant.front().second, 0.0, 1.0e-6);

            // Lisser une répétition ne touche pas l'original
            expect (retouche::repeter (d, 0));
            retouche::lisser (d.sections[1], "/pistes/2/volumeDb");
            const auto lisse = retouche::points (d.sections[1], "/pistes/2/volumeDb");
            const auto original = retouche::points (d.sections[0], "/pistes/2/volumeDb");
            expectWithinAbsoluteError (original[1].second, avant[1].second, 1.0e-9, "l'original n'a pas bougé");
            expect (std::abs (lisse[1].second - avant[1].second) > 0.1, "les crans sont adoucis");
            expectWithinAbsoluteError (lisse.back().second, avant.back().second, 1.0e-6, "la dernière valeur reste : la suite s'enchaîne sans saut");

            retouche::effacer (d.sections[0], "/pistes/2/volumeDb");
            expect (retouche::courbes (d.sections[0], 2).isEmpty(), "courbe effacée");
            expect (! retouche::courbes (d.sections[1], 2).isEmpty(), "la répétition garde la sienne");

            int reglages = 0;

            for (auto& e : d.sections[0].evenements)
                reglages += e.genre == G::reglages ? 1 : 0;

            expectEquals (reglages, 0, "les gestes vides disparaissent");
        }

        beginTest ("Un déroulé s'enregistre et se relit à l'identique ; le son se retrouve au bon endroit"_fr);
        {
            auto d = deroule;
            d.nom = "Version courte";
            retouche::couper (d, 0, 4.0);
            const auto fichier = racine.getChildFile ("deroules").getChildFile ("Version courte.json");
            expect (d.enregistrer (fichier));
            const auto relu = Deroule::charger (fichier);
            expect (relu.has_value() && texte (relu->versJson()) == texte (d.versJson()), "relu tel quel");

            // Groove commence 2 mesures (4 s à 120 BPM) après le début de la prise
            const double debut = prise.secondesDansLeSon (prise.debut);
            const double groove = prise.secondesDansLeSon (deroule.sections[1].debutDansLaPrise);
            expectWithinAbsoluteError (groove - debut, 4.0, 1.0e-6, "2 mesures plus loin dans le son");
            expect (debut >= 0.0 && debut < 0.2, "le son commence avec la prise : " + juce::String (debut, 3));

            auto avecTempo = prise;
            EvenementDePrise e;
            e.genre = G::reglages;
            e.temps = prise.debut + 4.0;
            juce::Array<juce::var> paire { juce::var ("/tempo"), juce::var (60.0) };
            e.changements = juce::Array<juce::var> { juce::var (paire) };
            avecTempo.evenements.insert (avecTempo.evenements.begin(), e);
            expectWithinAbsoluteError (avecTempo.secondesDansLeSon (prise.debut + 8.0) - debut, 2.0 + 4.0, 1.0e-6, "le tempo qui change est suivi");
        }

        beginTest ("L'onglet Retouche montre la prise en sections et ses courbes"_fr);
        {
            CarteSimulee carte (engine, 2);
            Moteur moteur (engine, EssaisEnchainements::morceauDEnchainement(), &banqueEssai());
            moteur.dossierDesPrises = [racine] { return racine; };
            PageRetouche page (engine, moteur);
            page.setSize (980, 640);
            page.ouvrirPrise (dossierDeLaPrise);
            expect (page.getDeroule() && page.getDeroule()->sections.size() == 3, "la prise ouverte en 3 sections");
            page.choisirSection (0);
            expectEquals (page.getLigneChoisie(), 2, "la ligne de la nappe, où le volume bouge, est choisie");

            auto image = page.createComponentSnapshot (page.getLocalBounds());
            const auto fichier = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("groovebox-onglet-retouche.png");
            fichier.deleteFile();
            juce::FileOutputStream flux (fichier);
            juce::PNGImageFormat().writeImageToStream (image, flux);
            logMessage ("  image de l'onglet : " + fichier.getFullPathName());

            page.ouvrirDeroule (racine.getChildFile ("deroules").getChildFile ("Version courte.json"));
            expect (page.getDeroule() && page.getDeroule()->sections.size() == 4, "le déroulé enregistré se rouvre");
        }
    }
};

//==============================================================================
class EssaisRejeu  : public juce::UnitTest
{
public:
    EssaisRejeu()  : juce::UnitTest ("Rejeu", "groovebox") {}

    /** Ce qu'on relève toutes les 250 ms : la scène de chaque piste, le volume de la nappe, le niveau du son. */
    struct Releve
    {
        std::vector<std::array<int, 4>> scenes;
        std::vector<float> volumes, niveaux;
    };

    static Morceau morceauDuRejeu()
    {
        auto m = EssaisEnchainements::morceauDEnchainement();
        m.scenes[0].nom = "Intro";
        m.scenes[1].nom = "Groove";
        m.scenes[2].nom = "Plein";
        m.scenes[0].clips[2] = EssaisEnchainements::clipTenu ("Do", 60);
        m.scenes[1].clips[2] = EssaisEnchainements::clipTenu ("Sol", 67);
        m.scenes[2].clips[2] = EssaisEnchainements::clipTenu ("Mi", 64);
        m.scenes[1].clips[1] = EssaisEnchainements::clipTenu ("Basse", 36);
        m.scenes[0].clips[3] = EssaisEnchainements::clipTenu ("Lead", 72);
        return m;
    }

    /** Joue par tranches de 250 ms, en relevant l'état après chaque tranche, et en faisant les gestes prévus. */
    static Releve jouer (Moteur& moteur, CarteSimulee& carte, double secondes, const std::map<int, std::function<void()>>& gestes = {})
    {
        Releve r;
        const int tranches = (int) (secondes * 4.0);

        for (int k = 0; k < tranches; ++k)
        {
            if (auto g = gestes.find (k); g != gestes.end())
                g->second();

            // Le volume au début de la tranche (juste après les gestes), les scènes à sa fin
            r.volumes.push_back (moteur.getMorceau().pistes[2].volumeDb);
            auto audio = EssaisEnchainements::jouerEtAvancer (moteur, carte, uneSeconde / 4);
            r.scenes.push_back ({ moteur.sceneJouee (0), moteur.sceneJouee (1), moteur.sceneJouee (2), moteur.sceneJouee (3) });
            r.niveaux.push_back (niveauEfficace (audio, 0));
        }

        return r;
    }

    void runTest() override
    {
        auto& engine = *te::Engine::getEngines()[0];
        const auto racine = EssaisPrises::dossierDEssai();

        // La prise d'origine : Intro, des crans de volume, une note, fondu vers Groove, coupe vers Plein, un clip seul
        Releve original;
        Deroule deroule;
        Morceau avantLaPrise;

        {
            CarteSimulee carte (engine, 2);
            Moteur moteur (engine, morceauDuRejeu(), &banqueEssai());
            moteur.dossierDesPrises = [racine] { return racine; };
            avantLaPrise = moteur.getMorceau();
            moteur.demarrerPrise();
            moteur.lancerScene (0);

            std::map<int, std::function<void()>> gestes;

            for (int k = 1; k <= 6; ++k)
                gestes[3 + k] = [&moteur, k] { moteur.modifier ([k] (Morceau& mm) { mm.pistes[2].volumeDb = -2.0f * (float) k; }, "encodeur 2"); };

            gestes[10] = [&moteur] { moteur.jouerNote (4, 76, 100); };
            gestes[12] = [&moteur] { moteur.relacherNote (4, 76); };
            gestes[14] = [&moteur] { moteur.lancerScene (1, EssaisEnchainements::enchainement (TypeEnchainement::fondu, 1)); };   // 3,5 s
            gestes[30] = [&moteur] { moteur.lancerScene (2, EssaisEnchainements::enchainement (TypeEnchainement::coupe, 1)); };   // 7,5 s
            gestes[38] = [&moteur] { moteur.lancerClip (3, 0); };                                                                  // 9,5 s
            original = jouer (moteur, carte, 12.0, gestes);

            const auto dossier = *moteur.arreterPrise();
            moteur.arreter();
            deroule = retouche::decouper (*Prise::charger (dossier.getChildFile ("prise.json")), dossier.getFileName());
            deroule.nom = "Essai";
        }

        beginTest ("Un déroulé tiré d'une prise se rejoue à l'identique"_fr);
        {
            CarteSimulee carte (engine, 2);
            Moteur moteur (engine, morceauDuRejeu(), &banqueEssai());
            const auto avant = moteur.getMorceau();

            expect (moteur.rejouer (deroule));
            expect (moteur.rejeuEnCours() && moteur.enLecture());
            const auto rejoue = jouer (moteur, carte, 12.0);

            int scenesDifferentes = 0, volumesDifferents = 0;
            double ecartDesNiveaux = 0.0, somme = 0.0;

            for (size_t k = 0; k < original.scenes.size() && k < rejoue.scenes.size(); ++k)
            {
                scenesDifferentes += original.scenes[k] != rejoue.scenes[k] ? 1 : 0;
                volumesDifferents += std::abs (original.volumes[k] - rejoue.volumes[k]) > 0.01f ? 1 : 0;
                ecartDesNiveaux += std::abs (original.niveaux[k] - rejoue.niveaux[k]);
                somme += original.niveaux[k];
            }

            logMessage ("  relevés différents : scènes " + juce::String (scenesDifferentes) + ", volume " + juce::String (volumesDifferents)
                        + " sur " + juce::String ((int) original.scenes.size()) + " ; écart de niveau " + juce::String (100.0 * ecartDesNiveaux / somme, 2) + " %");
            expectEquals (scenesDifferentes, 0, "les mêmes clips aux mêmes instants");
            expectEquals (volumesDifferents, 0, "les mêmes réglages au même moment");
            expect (ecartDesNiveaux < 0.03 * somme, "le même son");

            moteur.avancer();
            expect (! moteur.rejeuEnCours(), "à la fin du déroulé, le rejeu s'arrête");
            expect (! moteur.enLecture());
            expect (moteur.getMorceau() == avant, "le morceau revient tel qu'il était");
            expect (! moteur.peutAnnuler(), "le rejeu ne laisse rien dans l'historique");
        }

        beginTest ("Sections déplacées : le déroulé suit le nouvel ordre"_fr);
        {
            CarteSimulee carte (engine, 2);
            Moteur moteur (engine, morceauDuRejeu(), &banqueEssai());
            auto d = deroule;
            retouche::deplacer (d, 2, -1);      // Intro, Plein, Groove
            expect (moteur.rejouer (d));
            const auto r = jouer (moteur, carte, 9.0);

            // Intro jusqu'à 4 s, Plein de 4 à 7,4 s (1,7 mesure), puis Groove
            expectEquals (r.scenes[12][2], 0, "Intro d'abord");
            expectEquals (r.scenes[18][2], 2, "puis Plein");
            expectEquals (r.scenes[18][1], 1, "comme pendant la prise, la basse de Groove continue dans Plein (case vide)");
            expectEquals (r.scenes[34][2], 1, "puis Groove");
            expectEquals (r.scenes[34][1], 1, "avec sa basse");
            moteur.arreter();
        }

        beginTest ("Reprendre la main sur une piste, puis la rendre au déroulé"_fr);
        {
            CarteSimulee carte (engine, 2);
            Moteur moteur (engine, morceauDuRejeu(), &banqueEssai());
            moteur.rejouer (deroule);
            jouer (moteur, carte, 0.5);

            // On tourne le volume de la nappe : elle est à nous, les crans du déroulé ne la touchent plus
            moteur.modifier ([] (Morceau& mm) { mm.pistes[2].volumeDb = -30.0f; });
            expect (moteur.pisteReprise (2), "la nappe est reprise");
            expect (! moteur.pisteReprise (1));
            jouer (moteur, carte, 2.5);
            expectWithinAbsoluteError (moteur.getMorceau().pistes[2].volumeDb, -30.0f, 1.0e-4f, "le déroulé ne la touche plus");
            expectEquals (moteur.etatDuRejeu()->pistesReprises, 1);

            moteur.rendreLaMain (2);
            expect (! moteur.pisteReprise (2));
            expectWithinAbsoluteError (moteur.getMorceau().pistes[2].volumeDb, -12.0f, 1.0e-4f, "rendue : elle revient où en est le déroulé");

            // Lancer un clip reprend sa piste ; lancer une scène reprend tout
            moteur.lancerClip (3, 0);
            expect (moteur.pisteReprise (3));
            moteur.lancerScene (2);
            expectEquals (moteur.etatDuRejeu()->pistesReprises, nombreDePistes, "une scène lancée à la main : tout est repris");
            jouer (moteur, carte, 1.5);     // 4,5 s : Groove a commencé dans le déroulé
            expectEquals (moteur.etatDuRejeu()->numero, 2);
            expectEquals (moteur.sceneJouee (2), 2, "c'est la scène lancée qui joue, pas Groove");
            moteur.rendreLaMain (-1);
            expectEquals (moteur.etatDuRejeu()->pistesReprises, 0);
            jouer (moteur, carte, 2.0);     // 6,5 s
            expectEquals (moteur.sceneJouee (2), 1, "rendu : le déroulé remet Groove, à la mesure suivante");
            expectEquals (moteur.sceneJouee (1), 1, "et sa basse");
            moteur.arreter();
        }

        beginTest ("Section ouverte : elle tourne jusqu'à ce qu'on passe à la suite"_fr);
        {
            CarteSimulee carte (engine, 2);
            Moteur moteur (engine, morceauDuRejeu(), &banqueEssai());
            auto d = deroule;
            d.sections[0].ouverte = true;
            moteur.rejouer (d);
            jouer (moteur, carte, 10.5);
            auto e = moteur.etatDuRejeu();
            expect (e && e->numero == 1 && e->ouverte, "après plus de deux tours, toujours dans Intro");
            expectEquals (moteur.sceneJouee (2), 0);
            expectWithinAbsoluteError (moteur.getMorceau().pistes[2].volumeDb, -12.0f, 1.0e-4f, "les crans rejoués à chaque tour");

            moteur.passerALaSuite();
            e = moteur.etatDuRejeu();
            expect (e && e->suiteDemandee && e->mesuresRestantes <= 1.0, "suite à la prochaine mesure");
            jouer (moteur, carte, 2.5);
            e = moteur.etatDuRejeu();
            expect (e && e->numero == 2, "on est passé à Groove");
            expectEquals (moteur.sceneJouee (2), 1);
            moteur.arreter();
        }

        beginTest ("Rejouer depuis une section, et le clic pendant le rejeu"_fr);
        {
            CarteSimulee carte (engine, 4);
            auto m = morceauDuRejeu();
            Moteur moteur (engine, m, &banqueEssai());
            auto d = deroule;

            for (auto& s : d.sections)
            {
                s.etat.getDynamicObject()->setProperty ("musiqueDansCasqueDb", -60.0);
                s.etat.getProperty ("clic", {}).getDynamicObject()->setProperty ("actif", true);
            }

            expect (moteur.rejouer (d, 1));
            const auto e = moteur.etatDuRejeu();
            expect (e && e->numero == 2 && e->section == "Groove", "on commence à Groove");
            EssaisEnchainements::jouerEtAvancer (moteur, carte, uneSeconde / 2);
            expectEquals (moteur.sceneJouee (2), 1);
            expectEquals (moteur.sceneJouee (3), 0, "le lead d'Intro, qui jouait au début de Groove, joue aussi");

            auto audio = carte.jouer (2 * uneSeconde);
            DetecteurDeCoups detecteur;
            detecteur.analyser (audio, 2);
            expect (detecteur.coups.size() == 4, "le clic continue dans le casque : " + juce::String ((int) detecteur.coups.size()) + " coups en 2 s");
            moteur.arreter();
        }

        beginTest ("Push : Play rejoue le déroulé choisi, Shift + Page droite passe à la suite"_fr);
        {
            using T = Evenement::Type;
            CarteSimulee carte (engine, 2);
            Moteur moteur (engine, morceauDuRejeu(), &banqueEssai());
            Surface surface;
            Pilote pilote (moteur, surface);
            auto appui = [&] (Bouton b, bool bas) { surface.recevoir ({ T::bouton, (int) b, bas ? 127 : 0, bas }); };
            auto bouton = [&] (Bouton b) { appui (b, true); appui (b, false); };

            auto d = deroule;
            d.sections[0].ouverte = true;
            moteur.deroulePourPlay = d;
            bouton (Bouton::play);
            expect (moteur.rejeuEnCours(), "Play : rejeu du déroulé choisi");
            jouer (moteur, carte, 1.0);

            juce::Image image (juce::Image::RGB, EtatSurface::largeurEcran, EtatSurface::hauteurEcran, true);
            pilote.dessinerEcran (image);
            {
                const auto fichier = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("groovebox-push-rejeu.png");
                fichier.deleteFile();
                juce::FileOutputStream flux (fichier);
                juce::PNGImageFormat().writeImageToStream (image, flux);
            }

            appui (Bouton::shift, true);
            bouton (Bouton::pageDroite);
            appui (Bouton::shift, false);
            expect (moteur.etatDuRejeu()->suiteDemandee, "Shift + Page droite : suite demandée");

            moteur.modifier ([] (Morceau& mm) { mm.pistes[5].volumeDb = -20.0f; });
            expect (moteur.pisteReprise (5));
            appui (Bouton::shift, true);
            bouton ((Bouton) (premierBoutonDuBas + 5));
            appui (Bouton::shift, false);
            expect (! moteur.pisteReprise (5), "Shift + bouton de piste : rendue au déroulé");

            bouton (Bouton::play);
            expect (! moteur.rejeuEnCours() && ! moteur.enLecture(), "Play pendant le rejeu : arrêt");
        }
    }
};

//==============================================================================
/** Le critère de l'étape 9 : une impro de 10 minutes devient un déroulé de 4 minutes rejoué à l'identique.
    Long (environ une minute) : hors des essais habituels, se lance avec l'argument « Rejeu long ». */
class EssaisRejeuLong  : public juce::UnitTest
{
public:
    EssaisRejeuLong()  : juce::UnitTest ("Rejeu long", "groovebox-long") {}

    void runTest() override
    {
        auto& engine = *te::Engine::getEngines()[0];
        const auto racine = EssaisPrises::dossierDEssai();
        auto m = EssaisRejeu::morceauDuRejeu();
        m.scenes[3].nom = "Pause";
        m.scenes[3].clips[2] = EssaisEnchainements::clipTenu ("La", 69);
        m.scenes[2].clips[1] = EssaisEnchainements::clipTenu ("Basse 2", 41);

        // L'impro : un geste toutes les 2 secondes, tirés au sort (toujours les mêmes)
        EssaisRejeu::Releve original;
        Deroule deroule;
        double mesure1 = 0.0;

        beginTest ("Une impro de 10 minutes jouée et capturée"_fr);
        {
            CarteSimulee carte (engine, 2);
            Moteur moteur (engine, m, &banqueEssai());
            moteur.dossierDesPrises = [racine] { return racine; };
            mesure1 = moteur.tempsDeLaMesure1();
            moteur.demarrerPrise();
            moteur.lancerScene (0);

            juce::Random hasard (7);
            std::map<int, std::function<void()>> gestes;

            for (int k = 6; k < 4 * 600; k += 8)
            {
                const int choix = hasard.nextInt (10);
                const int scene = hasard.nextInt (4), piste = 1 + hasard.nextInt (3);
                const float x = hasard.nextFloat();

                if (choix < 3)
                    gestes[k] = [&moteur, scene, x] { moteur.lancerScene (scene, EssaisEnchainements::enchainement ((TypeEnchainement) juce::jlimit (0, 4, (int) (x * 5.0f)), 1 + (int) (x * 1.9f))); };
                else if (choix < 7)
                    gestes[k] = [&moteur, piste, x] { moteur.modifier ([piste, x] (Morceau& mm) { mm.pistes[(size_t) piste].volumeDb = -20.0f * x; mm.pistes[(size_t) piste].panoramique = x - 0.5f; }, "encodeur " + juce::String (piste)); };
                else if (choix < 9)
                    gestes[k] = [&moteur, piste, scene] { moteur.lancerClip (piste, scene); };
                else
                    gestes[k] = [&moteur, x] { moteur.filtreGlobal (0.5f + 0.3f * (x - 0.5f)); };
            }

            const auto debut = juce::Time::getMillisecondCounterHiRes();
            original = EssaisRejeu::jouer (moteur, carte, 600.0, gestes);
            logMessage ("  10 minutes jouées en " + juce::String ((juce::Time::getMillisecondCounterHiRes() - debut) / 1000.0, 1) + " s");

            const auto dossier = *moteur.arreterPrise();
            moteur.arreter();
            const auto prise = *Prise::charger (dossier.getChildFile ("prise.json"));
            logMessage ("  " + juce::String ((int) prise.evenements.size()) + " gestes");
            deroule = retouche::decouper (prise, dossier.getFileName());
            expect (deroule.sections.size() > 20, juce::String ((int) deroule.sections.size()) + " sections");
            expectWithinAbsoluteError (deroule.longueur() / 2.0, 600.0, 1.0, "10 minutes de sections");
        }

        beginTest ("4 minutes prises au milieu, rejouées à l'identique"_fr);
        {
            // On garde les sections de la 3e à la 7e minute, en commençant par une section entrée en coupe (sans fondu à moitié fait)
            Deroule court;
            court.nom = "4 minutes";
            double debut = -1.0;

            for (auto& s : deroule.sections)
            {
                const double secondes = (s.debutDansLaPrise - mesure1) / 2.0;

                if (debut < 0.0 && secondes >= 180.0 && s.entree && s.entree->type == TypeEnchainement::coupe)
                    debut = secondes;

                if (debut >= 0.0 && secondes < debut + 240.0)
                    court.sections.push_back (s);
            }

            expect (debut > 0.0 && court.sections.size() > 5, "un passage de 4 minutes");
            logMessage ("  de " + juce::String (debut, 0) + " s à " + juce::String (debut + court.longueur() / 2.0, 0) + " s, "
                        + juce::String ((int) court.sections.size()) + " sections");

            CarteSimulee carte (engine, 2);
            Moteur moteur (engine, m, &banqueEssai());
            expect (moteur.rejouer (court));
            const auto rejoue = EssaisRejeu::jouer (moteur, carte, court.longueur() / 2.0);
            const int decalage = (int) std::round (debut * 4.0);

            int scenesDifferentes = 0, volumesDifferents = 0;
            double ecart = 0.0, somme = 0.0;

            for (size_t k = 0; k < rejoue.scenes.size() && k + (size_t) decalage < original.scenes.size(); ++k)
            {
                const auto o = (size_t) decalage + k;
                if (rejoue.scenes[k] != original.scenes[o])
                {
                    ++scenesDifferentes;
                    auto texteDe = [] (const std::array<int, 4>& a) { return juce::String (a[0]) + "," + juce::String (a[1]) + "," + juce::String (a[2]) + "," + juce::String (a[3]); };
                    logMessage ("  à " + juce::String ((double) o / 4.0, 2) + " s : rejoué " + texteDe (rejoue.scenes[k]) + ", d'origine " + texteDe (original.scenes[o])
                                + " ; niveau rejoué " + juce::String (rejoue.niveaux[k], 5) + ", d'origine " + juce::String (original.niveaux[o], 5));
                }

                volumesDifferents += std::abs (rejoue.volumes[k] - original.volumes[o]) > 0.01f ? 1 : 0;
                ecart += std::abs (rejoue.niveaux[k] - original.niveaux[o]);
                somme += original.niveaux[o];
            }

            logMessage ("  relevés différents : scènes " + juce::String (scenesDifferentes) + ", volume " + juce::String (volumesDifferents)
                        + " sur " + juce::String ((int) rejoue.scenes.size()) + " ; écart de niveau " + juce::String (100.0 * ecart / juce::jmax (1.0e-9, somme), 2) + " %");
            // Pendant qu'un enchaînement passe d'une voix à l'autre, les deux clips jouent : la « scène jouée » relevée peut
            // différer d'une tranche sans que le son change (vérifié ci-dessus, niveau par niveau)
            expect (scenesDifferentes <= (int) rejoue.scenes.size() / 100, "les mêmes clips aux mêmes instants");
            expectEquals (volumesDifferents, 0, "les mêmes réglages");
            expect (ecart < 0.01 * somme, "le même son");
            EssaisEnchainements::jouerEtAvancer (moteur, carte, uneSeconde / 2);
            expect (! moteur.rejeuEnCours(), "fini au bout des 4 minutes");
        }
    }
};

//==============================================================================
/** La Démo 2 rejouée en entier, avec Surge XT : niveaux de chaque partie et fichier à écouter. Hors série (« Démo 2 »). */
class EssaisDemo2  : public juce::UnitTest
{
public:
    EssaisDemo2()  : juce::UnitTest ("Démo 2"_fr, "groovebox-long") {}

    void runTest() override
    {
        auto& engine = *te::Engine::getEngines()[0];

        beginTest ("La Démo 2 : 6 scènes, un déroulé d'un bout à l'autre"_fr);
        const auto m = morceauDeDemonstration2();
        const auto d = derouleDeLaDemo2 (m);
        expect (m.mesure == Mesure::standard (6, 8));
        expectEquals ((int) d.sections.size(), 6);
        expectWithinAbsoluteError (d.longueur() / 6.0, 36.0, 1.0e-6, "36 mesures");
        expect (Morceau::depuisJson (m.versJson()) == m, "se sauvegarde et se relit");

        beginTest ("Rejouée avec Surge XT : chaque partie sonne, sans saturer"_fr);
        surge::autoriser (true);
        CarteSimulee carte (engine, 2);
        Moteur moteur (engine, m, &banqueEssai());
        logMessage (moteur.joueAvecSurge (1) ? "  avec Surge XT" : "  sans Surge XT (sons provisoires)");
        expect (moteur.rejouer (d));

        juce::AudioBuffer<float> tout (2, 0);
        std::vector<std::pair<juce::String, int>> debuts;
        juce::String section;

        for (int k = 0; k < 4 * 75 && moteur.rejeuEnCours(); ++k)
        {
            if (auto e = moteur.etatDuRejeu(); e && e->section != section)
            {
                section = e->section;
                debuts.push_back ({ section, tout.getNumSamples() });
            }

            auto morceau = EssaisEnchainements::jouerEtAvancer (moteur, carte, uneSeconde / 4);
            const int avant = tout.getNumSamples();
            tout.setSize (2, avant + morceau.getNumSamples(), true, true);

            for (int c = 0; c < 2; ++c)
                tout.copyFrom (c, avant, morceau, c, 0, morceau.getNumSamples());
        }

        surge::autoriser (false);
        expect (! moteur.rejeuEnCours(), "le déroulé va jusqu'au bout");
        logMessage ("  durée " + juce::String (tout.getNumSamples() / frequence, 1) + " s, crête "
                    + juce::String (juce::Decibels::gainToDecibels (tout.getMagnitude (0, tout.getNumSamples())), 1) + " dB");
        expect (tout.getMagnitude (0, tout.getNumSamples()) < 0.95f, "sous la protection de sortie");

        debuts.push_back ({ "fin", tout.getNumSamples() });

        for (size_t i = 0; i + 1 < debuts.size(); ++i)
        {
            const float niveau = niveauEfficace (tout, 0, debuts[i].second, debuts[i + 1].second);
            logMessage ("  " + debuts[i].first + " : " + juce::String (juce::Decibels::gainToDecibels (niveau), 1) + " dB efficaces");
            expect (niveau > 0.005f, debuts[i].first + " sonne");
        }

        const auto fichier = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("Démo 2.wav"_fr);
        fichier.deleteFile();
        juce::WavAudioFormat wav;

        if (auto ecrivain = std::unique_ptr<juce::AudioFormatWriter> (wav.createWriterFor (new juce::FileOutputStream (fichier), frequence, 2, 24, {}, 0)))
            ecrivain->writeFromAudioSampleBuffer (tout, 0, tout.getNumSamples());

        logMessage ("  fichier : " + fichier.getFullPathName());
    }
};

//==============================================================================
class EssaisSampler  : public juce::UnitTest
{
public:
    EssaisSampler()  : juce::UnitTest ("Sampler", "groovebox") {}

    /** Écrit un son d'essai dans la banque (une seule fois). */
    static void ecrire (const juce::String& chemin, const juce::AudioBuffer<float>& b)
    {
        auto f = banqueEssai().getRacine().getChildFile (chemin);

        if (f.existsAsFile())
            return;

        f.getParentDirectory().createDirectory();
        juce::WavAudioFormat wav;
        auto flux = f.createOutputStream();
        std::unique_ptr<juce::AudioFormatWriter> e (wav.createWriterFor (flux.release(), 48000.0, (unsigned int) b.getNumChannels(), 24, {}, 0));
        e->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
    }

    /** 16 tranches de 0,1 s : la tranche k est un sinus de 200 + 100 k Hz. */
    static void fichierEnTranches()
    {
        juce::AudioBuffer<float> b (1, 16 * 4800);

        for (int i = 0; i < b.getNumSamples(); ++i)
        {
            const int k = i / 4800;
            b.setSample (0, i, 0.5f * (float) std::sin (juce::MathConstants<double>::twoPi * (200.0 + 100.0 * k) * i / 48000.0));
        }

        ecrire ("Essais/Tranches.wav", b);
    }

    /** Une mesure de 4/4 à 120 (2 s) : un clic court sur chaque temps, et un sinus de 440 Hz très faible entre les clics. */
    static void fichierBoucle()
    {
        juce::AudioBuffer<float> b (2, 96000);
        b.clear();

        for (int i = 0; i < b.getNumSamples(); ++i)
        {
            float x = 0.02f * (float) std::sin (juce::MathConstants<double>::twoPi * 440.0 * i / 48000.0);

            if (i % 24000 < 240)
                x += 0.8f * (1.0f - (float) (i % 24000) / 240.0f);

            b.setSample (0, i, x);
            b.setSample (1, i, x);
        }

        ecrire ("Essais/Boucle 120.wav", b);
    }

    /** Hauteur d'un son riche en harmoniques : la période qui ressemble le plus au son lui-même (de 80 à 1000 Hz). */
    static double hauteurParAutocorrelation (const juce::AudioBuffer<float>& a, int debut, int longueur)
    {
        auto d = a.getReadPointer (0);
        int meilleure = 48;
        double meilleurScore = -1.0e9;

        for (int periode = 48; periode <= 600; ++periode)
        {
            double score = 0.0;

            for (int i = debut; i < debut + longueur; ++i)
                score += d[i] * d[i + periode];

            if (score > meilleurScore)
            {
                meilleurScore = score;
                meilleure = periode;
            }
        }

        return 48000.0 / meilleure;
    }

    static double frequence (const juce::AudioBuffer<float>& a, int debut, int fin)
    {
        auto d = a.getReadPointer (0);
        int passages = 0;

        for (int i = debut + 1; i < fin; ++i)
            if (d[i - 1] <= 0.0f && d[i] > 0.0f)
                ++passages;

        return passages * 48000.0 / (fin - debut);
    }

    /** Début des coups (montées au-dessus de 0,2 après un passage sous 0,05 pendant 20 ms). */
    static std::vector<int> coups (const juce::AudioBuffer<float>& a, float seuil = 0.2f)
    {
        std::vector<int> liste;
        auto d = a.getReadPointer (0);
        int calme = 0;

        for (int i = 0; i < a.getNumSamples(); ++i)
        {
            if (std::abs (d[i]) > seuil && calme > 960)
                liste.push_back (i);

            calme = std::abs (d[i]) < 0.05f ? calme + 1 : 0;
        }

        return liste;
    }

    static Morceau morceauSampler (ReglagesSampler r, double tempo = 120.0)
    {
        auto m = morceauAvec (tempo, Mesure::standard (4, 4));
        m.clic.actif = false;
        m.compressionDeSortie = 0.0f;
        m.pistes[0].son = SonProvisoire::sampler;
        m.pistes[0].sampler = r;
        return m;
    }

    static juce::AudioBuffer<float> jouer (te::Engine& engine, const Morceau& m, int note, int duree, int relacheApres = -1)
    {
        CarteSimulee carte (engine, 2);
        Moteur moteur (engine, m, &banqueEssai());
        carte.jouer (256);
        moteur.jouerNote (0, note, 127);

        if (relacheApres < 0)
            return carte.jouer (duree);

        auto avant = carte.jouer (relacheApres);
        moteur.relacherNote (0, note);
        auto apres = carte.jouer (duree - relacheApres);
        juce::AudioBuffer<float> tout (2, duree);
        tout.copyFrom (0, 0, avant, 0, 0, relacheApres);
        tout.copyFrom (0, relacheApres, apres, 0, 0, duree - relacheApres);
        return tout;
    }

    void runTest() override
    {
        auto& engine = *te::Engine::getEngines()[0];
        EssaisBatterie::fichierSinus();
        fichierEnTranches();
        fichierBoucle();

        beginTest ("Réglages du sampler : JSON et longueur devinée des boucles"_fr);
        {
            ReglagesSampler r;
            r.son = "Boucles/Essai.wav";
            r.mode = ModeSampler::decoupe;
            r.debut = 0.25f; r.longueur = 0.5f; r.hauteur = -7.0f; r.filtre = 0.3f; r.envers = true; r.mesuresDeBoucle = 4;
            expect (ReglagesSampler::depuisJson (juce::JSON::parse (juce::JSON::toString (r.versJson()))) == r, "relu à l'identique");

            auto m = morceauSampler (r);
            const auto relu = Morceau::depuisJson (m.versJson());
            expect (relu.pistes[0].estUnSampler() && relu.pistes[0].sampler == r, "le morceau garde la piste sampler");

            ReglagesSampler auto_;
            expectEquals (sampler::mesuresDeLaBoucle (auto_, 4.0, 120.0, Mesure::standard (4, 4)), 2, "4 s à 120 en 4/4 : 2 mesures");
            expectEquals (sampler::mesuresDeLaBoucle (auto_, 2.4, 120.0, Mesure::standard (4, 4)), 1, "2,4 s : 1 mesure (100 BPM)");
            expectEquals (sampler::mesuresDeLaBoucle (auto_, 3.0, 120.0, Mesure::standard (6, 8)), 2, "3 s en 6/8 à 120 : 2 mesures");
            ReglagesSampler jungle;
            jungle.son = "Boucles/Jungle 170.wav";
            expectEquals (sampler::tempoDansLeNom (jungle.son), 170.0);
            expectEquals (sampler::tempoDansLeNom ("Mes sons/loop_92bpm.wav"), 92.0);
            expectEquals (sampler::tempoDansLeNom ("Voix/Allez.aiff"), 0.0);
            expectEquals (sampler::mesuresDeLaBoucle (jungle, 2.0 * 4 * 60.0 / 170.0, 120.0, Mesure::standard (4, 4)), 2, "Jungle 170 : 2 mesures, d'après son nom");

            const auto [d, f] = r.partieJouee();
            expectWithinAbsoluteError (d, 0.25, 1.0e-6);
            expectWithinAbsoluteError (f, 0.25 + 0.75 * 0.5, 1.0e-6);
        }

        beginTest ("Sons de départ du sampler : boucles à la bonne durée, textures en do, voix du Mac"_fr);
        {
            for (auto& b : fabriqueDeSons::bouclesDeDepart())
            {
                auto* e = banqueEssai().charger ("Boucles/" + b.nom + ".wav");
                expect (e != nullptr, b.nom);

                if (e == nullptr)
                    continue;

                const double attendu = b.mesures * b.numerateur * 4.0 / b.denominateur * 60.0 / b.tempo;
                expectWithinAbsoluteError (e->audio.getNumSamples() / e->frequence, attendu, 0.001, b.nom + " : durée"_fr);
                expect (e->audio.getMagnitude (0, e->audio.getNumSamples()) > 0.5f, b.nom + " : niveau");

                ReglagesSampler r;
                r.son = "Boucles/" + b.nom + ".wav";
                expectEquals (sampler::mesuresDeLaBoucle (r, attendu, 120.0, Mesure::standard (b.numerateur, b.denominateur)), b.mesures, b.nom + " : mesures devinées"_fr);
            }

            for (auto& t : { "Textures/Drone en do.wav", "Textures/Cloche en do.wav", "Textures/Corde en do.wav", "Textures/Souffle.wav",
                             "One-shots/Laser.wav", "One-shots/Gong.wav", "One-shots/Bulle.wav", "One-shots/Ressort.wav" })
            {
                auto* e = banqueEssai().charger (t);
                expect (e != nullptr && e->audio.getMagnitude (0, e->audio.getNumSamples()) > 0.3f, t);
            }

            expect (banqueEssai().charger ("One-shots/Sirène dub.wav"_fr) != nullptr, "sirène");

            // La corde en do est accordée sur la note 60
            if (auto* corde = banqueEssai().charger ("Textures/Corde en do.wav"))
                expectWithinAbsoluteError (hauteurParAutocorrelation (corde->audio, 4800, 9600), 261.6, 4.0, "corde en do");

            // Voix : la synthèse vocale du Mac, dans un dossier à part
            auto dossier = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("groovebox-essais-voix");
            dossier.deleteRecursively();
            fabriqueDeSons::assurerSonsDuSampler (dossier, true);
            BanqueDeSons voix (dossier);
            const auto liste = voix.sons ("Voix");
            expect (liste.size() == 6, juce::String (liste.size()) + " voix");

            if (auto* e = liste.isEmpty() ? nullptr : voix.charger (liste[0]))
                expect (e->audio.getNumSamples() > 4800 && e->audio.getMagnitude (0, e->audio.getNumSamples()) > 0.1f, "voix audible : " + liste[0]);

            // Importer : copier un son glissé sur la fenêtre dans « Mes sons »
            const auto importes = voix.importer ({ dossier.getChildFile ("One-shots/Gong.wav").getFullPathName(),
                                                   banqueEssai().getRacine().getChildFile ("One-shots/Gong.wav").getFullPathName(),
                                                   dossier.getChildFile ("rien.txt").getFullPathName() });
            expectEquals (importes.size(), 2);
            expectEquals (importes[0], juce::String ("One-shots/Gong.wav"), "déjà dans Sons : pris tel quel"_fr);
            expect (importes[1].startsWith ("Mes sons/Gong") && voix.charger (importes[1]) != nullptr, "copié dans Mes sons : " + importes[1]);
            dossier.deleteRecursively();
        }

        beginTest ("Un coup : la note transpose, le relâché ne coupe pas ; en notes, le relâché éteint"_fr);
        {
            ReglagesSampler r;
            r.son = "Essais/Sinus 440.wav";
            auto a = jouer (engine, morceauSampler (r), 60, 24000);
            expectWithinAbsoluteError (frequence (a, 4800, 19200), 440.0, 8.0, "note 60 : hauteur d'origine");
            a = jouer (engine, morceauSampler (r), 72, 24000);
            expectWithinAbsoluteError (frequence (a, 4800, 19200), 880.0, 12.0, "note 72 : une octave au-dessus");

            r.hauteur = 7.0f;
            a = jouer (engine, morceauSampler (r), 60, 24000);
            expectWithinAbsoluteError (frequence (a, 4800, 19200), 440.0 * std::pow (2.0, 7.0 / 12.0), 10.0, "hauteur +7");
            r.hauteur = 0.0f;

            a = jouer (engine, morceauSampler (r), 60, 36000, 4800);
            expect (niveauEfficace (a, 0, 24000, 30000) > 0.1f, "un coup : le son continue après le relâché"_fr);

            r.mode = ModeSampler::notes;
            r.relache = 0.0f;
            a = jouer (engine, morceauSampler (r), 60, 36000, 4800);
            expect (niveauEfficace (a, 0, 2000, 4000) > 0.1f, "en notes : le son joue tant que la note est tenue");
            expect (niveauEfficace (a, 0, 24000, 30000) < 0.001f, "en notes : relâché, il s'éteint"_fr);
        }

        beginTest ("Découpé : chaque pad joue sa tranche ; à l'envers, le son part de la fin"_fr);
        {
            ReglagesSampler r;
            r.son = "Essais/Tranches.wav";
            r.mode = ModeSampler::decoupe;
            auto a = jouer (engine, morceauSampler (r), 36 + 5, 9600);
            expectWithinAbsoluteError (frequence (a, 300, 4300), 700.0, 15.0, "tranche 6 : 700 Hz");
            expect (niveauEfficace (a, 0, 5400, 9600) < 0.001f, "la tranche dure 0,1 s");
            expect (niveauEfficace (jouer (engine, morceauSampler (r), 70, 4800), 0) < 0.001f, "note hors des 16 pads : rien");

            r.mode = ModeSampler::unCoup;
            r.envers = true;
            a = jouer (engine, morceauSampler (r), 60, 9600);
            expectWithinAbsoluteError (frequence (a, 300, 4300), 1700.0, 25.0, "à l'envers : la dernière tranche d'abord"_fr);

            r.envers = false;
            r.debut = 0.5f;
            a = jouer (engine, morceauSampler (r), 60, 9600);
            expectWithinAbsoluteError (frequence (a, 300, 4300), 1000.0, 20.0, "début à la moitié : la tranche 9"_fr);
        }

        beginTest ("Filtre : passe-bas et passe-haut atténuent un sinus de 440 Hz"_fr);
        {
            ReglagesSampler r;
            r.son = "Essais/Sinus 440.wav";
            const auto neutre = niveauEfficace (jouer (engine, morceauSampler (r), 60, 24000), 0, 4800, 19200);
            r.filtre = -1.0f;
            const auto bas = niveauEfficace (jouer (engine, morceauSampler (r), 60, 24000), 0, 4800, 19200);
            r.filtre = 1.0f;
            const auto haut = niveauEfficace (jouer (engine, morceauSampler (r), 60, 24000), 0, 4800, 19200);
            expect (neutre > 0.1f);
            expect (bas < neutre * 0.3f, "passe-bas : " + juce::String (bas / neutre, 3));
            expect (haut < neutre * 0.1f, "passe-haut : " + juce::String (haut / neutre, 3));
        }

        beginTest ("Étirer une boucle : la durée suit le tempo, les coups restent à leur place, la hauteur ne change pas"_fr);
        {
            auto* son = banqueEssai().charger ("Essais/Boucle 120.wav");
            expect (son != nullptr);

            for (double secondes : { 2.4, 1.6 })
            {
                auto e = EtireurDeBoucles::etirer (*son, 0.0, 1.0, secondes, 0.0f, 48000.0);
                expect (e != nullptr);

                if (e == nullptr)
                    continue;

                const int n = e->audio.getNumSamples();
                expectEquals (n, juce::roundToInt (secondes * 48000.0));

                // La boucle jouée deux fois : un coup peut tomber juste avant la fin du tour et s'enchaîner avec le début
                juce::AudioBuffer<float> deuxTours (1, 2 * n);
                deuxTours.copyFrom (0, 0, e->audio, 0, 0, n);
                deuxTours.copyFrom (0, n, e->audio, 0, 0, n);
                const double periode = n / 4.0;
                int compte = 0;
                double ecartMax = 0.0;

                for (auto c : coups (deuxTours))
                    if (c >= periode / 2 && c < n + periode / 2)
                    {
                        ++compte;
                        ecartMax = std::max (ecartMax, std::abs (c - std::round (c / periode) * periode));
                    }

                expectEquals (compte, 4, "coups par tour à " + juce::String (secondes) + " s");
                expect (ecartMax < 0.012 * 48000.0, "coups à leur place à " + juce::String (secondes) + " s : écart max "_fr + juce::String (ecartMax / 48.0, 1) + " ms");
            }

            auto e = EtireurDeBoucles::etirer (*son, 0.0, 1.0, 2.4, 0.0f, 48000.0);
            expectWithinAbsoluteError (frequence (e->audio, 3000, 11000), 440.0, 15.0, "étirée, le sinus reste à 440 Hz"_fr);
        }

        beginTest ("Boucle dans le moteur : calée au tempo du morceau, relancée à chaque tour du clip"_fr);
        {
            ReglagesSampler r;
            r.son = "Essais/Boucle 120.wav";
            r.mode = ModeSampler::boucle;
            auto m = morceauSampler (r, 100.0);
            m.assurerScenes (1);
            m.scenes[0].clips[0] = Clip { "Boucle", 1, { { 0.0, 4.0, 60, 100 } } };

            CarteSimulee carte (engine, 2);
            Moteur moteur (engine, m, &banqueEssai());
            expectWithinAbsoluteError (moteur.secondesDeLaBoucle (0), 2.4, 1.0e-9, "1 mesure à 100 : 2,4 s");
            moteur.attendreLesBoucles();
            expect (moteur.samplerDeLaVoix (0, 0)->getBoucleEtiree() != nullptr, "la boucle étirée est installée");
            moteur.lancerClip (0, 0);
            carte.jouer (128);
            moteur.avancer();
            auto a = carte.jouer (8 * uneSeconde);
            const auto c = coups (a);
            expect (c.size() >= 12, juce::String ((int) c.size()) + " coups");

            double ecartMax = 0.0;

            for (size_t k = 1; k < c.size(); ++k)
                ecartMax = std::max (ecartMax, std::abs ((double) (c[k] - c[k - 1]) - 0.6 * 48000.0));

            expect (ecartMax < 0.012 * 48000.0, "coups tous les 0,6 s, écart max " + juce::String (ecartMax / 48.0, 1) + " ms");

            // Le tempo change : la boucle est étirée de nouveau
            moteur.modifier ([] (Morceau& x) { x.tempo = 150.0; });
            moteur.attendreLesBoucles();
            const auto* etiree = moteur.samplerDeLaVoix (0, 0)->getBoucleEtiree();
            expect (etiree != nullptr && std::abs (etiree->audio.getNumSamples() - 1.6 * 48000.0) < 2.0, "à 150 : 1,6 s");
        }

        beginTest ("Push : Browse change la machine, choisit une boucle, les pages du sampler, les tranches au pas à pas"_fr);
        {
            using T = Evenement::Type;
            CarteSimulee carte (engine, 2);
            auto m = morceauAvec (120.0, Mesure::standard (4, 4));
            m.assurerScenes (8);
            Moteur moteur (engine, m, &banqueEssai());
            Surface surface;
            Pilote pilote (moteur, surface);
            auto bouton = [&] (Bouton b) { surface.recevoir ({ T::bouton, (int) b, 1, true }); surface.recevoir ({ T::bouton, (int) b, 0, false }); };
            auto tourner = [&] (int encodeur, int pas) { surface.recevoir ({ T::encodeurTourne, encodeur, pas, false }); };

            // Piste 3 (Nappe) choisie, Browse : on n'ouvre pas sur « Machine »
            surface.recevoir ({ T::bouton, premierBoutonDuBas + 2, 1, true });
            surface.recevoir ({ T::bouton, premierBoutonDuBas + 2, 0, false });
            bouton (Bouton::browse);
            expect (pilote.getPage() == Pilote::Page::browse);
            tourner (0, 400);    // Machine, la dernière catégorie : Batterie, Synthé, Sampler
            tourner (1, 16);
            expect (moteur.getMorceau().pistes[2].estUnSampler(), "Browse, Machine : la piste devient un sampler");
            expectEquals (moteur.getMorceau().pistes[2].nom, juce::String ("Sampler"), "elle prend le nom de sa machine");

            // Catégorie Boucles (la première du sampler), le premier son : le mode passe en boucle, un clip de boucle est écrit
            tourner (0, -400);
            tourner (1, 8);
            tourner (1, -8);
            const auto& r = moteur.getMorceau().pistes[2].sampler;
            expect (r.son.startsWith ("Boucles/"), "son choisi : " + r.son);
            expect (r.mode == ModeSampler::boucle, "une boucle se joue en boucle");
            const auto* clip = moteur.getMorceau().clip (2, 0);
            expect (clip != nullptr && clip->nom == "Boucle" && clip->notes.size() == 1, "clip de boucle écrit");

            if (clip != nullptr)
                expectEquals (clip->longueurMesures, juce::roundToInt (moteur.secondesDeLaBoucle (2) / 2.0), "le clip a la longueur de la boucle");

            // Device : page Sampler (8 réglages), puis Commun ; la hauteur se règle
            bouton (Bouton::device);
            expect (pilote.getPage() == Pilote::Page::sampler, "Device : page du sampler");
            tourner (2, 50);
            expectWithinAbsoluteError (moteur.getMorceau().pistes[2].sampler.hauteur, 5.0f, 0.01f, "encodeur 3 : hauteur");
            bouton (Bouton::device);
            expect (pilote.getPage() == Pilote::Page::communSampler, "boucle : pas de générateur, la page Commun");

            // Mode découpé : pads en 4 sur 4, le pas à pas écrit la tranche 1
            tourner (0, -8);
            expect (moteur.getMorceau().pistes[2].sampler.mode == ModeSampler::decoupe, "mode découpé"_fr);
            moteur.modifier ([] (Morceau& x) { x.scenes[0].clips[2].reset(); });
            bouton (Bouton::note);
            surface.recevoir (Evenement::pad (0, 1, 100));
            surface.recevoir ({ T::pad, 1, 0, false });
            expectEquals (pilote.getAnnonce().titre, juce::String ("Tranche 2"));
            surface.recevoir (Evenement::pad (7, 0, 100));     // premier pas
            clip = moteur.getMorceau().clip (2, 0);
            expect (clip != nullptr && clip->notes.size() == 1 && clip->notes[0].hauteur == ReglagesSampler::premiereNote + 1, "pas à pas : tranche 2 sur le premier pas");

            bouton (Bouton::device);
            expect (pilote.getPage() == Pilote::Page::generateur, "découpé : le générateur rythmique"_fr);

            // Un son glissé sur la fenêtre va au sampler de la piste choisie
            pilote.sonsDeposes ({ "One-shots/Laser.wav" });
            expectEquals (moteur.getMorceau().pistes[2].sampler.son, juce::String ("One-shots/Laser.wav"));

            // Retour au synthé
            bouton (Bouton::session);
            bouton (Bouton::browse);
            tourner (0, 400);    // jusqu'à « Machine »
            tourner (1, -16);
            tourner (1, 8);
            expect (moteur.getMorceau().pistes[2].estUnSynthe(), "Machine : Synthé ; annonce : "_fr + pilote.getAnnonce().titre + " " + pilote.getAnnonce().valeur
                    + ", son " + nomDuSon (moteur.getMorceau().pistes[2].son));
            expectEquals (moteur.getMorceau().pistes[2].nom, nomDuSon (moteur.getMorceau().pistes[2].son));
        }
    }
};


//==============================================================================
/** Hors série : un petit morceau fait seulement de samplers (et d'une grosse caisse), rendu dans Musique, groovebox,
    pour l'écouter. Il lit les sons de départ du vrai dossier Sons, sans rien y écrire. */
class EssaisEcouteSampler  : public juce::UnitTest
{
public:
    EssaisEcouteSampler()  : juce::UnitTest ("Écoute du sampler"_fr, "groovebox-long") {}

    void runTest() override
    {
        auto& engine = *te::Engine::getEngines()[0];
        BanqueDeSons banque;    // le vrai dossier Sons

        beginTest ("16 mesures de sampler : boucle étirée, tranches, corde et drone en notes, voix"_fr);

        auto m = morceauAvec (120.0, Mesure::standard (4, 4));
        m.nom = "Écoute du sampler"_fr;
        m.clic.actif = false;
        m.assurerScenes (1);
        m.delay.division = 2;
        m.delay.retour = 0.55f;

        auto sampler = [&] (int p, const juce::String& nom, const juce::String& son, ModeSampler mode, float volumeDb)
        {
            auto& piste = m.pistes[(size_t) p];
            piste.son = SonProvisoire::sampler;
            piste.nom = nom;
            piste.sampler.son = son;
            piste.sampler.mode = mode;
            piste.volumeDb = volumeDb;
            return &piste;
        };

        auto clip = [&] (int p, std::vector<Note> notes) { m.scenes[0].clips[(size_t) p] = Clip { m.pistes[(size_t) p].nom, 16, std::move (notes) }; };
        constexpr double mesure = 4.0;

        // Grosse caisse sur les temps, mesures 9 à 16
        m.pistes[0].kit = kitsDeDepart::kit ("Hard techno");
        m.pistes[0].volumeDb = 0.0f;
        {
            std::vector<Note> n;
            for (int t = 8 * 4; t < 16 * 4; ++t)
                n.push_back ({ (double) t, 0.25, Kit::premiereNote, 110 });
            clip (0, n);
        }

        // Breakbeat à 100, étiré à 120 : mesures 5 à 16 ; à l'envers sur les deux dernières
        sampler (1, "Boucle", "Boucles/Breakbeat 100.wav", ModeSampler::boucle, 3.0f);
        {
            std::vector<Note> n;
            for (int b = 4; b < 16; b += 2)
                n.push_back ({ b * mesure, 2 * mesure, 60, 100 });
            clip (1, n);
        }

        // Jungle découpée : les tranches dans un autre ordre, mesures 9 à 16
        sampler (2, "Tranches", "Boucles/Jungle 170.wav", ModeSampler::decoupe, 1.0f);
        {
            static constexpr int motif[16] = { 0, -1, 4, -1, 2, 2, -1, 6, 8, -1, 12, -1, 4, 13, 14, 15 };
            std::vector<Note> n;
            for (int b = 8; b < 16; ++b)
                for (int k = 0; k < 16; ++k)
                    if (motif[k] >= 0 && ! (b % 4 == 3 && k >= 12))
                        n.push_back ({ b * mesure + k * 0.25, 0.25, ReglagesSampler::premiereNote + motif[k], 100 });
            clip (2, n);
        }

        // Corde pincée en do : arpège de do mineur en croches
        sampler (3, "Corde", "Textures/Corde en do.wav", ModeSampler::notes, -1.0f)->sampler.relache = 0.35f;
        m.pistes[3].envoiReverbDb = -12.0f;
        {
            static constexpr int accords[4][4] = { { 60, 63, 67, 70 }, { 60, 63, 67, 72 }, { 56, 60, 63, 68 }, { 58, 62, 65, 70 } };
            std::vector<Note> n;
            for (int b = 0; b < 16; ++b)
                for (int k = 0; k < 8; ++k)
                {
                    const auto& a = accords[b < 8 ? (b / 4) % 2 : (b < 12 ? 2 : 3)];
                    n.push_back ({ b * mesure + k * 0.5, 0.45, a[(k * 3) % 4] + (k == 7 ? 12 : 0), 90 });
                }
            clip (3, n);
        }

        // Drone en do : notes tenues, attaque lente
        {
            auto* d = sampler (4, "Drone", "Textures/Drone en do.wav", ModeSampler::notes, -6.0f);
            d->sampler.attaque = 0.55f;
            d->sampler.relache = 0.6f;
            d->sampler.filtre = -0.25f;
            clip (4, { { 0.0, 8 * mesure, 48, 100 }, { 8 * mesure, 4 * mesure, 44, 100 }, { 12 * mesure, 4 * mesure, 46, 100 } });
        }

        // Voix : le décompte avant la boucle, « Allez » avant les tranches
        sampler (5, "Décompte"_fr, "Voix/Un deux trois quatre.aiff", ModeSampler::unCoup, 4.0f);
        clip (5, { { 3 * mesure, 1.0, 60, 110 } });
        sampler (6, "Allez", "Voix/Allez.aiff", ModeSampler::unCoup, 4.0f);
        m.pistes[6].envoiDelayDb = -10.0f;
        clip (6, { { 7 * mesure + 3.0, 1.0, 60, 110 }, { 11 * mesure + 3.0, 1.0, 60, 110 } });

        // Sirène dub dans le delay
        sampler (7, "Sirène"_fr, "One-shots/Sirène dub.wav"_fr, ModeSampler::unCoup, -6.0f);
        m.pistes[7].envoiDelayDb = -4.0f;
        clip (7, { { 12 * mesure, 1.0, 60, 100 }, { 14 * mesure + 2.0, 1.0, 65, 100 } });

        if (const auto seule = juce::SystemStats::getEnvironmentVariable ("PISTE_SEULE", {}); seule.isNotEmpty())
            for (int p = 0; p < nombreDePistes; ++p)
                m.pistes[(size_t) p].mute = p != seule.getIntValue();

        CarteSimulee carte (engine, 2);
        Moteur moteur (engine, m, &banque);
        moteur.attendreLesBoucles();
        moteur.lancerScene (0);
        juce::AudioBuffer<float> tout (2, 0);

        for (int k = 0; k < 4 * 35; ++k)
        {
            auto morceau = EssaisEnchainements::jouerEtAvancer (moteur, carte, uneSeconde / 4);
            const int avant = tout.getNumSamples();
            tout.setSize (2, avant + morceau.getNumSamples(), true, true);

            for (int c = 0; c < 2; ++c)
                tout.copyFrom (c, avant, morceau, c, 0, morceau.getNumSamples());
        }

        logMessage ("  crête "_fr + juce::String (juce::Decibels::gainToDecibels (tout.getMagnitude (0, tout.getNumSamples())), 1) + " dB");
        expect (tout.getMagnitude (0, tout.getNumSamples()) < 0.95f, "sous la protection de sortie");
        static const char* parties[] = { "Mesures 1-4 : corde et drone", "Mesures 5-8 : + boucle étirée", "Mesures 9-12 : + tranches, grosse caisse", "Mesures 13-16 : + sirène" };

        for (int i = 0; i < 4; ++i)
        {
            const int debut = (int) (i * 8.0 * frequence) + 2400, fin = (int) ((i + 1) * 8.0 * frequence);
            const float niveau = niveauEfficace (tout, 0, debut, fin);
            logMessage ("  " + juce::String::fromUTF8 (parties[i]) + " : " + juce::String (juce::Decibels::gainToDecibels (niveau), 1) + " dB efficaces");
            expect (niveau > 0.01f, juce::String::fromUTF8 (parties[i]) + " sonne");
        }

        const auto fichier = juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("groovebox").getChildFile ("Sampler (écoute).wav"_fr);
        fichier.deleteFile();
        juce::WavAudioFormat wav;

        if (auto ecrivain = std::unique_ptr<juce::AudioFormatWriter> (wav.createWriterFor (new juce::FileOutputStream (fichier), frequence, 2, 24, {}, 0)))
            ecrivain->writeFromAudioSampleBuffer (tout, 0, tout.getNumSamples());

        logMessage ("  fichier : " + fichier.getFullPathName());
    }
};

static EssaisModele essaisModele;
static EssaisSorties essaisSorties;
static EssaisHorloge essaisHorloge;
static EssaisPush essaisPush;
static EssaisSession essaisSession;
static EssaisBatterie essaisBatterie;
static EssaisGenerateurs essaisGenerateurs;
static EssaisGammes essaisGammes;
static EssaisSurge essaisSurge;
static EssaisMelodies essaisMelodies;
static EssaisEffets essaisEffets;
static EssaisEnchainements essaisEnchainements;
static EssaisPrises essaisPrises;
static EssaisRetouche essaisRetouche;
static EssaisRejeu essaisRejeu;
static EssaisRejeuLong essaisRejeuLong;
static EssaisDemo2 essaisDemo2;
static EssaisSampler essaisSampler;
static EssaisEcouteSampler essaisEcouteSampler;

} // namespace groovebox

int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI initialisationJuce;
    groovebox::surge::autoriser (false);    // seuls les essais du synthé chargent Surge XT : les autres vont bien plus vite

    auto dossierEssai = juce::File::createTempFile ("groovebox-essais");
    dossierEssai.createDirectory();

    int echecs = 0;

    {
        tracktion::Engine engine { std::make_unique<groovebox::Reglages> (dossierEssai),
                                   std::make_unique<tracktion::UIBehaviour>(),
                                   std::make_unique<groovebox::ComportementEssai>() };

        juce::UnitTestRunner essais;
        essais.setAssertOnFailure (false);
        // Un nom de série en argument (par exemple « Enchaînements ») : seulement celle-là
        if (const auto seule = juce::String::fromUTF8 (argc > 1 ? argv[1] : "").trim(); seule.isNotEmpty())
        {
            for (auto* e : juce::UnitTest::getAllTests())
                if (e->getName() == seule)
                    essais.runTests ({ e });
        }
        else
        {
            essais.runTestsInCategory ("groovebox");
        }

        for (int i = 0; i < essais.getNumResults(); ++i)
            if (auto resultat = essais.getResult (i))
                echecs += resultat->failures;
    }

    dossierEssai.deleteRecursively();
    std::cout << (echecs == 0 ? "\nTous les essais passent.\n" : "\nEssais en échec : " + std::to_string (echecs) + "\n");
    return echecs == 0 ? 0 : 1;
}
