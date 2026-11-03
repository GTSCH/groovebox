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
        CarteSimulee (te::Engine& e, int sorties)
            : engine (e), nombreDeSorties (sorties)
        {
            te::HostedAudioDeviceInterface::Parameters p;
            p.sampleRate = frequence;
            p.blockSize = 128;
            p.inputChannels = 0;
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

        juce::AudioBuffer<float> jouer (int n)
        {
            juce::AudioBuffer<float> sortie (nombreDeSorties, n);
            sortie.clear();
            juce::MidiBuffer midi;

            for (int debut = 0; debut < n; debut += 128)
            {
                const int k = std::min (128, n - debut);
                juce::AudioBuffer<float> bloc (sortie.getArrayOfWritePointers(), nombreDeSorties, debut, k);
                io.processBlock (bloc, midi);
            }

            return sortie;
        }

    private:
        te::Engine& engine;
        int nombreDeSorties;
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
            expectEquals ((int) apres.tousLesMessages().size(), 64 + 128);
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

            // Piste 2 (basse) : pas de mode Note avant l'étape 6
            surface.recevoir ({ T::bouton, premierBoutonDuBas + 1, 1, true });
            surface.recevoir ({ T::bouton, premierBoutonDuBas + 1, 0, false });
            bouton (Bouton::note);
            expect (pilote.getMode() == Pilote::Mode::session, "pas de mode Note sur la basse pour l'instant"_fr);

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

        beginTest ("La voix B de chaque piste a son instrument et reste muette");
        {
            CarteSimulee carte (engine, 4);
            Moteur moteur (engine, morceauDeDemonstration(), &banqueEssai());
            moteur.lancerScene (2);

            for (int i = 0; i < 30; ++i) { carte.jouer (uneSeconde / 10); moteur.avancer(); }

            for (int p = 0; p < nombreDePistes; ++p)
            {
                auto b = moteur.voix (p, 1);
                expect (b != nullptr && b->pluginList.getPluginsOfType<InstrumentProvisoire>().size() == 1, "instrument de la voix B"_fr);

                for (auto c : b->getClipSlotList().getClipSlots())
                    expect (c->getClip() == nullptr, "aucun clip sur la voix B"_fr);
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
            c.regenerer (m.mesure);
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

static EssaisModele essaisModele;
static EssaisSorties essaisSorties;
static EssaisHorloge essaisHorloge;
static EssaisPush essaisPush;
static EssaisSession essaisSession;
static EssaisBatterie essaisBatterie;
static EssaisGenerateurs essaisGenerateurs;

} // namespace groovebox

int main()
{
    juce::ScopedJuceInitialiser_GUI initialisationJuce;

    auto dossierEssai = juce::File::createTempFile ("groovebox-essais");
    dossierEssai.createDirectory();

    int echecs = 0;

    {
        tracktion::Engine engine { std::make_unique<groovebox::Reglages> (dossierEssai),
                                   std::make_unique<tracktion::UIBehaviour>(),
                                   std::make_unique<groovebox::ComportementEssai>() };

        juce::UnitTestRunner essais;
        essais.setAssertOnFailure (false);
        essais.runTestsInCategory ("groovebox");

        for (int i = 0; i < essais.getNumResults(); ++i)
            if (auto resultat = essais.getResult (i))
                echecs += resultat->failures;
    }

    dossierEssai.deleteRecursively();
    std::cout << (echecs == 0 ? "\nTous les essais passent.\n" : "\nEssais en échec : " + std::to_string (echecs) + "\n");
    return echecs == 0 ? 0 : 1;
}
