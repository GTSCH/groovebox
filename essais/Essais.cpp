// Essais automatiques de groovebox : ce qui se vérifie sans oreille.
// Une carte son simulée (4 sorties, ou 2) remplace la vraie : aucun son ne sort du Mac.

#include <JuceHeader.h>
#include "moteur/Moteur.h"
#include "moteur/Reglages.h"
#include "modele/TapTempo.h"
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
            Moteur moteur (engine);
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
            Moteur moteur (engine, morceau);
            expect (! moteur.getMorceau().clic.ecouteSurMac);

            moteur.activerBip (Sortie::casque, true);
            moteur.lancer();
            auto audio = carte.jouer (3 * uneSeconde);

            for (int canal : { 0, 1 })
                expectEquals (crete (audio, canal), 0.0f, "rien du casque sur la sortie " + juce::String (canal + 1));

            for (int canal : { 2, 3 })
                expect (crete (audio, canal) > 0.3f, "clic absent sur la sortie " + juce::String (canal + 1));
        }

        beginTest ("Sans sorties 3-4, le clic et le test casque restent muets");
        {
            CarteSimulee carte (engine, 2);
            Moteur moteur (engine, morceauAvec (120.0, Mesure::standard (4, 4)));
            expect (! moteur.sortieCasqueDisponible());

            moteur.activerBip (Sortie::casque, true);
            expect (! moteur.bipActif (Sortie::casque), "le bouton casque doit refuser de s'allumer");
            moteur.lancer();
            auto audio = carte.jouer (2 * uneSeconde);

            for (int canal : { 0, 1 })
                expectEquals (crete (audio, canal), 0.0f);
        }

        beginTest ("Écoute sur le Mac : le clic sur 1-2, sans doubler la musique"_fr);
        {
            CarteSimulee carte (engine, 2);
            Moteur moteur (engine, morceauAvec (120.0, Mesure::standard (4, 4)));
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
            moteur.lancer();
            laisserTravailler();
            auto clic = carte.jouer (2 * uneSeconde);
            expect (crete (clic, 0) > 0.3f, "le clic doit s'entendre sur le Mac");
        }

        beginTest ("Le son de test marche aussi à l'arrêt"_fr);
        {
            CarteSimulee carte (engine, 2);
            Moteur moteur (engine);
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
        auto lancerSurDeuxSorties = [] (Moteur& moteur)
        {
            moteur.modifier ([] (Morceau& m) { m.clic.ecouteSurMac = true; });
            laisserTravailler();
            moteur.lancer();
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
            Moteur moteur (engine, morceauAvec (tempo, mesure));
            lancerSurDeuxSorties (moteur);

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
            Moteur moteur (engine, morceauAvec (tempo, mesure));
            lancerSurDeuxSorties (moteur);

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
            Moteur moteur (engine, morceau);
            lancerSurDeuxSorties (moteur);
            DetecteurDeCoups detecteur;
            detecteur.analyser (carte.jouer (uneSeconde * 3), 0);
            expectEquals (suiteDesTypes (detecteur.coups, 4), juce::String ("PGPG"));

            moteur.arreter();
            moteur.modifier ([] (Morceau& m) { m.clic.subdivisions = true; });
            laisserTravailler();
            moteur.lancer();
            DetecteurDeCoups avecSubdivisions;
            avecSubdivisions.analyser (carte.jouer (uneSeconde * 2), 0);
            expectEquals (suiteDesTypes (avecSubdivisions.coups, 6), juce::String ("PssGss"));
        }

        beginTest ("Décompte de 2 mesures, puis mesure 1"_fr);
        {
            CarteSimulee carte (engine, 2);
            Moteur moteur (engine, morceauAvec (120.0, Mesure::standard (4, 4), 2));
            lancerSurDeuxSorties (moteur);

            DetecteurDeCoups detecteur;
            detecteur.analyser (carte.jouer (uneSeconde), 0);
            expectEquals (moteur.getPosition().mesure, -1, "première mesure de décompte"_fr);
            detecteur.analyser (carte.jouer (uneSeconde * 2), 0);
            expectEquals (moteur.getPosition().mesure, 0, "seconde mesure de décompte"_fr);
            detecteur.analyser (carte.jouer (uneSeconde * 2), 0);
            expectEquals (moteur.getPosition().mesure, 1, "après le décompte, mesure 1"_fr);
            expectEquals (moteur.getPosition().temps, 1);
            expectEquals (suiteDesTypes (detecteur.coups, 10), juce::String ("PaaaPaaaPa"));
        }

        beginTest ("Changer le tempo en pleine lecture ne fait pas sauter la mesure");
        {
            CarteSimulee carte (engine, 2);
            Moteur moteur (engine, morceauAvec (120.0, Mesure::standard (4, 4)));
            lancerSurDeuxSorties (moteur);

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
            Moteur moteur (engine, morceauAvec (120.0, Mesure::standard (4, 4)));
            lancerSurDeuxSorties (moteur);
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
            Moteur moteur (engine, morceauAvec (120.0, Mesure::standard (4, 4)));
            lancerSurDeuxSorties (moteur);
            carte.jouer (uneSeconde);
            moteur.modifier ([] (Morceau& m) { m.tempo = 150.0; });
            laisserTravailler();
            carte.jouer (uneSeconde);
            moteur.arreter();
            laisserTravailler();
            expect (! moteur.enLecture());
            expectEquals (moteur.getEdit().tempoSequence.getNumTempos(), 1, "un seul tempo après l'arrêt"_fr);

            moteur.lancer();
            carte.jouer (uneSeconde / 10);
            expectEquals (moteur.getPosition().mesure, 1);
            expectWithinAbsoluteError (moteur.getEdit().tempoSequence.getTempo (0)->getBpm(), 150.0, 0.001);
        }

        beginTest ("La mesure ne change pas pendant la lecture");
        {
            CarteSimulee carte (engine, 2);
            Moteur moteur (engine, morceauAvec (120.0, Mesure::standard (4, 4)));
            moteur.lancer();
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
            Moteur moteur (engine);
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
            Moteur moteur (engine, morceauAvec (120.0, Mesure::standard (4, 4)));
            Surface surface;
            Pilote pilote (moteur, surface);
            auto bouton = [&] (Bouton b) { surface.recevoir ({ T::bouton, (int) b, 1, true }); surface.recevoir ({ T::bouton, (int) b, 0, false }); };
            auto tourner = [&] (int encodeur, int pas) { surface.recevoir ({ T::encodeurTourne, encodeur, pas, false }); };

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

        beginTest ("Les pads s'allument quand on les appuie, plus fort si on frappe fort");
        {
            CarteSimulee carte (engine, 2);
            Moteur moteur (engine);
            Surface surface;
            Pilote pilote (moteur, surface);

            expectEquals (pilote.calculerEtat().pads[9], palette::indexDePiste (1, 0));
            surface.recevoir (Evenement::pad (1, 1, 30));
            expectEquals (pilote.calculerEtat().pads[9], palette::indexDePiste (1, 1));
            surface.recevoir (Evenement::pad (1, 1, 127));
            expectEquals (pilote.calculerEtat().pads[9], palette::indexDePiste (1, 3));
            surface.recevoir (Evenement::pad (1, 1, 0));
            expectEquals (pilote.calculerEtat().pads[9], palette::indexDePiste (1, 0));
        }

        beginTest ("Le Push et le simulateur reçoivent le même état");
        {
            CarteSimulee carte (engine, 2);
            Moteur moteur (engine);
            Surface surface;
            ClientEssai push, simulateur;
            surface.ajouterClient (&push);
            surface.ajouterClient (&simulateur);
            Pilote pilote (moteur, surface);

            surface.recevoir (Evenement::pad (0, 5, 100));
            expect (push.etat == simulateur.etat);
            expect (push.ecran == simulateur.ecran, "même image d'écran"_fr);
            expectEquals (push.etat.pads[5], palette::indexDePiste (5, 3));
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

static EssaisModele essaisModele;
static EssaisSorties essaisSorties;
static EssaisHorloge essaisHorloge;
static EssaisPush essaisPush;

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
