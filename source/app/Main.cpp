#include "FenetrePrincipale.h"
#include "Simulateur.h"
#include "push/EcranPush.h"
#include "push/PushMidi.h"
#include "push/Pilote.h"
#include "moteur/Reglages.h"
#include "modele/Bibliotheque.h"
#include "modele/Demo.h"
#include "moteur/Surge.h"
#include "moteur/Exports.h"

namespace groovebox
{

/** Comportement du moteur : on ouvre la carte son nous-mêmes.
    Calcul du son sur le seul fil audio : avec quelques % de charge, répartir le travail sur les 8 cœurs de la puce M3
    (dont 4 lents) faisait attendre le fil audio et provoquait des coupures. À revoir quand Surge XT arrivera (étape 6). */
struct ComportementMoteur  : public te::EngineBehaviour
{
    bool autoInitialiseDeviceManager() override     { return false; }
    int getNumberOfCPUsToUseForAudio() override     { return 1; }
};

class Application  : public juce::JUCEApplication,
                     private juce::Timer
{
public:
    const juce::String getApplicationName() override       { return "groovebox"; }
    const juce::String getApplicationVersion() override    { return ProjectInfo::versionString; }
    // Un export tourne dans un second groovebox, sans fenêtre ni carte son
    bool moreThanOneInstanceAllowed() override             { return getCommandLineParameters().contains ("--exporter"); }

    void initialise (const juce::String& ligneDeCommande) override
    {
        if (const auto args = juce::StringArray::fromTokens (ligneDeCommande, true); args.contains ("--exporter"))
            return lancerLExport (juce::File (args[args.indexOf ("--exporter") + 1].unquoted()));

        journal.reset (juce::FileLogger::createDefaultAppLogger ("groovebox", "journal.txt",
                                                                 "groovebox " + getApplicationVersion() + " démarré"_fr));
        juce::Logger::setCurrentLogger (journal.get());

        engine = std::make_unique<te::Engine> (std::make_unique<Reglages>(),
                                               std::make_unique<te::UIBehaviour>(),
                                               std::make_unique<ComportementMoteur>());
        engine->getDeviceManager().initialise (0, 4);
        appliquerReglagesDeDepart (*engine);
        ouvrirLesEntreesDeLaCarte (*engine);
        // Le dernier morceau ouvert revient tel qu'on l'a laissé ; au tout premier lancement, la démo
        bibliotheque = std::make_unique<Bibliotheque>();
        plantageDetecte = bibliotheque->marquerOuverture();
        moteur = std::make_unique<Moteur> (*engine, morceauDeDepart());
        dossierCourant = moteur->getMorceau().nom;
        moteur->surChangementDeMorceau = [this] { sauvegardeEnAttente = true; suivreLeNom(); };
        moteur->dossierDesPrises = [this] { return bibliotheque->dossierDe (dossierCourant).getChildFile ("prises"); };
        startTimer (2000);

        if (plantageDetecte)
            juce::Logger::writeToLog ("La séance précédente ne s'est pas fermée normalement"_fr);

        if (auto carte = engine->getDeviceManager().deviceManager.getCurrentAudioDevice())
            juce::Logger::writeToLog ("Carte son : " + carte->getName() + ", "
                                      + juce::String (carte->getCurrentSampleRate()) + " Hz, tampon "
                                      + juce::String (carte->getCurrentBufferSizeSamples()));
        else
            juce::Logger::writeToLog ("Aucune carte son ouverte");

        const auto arguments = juce::StringArray::fromTokens (ligneDeCommande, true);

        if (const auto i = arguments.indexOf ("--options-surge"); i >= 0)
        {
            // Pour chaque paramètre demandé (séparés par des virgules) : les textes possibles et la valeur qui y mène
            if (auto plugin = surge::creer (moteur->getEdit()))
                if (auto externe = dynamic_cast<te::ExternalPlugin*> (plugin.get()))
                    for (auto* p : externe->getAudioPluginInstance()->getParameters())
                        for (auto& voulu : juce::StringArray::fromTokens (ligneDeCommande.fromFirstOccurrenceOf ("--options-surge", false, false).trim().unquoted(), ",", ""))
                            if (p->getName (100).endsWith (voulu.trim().unquoted().trim()))
                            {
                                juce::String precedent;

                                for (int k = 0; k <= 400; ++k)
                                    if (auto t = p->getText ((float) k / 400.0f, 100); t != precedent)
                                    {
                                        std::cout << voulu.trim() << "\t" << juce::String ((float) k / 400.0f, 4) << "\t" << t << std::endl;
                                        precedent = t;
                                    }
                            }

            juce::JUCEApplication::getInstance()->systemRequestedQuit();
            return;
        }

        if (arguments.contains ("--parametres-surge"))
        {
            // Liste les paramètres de Surge XT (nom, valeur, texte affiché) pour concevoir les sons de départ
            if (auto plugin = surge::creer (moteur->getEdit()))
                for (auto& [nom, p] : surge::parametres (*plugin))
                    std::cout << nom << "\t" << p->getCurrentValue() << "\t" << p->getCurrentValueAsString() << std::endl;
            else
                std::cout << "Surge XT introuvable" << std::endl;

            juce::JUCEApplication::getInstance()->systemRequestedQuit();
            return;
        }

        if (const auto i = arguments.indexOf ("--diagnostic"); i >= 0)
            lancerDiagnostic (arguments[i + 1].getIntValue(), arguments.contains ("--demo"), arguments.contains ("--prise"), arguments.contains ("--charge"));
        else
            demarrerLeJeu();
    }

    void shutdown() override
    {
        stopTimer();
        fenetre.reset();
        simulateur.reset();
        pilote.reset();
        pushMidi.reset();
        ecranPush.reset();
        surface.reset();
        diagnostic.reset();

        if (dossierDePriseDeDiagnostic != juce::File())
        {
            if (moteur != nullptr)
                moteur->arreterPrise();

            dossierDePriseDeDiagnostic.deleteRecursively();
        }

        if (moteur != nullptr)
            sauvegarder();

        if (bibliotheque != nullptr)
            bibliotheque->marquerFermeture();

        if (engine != nullptr && ! exportEnCours)
        {
            engine->getDeviceManager().saveSettings();
            engine->getPropertyStorage().getPropertiesFile().saveIfNeeded();
        }

        moteur.reset();
        engine.reset();
        juce::Logger::writeToLog ("groovebox fermé"_fr);
        juce::Logger::setCurrentLogger (nullptr);
        journal.reset();
    }

    void systemRequestedQuit() override                    { quit(); }

private:
    bool sauvegardeEnAttente = false;
    bool chargementEnCours = false;

    void demarrerLeJeu()
    {
        // Le Push et son simulateur partagent la même surface : ce qu'on fait sur l'un s'allume sur l'autre
        surface = std::make_unique<Surface>();
        ecranPush = std::make_unique<EcranPush>();
        surface->ajouterClient (ecranPush.get());
        pushMidi = std::make_unique<PushMidi> (*surface);
        surface->ajouterClient (pushMidi.get());
        pilote = std::make_unique<Pilote> (*moteur, *surface);

        ActionsMorceaux actions;
        actions.noms = [this] { return bibliotheque->noms(); };
        actions.ouvrir = [this] (const juce::String& nom) { ouvrirMorceau (nom); };
        actions.nouveau = [this] { nouveauMorceau(); };
        actions.demo = [this] { ouvrirLaDemo(); };
        actions.demo2 = [this] { ouvrirLaDemo2(); };
        actions.ouvrirSimulateur = [this] { ouvrirLeSimulateur(); };
        actions.deposerDesSons = [this] (const juce::StringArray& fichiers) { pilote->sonsDeposes (moteur->getBanque().importer (fichiers)); };

        fenetre = std::make_unique<Fenetre> (*engine, *moteur, std::move (actions), [this] { return etatDuPush(); });

        if (plantageDetecte)
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, "groovebox a été rouvert"_fr,
                "groovebox s'était fermé brusquement. Le morceau « "_fr + moteur->getMorceau().nom
                + " » est revenu tel qu'il était à la dernière sauvegarde, il y a au plus quelques secondes.\n\n"_fr
                + "Des copies plus anciennes sont dans Musique, groovebox, Morceaux, puis le dossier du morceau, « sauvegardes »."_fr);
    }

    //==============================================================================
    Morceau morceauDeDepart()
    {
        if (auto m = bibliotheque->ouvrir (bibliotheque->dernierOuvert()))
            return *m;

        return demo();
    }

    Morceau demo()
    {
        if (auto m = bibliotheque->ouvrir ("Démo"_fr))
            return *m;

        auto m = morceauDeDemonstration();
        bibliotheque->enregistrer (m);
        return m;
    }

    void charger (const Morceau& m)
    {
        moteur->deroulePourPlay.reset();    // un autre morceau : Play redevient la lecture habituelle
        sauvegarder();

        // Le moteur annonce le changement de morceau : ce n'est pas un changement de nom, le dossier d'avant ne bouge pas
        chargementEnCours = true;
        moteur->chargerMorceau (m);
        chargementEnCours = false;
        dossierCourant = moteur->getMorceau().nom;
        bibliotheque->retenirDernier (dossierCourant);
        sauvegarder();
    }

    void ouvrirMorceau (const juce::String& nom)
    {
        if (auto m = bibliotheque->ouvrir (nom))
            charger (*m);
    }

    void nouveauMorceau()
    {
        Morceau m;
        m.nom = bibliotheque->nomLibre ("Morceau");
        m.assurerScenes (8);
        charger (m);
    }

    void ouvrirLaDemo()
    {
        charger (demo());
    }

    /** Démo 2 : le morceau en 6/8, et son déroulé d'un bout à l'autre, que Play rejoue tout de suite. */
    void ouvrirLaDemo2()
    {
        const auto nom = "Démo 2"_fr;
        auto m = bibliotheque->ouvrir (nom);

        if (! m)
        {
            m = morceauDeDemonstration2();
            bibliotheque->enregistrer (*m);
        }

        charger (*m);

        const auto deroule = derouleDeLaDemo2 (morceauDeDemonstration2());
        const auto fichier = bibliotheque->dossierDe (nom).getChildFile ("deroules").getChildFile (juce::File::createLegalFileName (deroule.nom) + ".json");

        if (! fichier.existsAsFile())
            deroule.enregistrer (fichier);

        if (auto d = Deroule::charger (fichier))
            moteur->deroulePourPlay = *d;
    }

    /** Le dossier suit le nom du morceau, même quand on annule un changement de nom. */
    void suivreLeNom()
    {
        const auto& nom = moteur->getMorceau().nom;

        // Pendant un chargement ou le diagnostic (qui peut charger la démo), les morceaux enregistrés ne bougent pas
        if (nom == dossierCourant || diagnosticEnCours || chargementEnCours)
            return;

        if (bibliotheque->renommer (dossierCourant, nom))
        {
            dossierCourant = nom;
            bibliotheque->retenirDernier (nom);
        }
        else
        {
            juce::Logger::writeToLog ("Renommage impossible : "_fr + dossierCourant + " vers " + nom);
        }
    }

    void ouvrirLeSimulateur()
    {
        if (simulateur == nullptr)
            simulateur = std::make_unique<FenetreSimulateur> (*surface);

        simulateur->setVisible (true);
        simulateur->toFront (true);
    }

    juce::String etatDuPush() const
    {
        if (pushMidi == nullptr)
            return {};

        if (pushMidi->estConnecte() && ecranPush->estConnecte())
            return "Push branché"_fr;

        if (ecranPush->estPrisAilleurs())
            return "Push pris par Ableton Live : ferme Live"_fr;

        if (pushMidi->estConnecte())
            return "Push branché, écran en attente"_fr;

        return "Push non branché"_fr;
    }

    void sauvegarder()
    {
        // Pendant un rejeu, le morceau change sans cesse puis revient : on attend la fin pour enregistrer
        if (moteur->rejeuEnCours())
            return;

        sauvegardeEnAttente = false;

        if (diagnosticEnCours)
            return;

        // Écrire un petit fichier JSON prend moins d'une milliseconde, sur le fil principal : le son n'est jamais touché
        if (! bibliotheque->enregistrer (moteur->getMorceau()))
            juce::Logger::writeToLog ("Sauvegarde du morceau impossible : "_fr + moteur->getMorceau().nom);
        else
            bibliotheque->retenirDernier (moteur->getMorceau().nom);
    }

    void timerCallback() override
    {
        if (sauvegardeEnAttente)
            sauvegarder();
    }

    struct Fenetre  : public juce::DocumentWindow
    {
        Fenetre (te::Engine& e, Moteur& m, ActionsMorceaux actions, std::function<juce::String()> etatDuPush)
            : juce::DocumentWindow ("groovebox", juce::Colour (0xff15171c), juce::DocumentWindow::allButtons)
        {
            setUsingNativeTitleBar (true);
            setContentOwned (new FenetrePrincipale (e, m, std::move (actions), std::move (etatDuPush)), true);
            setResizable (true, false);
            centreWithSize (getWidth(), getHeight());
            setVisible (true);
            getContentComponent()->grabKeyboardFocus();
        }

        void closeButtonPressed() override                 { juce::JUCEApplication::getInstance()->systemRequestedQuit(); }
    };

    /** Essai sans fenêtre : bips actifs mais muets pendant N secondes, puis bilan des coupures dans le journal. */
    struct Diagnostic  : private juce::Timer
    {
        Diagnostic (te::Engine& e, Moteur& m, int s)  : engine (e), moteur (m), restant (s), secondes (s)
        {
            for (auto s : { Sortie::principale, Sortie::casque })
            {
                moteur.reglerNiveauBip (s, 0.0f);
                moteur.activerBip (s, true);
            }

            // Lecture de la scène la plus remplie, avec le clic, presque muette (-60 dB) mais calculée en entier
            morceauDeDepart = moteur.getMorceau();
            moteur.modifier ([] (Morceau& m)
            {
                m.clic.volume = 0.0f;

                for (auto& p : m.pistes)
                {
                    p.volumeDb = -60.0f;
                    p.mute = p.solo = false;
                }
            });

            int nombreMax = -1, secondMax = -1;

            for (int s = 0; s < (int) moteur.getMorceau().scenes.size(); ++s)
            {
                int n = 0;

                for (auto& c : moteur.getMorceau().scenes[(size_t) s].clips)
                    n += c.has_value() ? 1 : 0;

                if (n > nombreMax)
                {
                    secondMax = nombreMax;
                    secondeScene = plusRemplie;
                    nombreMax = n;
                    plusRemplie = s;
                }
                else if (n > secondMax)
                {
                    secondMax = n;
                    secondeScene = s;
                }
            }

            moteur.lancerScene (plusRemplie);
            clipsLances = nombreMax;

            if (auto carte = engine.getDeviceManager().deviceManager.getCurrentAudioDevice())
                coupuresAuDepart = dernieresCoupures = carte->getXRunCount();

            startTimer (1000);
        }

        void timerCallback() override
        {
            const auto charge = engine.getDeviceManager().getCpuUsage();
            chargeMax = juce::jmax (chargeMax, charge);
            chargeMaxDeLaMinute = juce::jmax (chargeMaxDeLaMinute, charge);
            sommeDeLaMinute += charge;

            // Une ligne par minute : on voit si le Mac ralentit en chauffant
            if (++secondesDeLaMinute == 60)
            {
                juce::String ligne ("Charge, minute ");
                ligne << (secondes - restant + 1) / 60 << " : moyenne " << juce::roundToInt (sommeDeLaMinute / 60.0f * 100.0f)
                      << " %, max " << juce::roundToInt (chargeMaxDeLaMinute * 100.0f) << " %, coupures "
                      << (dernieresCoupures - coupuresAuDepart);
                juce::Logger::writeToLog (ligne);
                std::cout << ligne << std::endl;
                secondesDeLaMinute = 0;
                sommeDeLaMinute = chargeMaxDeLaMinute = 0.0f;
            }

            if (auto carte = engine.getDeviceManager().deviceManager.getCurrentAudioDevice())
                if (carte->getXRunCount() != dernieresCoupures)
                {
                    dernieresCoupures = carte->getXRunCount();
                    juce::Logger::writeToLog ("Diagnostic : coupure à la seconde "_fr + juce::String (secondes - restant + 1));
                }

            // Toutes les 4 secondes, un fondu d'une mesure avec montée vers l'autre scène, batterie comprise : les deux voix calculent ensemble
            if ((secondes - restant) % 4 == 3 && secondeScene != plusRemplie)
            {
                Enchainement e;
                e.type = TypeEnchainement::fondu;
                e.mesures = 1;
                e.batterieNette = false;
                e.montee = true;
                std::swap (plusRemplie, secondeScene);
                moteur.lancerScene (plusRemplie, e);
                ++enchainementsLances;
            }

            if (--restant > 0)
                return;

            stopTimer();
            juce::String bilan ("Diagnostic : ");

            if (auto carte = engine.getDeviceManager().deviceManager.getCurrentAudioDevice())
                bilan << carte->getName() << ", " << carte->getCurrentSampleRate() << " Hz, tampon "
                      << carte->getCurrentBufferSizeSamples() << ", coupures " << (carte->getXRunCount() - coupuresAuDepart)
                      << ", charge max " << juce::roundToInt (chargeMax * 100.0f) << " %, lecture "
                      << (moteur.enLecture() ? "en cours" : "arrêtée"_fr) << ", mesure " << moteur.getPosition().mesure << ", " << clipsLances << " clips, "
                      << enchainementsLances << " fondus";
            else
                bilan << "aucune carte son";

            juce::Logger::writeToLog (bilan);
            std::cout << bilan << std::endl;
            moteur.arreter();
            moteur.chargerMorceau (morceauDeDepart);
            juce::JUCEApplication::getInstance()->systemRequestedQuit();
        }

        te::Engine& engine;
        Moteur& moteur;
        int restant, secondes, coupuresAuDepart = 0, dernieresCoupures = 0;
        float chargeMax = 0.0f, chargeMaxDeLaMinute = 0.0f, sommeDeLaMinute = 0.0f;
        int secondesDeLaMinute = 0;
        int clipsLances = 0, plusRemplie = 0, secondeScene = 0, enchainementsLances = 0;
        Morceau morceauDeDepart;
    };

    /** --exporter <demande.json> : calcule les exports demandés et dit où il en est sur la sortie standard
        (« progression 0.42 texte », « fichier chemin », « remarque texte », « fin secondes-de-musique secondes-de-calcul »). */
    void lancerLExport (const juce::File& fichierDeDemande)
    {
        exportEnCours = true;
        engine = std::make_unique<te::Engine> (std::make_unique<Reglages>(),
                                               std::make_unique<te::UIBehaviour>(),
                                               std::make_unique<ComportementMoteur>());

        juce::MessageManager::callAsync ([this, fichierDeDemande]
        {
            const auto demande = exports::Demande::depuisJson (juce::JSON::parse (fichierDeDemande));
            const auto r = exports::exporter (*engine, demande, nullptr, [] (float x, const juce::String& texte)
            {
                std::cout << "progression " << x << " " << texte << std::endl;
            });

            for (auto& f : r.fichiers)
                std::cout << "fichier " << f << std::endl;

            for (auto& remarque : r.remarques)
                std::cout << "remarque " << remarque << std::endl;

            std::cout << "fin " << r.secondesDeMusique << " " << r.secondesDeCalcul << std::endl;
            quit();
        });
    }

    void lancerDiagnostic (int secondes, bool avecLaDemo, bool avecUnePrise, bool chargeLourde)
    {
        diagnosticEnCours = true;   // aucune sauvegarde pendant le diagnostic

        // Avec --demo : la démo, sans toucher aux morceaux enregistrés
        if (avecLaDemo)
            moteur->chargerMorceau (morceauDeDemonstration());

        // Avec --charge : la Démo 2 (8 pistes, Surge) dont la batterie devient une batterie guide acoustique
        if (chargeLourde)
            moteur->chargerMorceau (morceauDeCharge());
        diagnostic = std::make_unique<Diagnostic> (*engine, *moteur, juce::jmax (1, secondes));

        // Avec --prise : une prise pendant tout le diagnostic, dans un dossier temporaire effacé à la fermeture
        if (avecUnePrise)
        {
            dossierDePriseDeDiagnostic = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("groovebox-diagnostic-prise");
            dossierDePriseDeDiagnostic.deleteRecursively();
            moteur->dossierDesPrises = [d = dossierDePriseDeDiagnostic] { return d; };
            moteur->surPriseTerminee = [] (const juce::File& d)
            {
                juce::Logger::writeToLog ("Diagnostic : prise de "_fr + juce::String (d.getChildFile ("sortie.wav").getSize() / 1000000.0, 1) + " Mo écrite");
                std::cout << "Diagnostic : prise de " << juce::String (d.getChildFile ("sortie.wav").getSize() / 1000000.0, 1) << " Mo" << std::endl;
            };
            moteur->demarrerPrise();
        }
    }

    static Morceau morceauDeCharge()
    {
        auto m = morceauDeDemonstration2();
        auto& guide = m.pistes[0];
        guide.son = SonProvisoire::batterie;
        guide.guide = true;
        guide.kit = kitsAcoustiques::kit (kitsAcoustiques::kitDeLaGuide);

        for (auto& scene : m.scenes)
            if (auto& c = scene.clips[0])
            {
                c->longueurMesures = 4;
                c->generateur = Generateur::parDefaut (MoteurRythmique::batteur, m.mesure);
                c->generateur->roulement = 4;
                c->regenerer (m.mesure, m.gamme);
            }

        return m;
    }

    std::unique_ptr<juce::FileLogger> journal;
    std::unique_ptr<te::Engine> engine;
    std::unique_ptr<Moteur> moteur;
    std::unique_ptr<Bibliotheque> bibliotheque;
    juce::String dossierCourant;
    bool plantageDetecte = false, diagnosticEnCours = false, exportEnCours = false;
    juce::File dossierDePriseDeDiagnostic;
    std::unique_ptr<Surface> surface;
    std::unique_ptr<EcranPush> ecranPush;
    std::unique_ptr<PushMidi> pushMidi;
    std::unique_ptr<Pilote> pilote;
    std::unique_ptr<FenetreSimulateur> simulateur;
    std::unique_ptr<Fenetre> fenetre;
    std::unique_ptr<Diagnostic> diagnostic;
};

} // namespace groovebox

START_JUCE_APPLICATION (groovebox::Application)
