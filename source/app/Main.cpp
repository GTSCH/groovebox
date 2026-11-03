#include "FenetrePrincipale.h"
#include "Simulateur.h"
#include "push/EcranPush.h"
#include "push/PushMidi.h"
#include "push/Pilote.h"
#include "moteur/Reglages.h"
#include "modele/Bibliotheque.h"
#include "modele/Demo.h"

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
    bool moreThanOneInstanceAllowed() override             { return false; }

    void initialise (const juce::String& ligneDeCommande) override
    {
        journal.reset (juce::FileLogger::createDefaultAppLogger ("groovebox", "journal.txt",
                                                                 "groovebox " + getApplicationVersion() + " démarré"_fr));
        juce::Logger::setCurrentLogger (journal.get());

        engine = std::make_unique<te::Engine> (std::make_unique<Reglages>(),
                                               std::make_unique<te::UIBehaviour>(),
                                               std::make_unique<ComportementMoteur>());
        engine->getDeviceManager().initialise (0, 4);
        appliquerReglagesDeDepart (*engine);
        // Le dernier morceau ouvert revient tel qu'on l'a laissé ; au tout premier lancement, la démo
        bibliotheque = std::make_unique<Bibliotheque>();
        plantageDetecte = bibliotheque->marquerOuverture();
        moteur = std::make_unique<Moteur> (*engine, morceauDeDepart());
        dossierCourant = moteur->getMorceau().nom;
        moteur->surChangementDeMorceau = [this] { sauvegardeEnAttente = true; suivreLeNom(); };
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

        if (const auto i = arguments.indexOf ("--diagnostic"); i >= 0)
            lancerDiagnostic (arguments[i + 1].getIntValue());
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

        if (moteur != nullptr)
            sauvegarder();

        if (bibliotheque != nullptr)
            bibliotheque->marquerFermeture();

        if (engine != nullptr)
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
        actions.ouvrirSimulateur = [this] { ouvrirLeSimulateur(); };

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
        sauvegarder();
        moteur->chargerMorceau (m);
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

    /** Le dossier suit le nom du morceau, même quand on annule un changement de nom. */
    void suivreLeNom()
    {
        const auto& nom = moteur->getMorceau().nom;

        if (nom == dossierCourant)
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

            int plusRemplie = 0, nombreMax = -1;

            for (int s = 0; s < (int) moteur.getMorceau().scenes.size(); ++s)
            {
                int n = 0;

                for (auto& c : moteur.getMorceau().scenes[(size_t) s].clips)
                    n += c.has_value() ? 1 : 0;

                if (n > nombreMax)
                {
                    nombreMax = n;
                    plusRemplie = s;
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
            chargeMax = juce::jmax (chargeMax, engine.getDeviceManager().getCpuUsage());

            if (auto carte = engine.getDeviceManager().deviceManager.getCurrentAudioDevice())
                if (carte->getXRunCount() != dernieresCoupures)
                {
                    dernieresCoupures = carte->getXRunCount();
                    juce::Logger::writeToLog ("Diagnostic : coupure à la seconde "_fr + juce::String (secondes - restant + 1));
                }

            if (--restant > 0)
                return;

            stopTimer();
            juce::String bilan ("Diagnostic : ");

            if (auto carte = engine.getDeviceManager().deviceManager.getCurrentAudioDevice())
                bilan << carte->getName() << ", " << carte->getCurrentSampleRate() << " Hz, tampon "
                      << carte->getCurrentBufferSizeSamples() << ", coupures " << (carte->getXRunCount() - coupuresAuDepart)
                      << ", charge max " << juce::roundToInt (chargeMax * 100.0f) << " %, lecture "
                      << (moteur.enLecture() ? "en cours" : "arrêtée"_fr) << ", mesure " << moteur.getPosition().mesure << ", " << clipsLances << " clips";
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
        float chargeMax = 0.0f;
        int clipsLances = 0;
        Morceau morceauDeDepart;
    };

    void lancerDiagnostic (int secondes)
    {
        diagnosticEnCours = true;   // aucune sauvegarde pendant le diagnostic
        diagnostic = std::make_unique<Diagnostic> (*engine, *moteur, juce::jmax (1, secondes));
    }

    std::unique_ptr<juce::FileLogger> journal;
    std::unique_ptr<te::Engine> engine;
    std::unique_ptr<Moteur> moteur;
    std::unique_ptr<Bibliotheque> bibliotheque;
    juce::String dossierCourant;
    bool plantageDetecte = false, diagnosticEnCours = false;
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
