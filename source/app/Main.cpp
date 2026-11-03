#include "FenetrePrincipale.h"
#include "moteur/Reglages.h"

namespace groovebox
{

/** Comportement du moteur : on ouvre la carte son nous-mêmes, sorties seulement à l'étape 1. */
struct ComportementMoteur  : public te::EngineBehaviour
{
    bool autoInitialiseDeviceManager() override     { return false; }
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
        // Le morceau en cours est rechargé tel qu'on l'a laissé, et sauvegardé au fil des changements
        moteur = std::make_unique<Moteur> (*engine, Morceau::charger (fichierDuMorceau()).value_or (Morceau {}));
        moteur->surChangementDeMorceau = [this] { sauvegardeEnAttente = true; };
        startTimer (2000);

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
            fenetre = std::make_unique<Fenetre> (*engine, *moteur);
    }

    void shutdown() override
    {
        stopTimer();
        fenetre.reset();
        diagnostic.reset();

        if (moteur != nullptr)
            sauvegarder();

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

    static juce::File fichierDuMorceau()
    {
        return Reglages::dossierParDefaut().getChildFile ("morceau-en-cours.json");
    }

    void sauvegarder()
    {
        sauvegardeEnAttente = false;

        if (! moteur->getMorceau().enregistrer (fichierDuMorceau()))
            juce::Logger::writeToLog ("Sauvegarde du morceau impossible : " + fichierDuMorceau().getFullPathName());
    }

    void timerCallback() override
    {
        if (sauvegardeEnAttente)
            sauvegarder();
    }

    struct Fenetre  : public juce::DocumentWindow
    {
        Fenetre (te::Engine& e, Moteur& m)
            : juce::DocumentWindow ("groovebox", juce::Colour (0xff15171c), juce::DocumentWindow::allButtons)
        {
            setUsingNativeTitleBar (true);
            setContentOwned (new FenetrePrincipale (e, m), true);
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
        Diagnostic (te::Engine& e, Moteur& m, int secondes)  : engine (e), moteur (m), restant (secondes)
        {
            for (auto s : { Sortie::principale, Sortie::casque })
            {
                moteur.reglerNiveauBip (s, 0.0f);
                moteur.activerBip (s, true);
            }

            // Lecture avec le clic, muet
            volumeDeDepart = moteur.getMorceau().clic.volume;
            moteur.modifier ([] (Morceau& m) { m.clic.volume = 0.0f; });
            moteur.lancer();

            if (auto carte = engine.getDeviceManager().deviceManager.getCurrentAudioDevice())
                coupuresAuDepart = carte->getXRunCount();

            startTimer (1000);
        }

        void timerCallback() override
        {
            chargeMax = juce::jmax (chargeMax, engine.getDeviceManager().getCpuUsage());

            if (--restant > 0)
                return;

            stopTimer();
            juce::String bilan ("Diagnostic : ");

            if (auto carte = engine.getDeviceManager().deviceManager.getCurrentAudioDevice())
                bilan << carte->getName() << ", " << carte->getCurrentSampleRate() << " Hz, tampon "
                      << carte->getCurrentBufferSizeSamples() << ", coupures " << (carte->getXRunCount() - coupuresAuDepart)
                      << ", charge max " << juce::roundToInt (chargeMax * 100.0f) << " %, lecture "
                      << (moteur.enLecture() ? "en cours" : "arrêtée"_fr) << ", mesure " << moteur.getPosition().mesure;
            else
                bilan << "aucune carte son";

            juce::Logger::writeToLog (bilan);
            std::cout << bilan << std::endl;
            moteur.arreter();
            moteur.modifier ([v = volumeDeDepart] (Morceau& m) { m.clic.volume = v; });
            juce::JUCEApplication::getInstance()->systemRequestedQuit();
        }

        te::Engine& engine;
        Moteur& moteur;
        int restant, coupuresAuDepart = 0;
        float chargeMax = 0.0f, volumeDeDepart = 0.8f;
    };

    void lancerDiagnostic (int secondes)
    {
        diagnostic = std::make_unique<Diagnostic> (*engine, *moteur, juce::jmax (1, secondes));
    }

    std::unique_ptr<juce::FileLogger> journal;
    std::unique_ptr<te::Engine> engine;
    std::unique_ptr<Moteur> moteur;
    std::unique_ptr<Fenetre> fenetre;
    std::unique_ptr<Diagnostic> diagnostic;
};

} // namespace groovebox

START_JUCE_APPLICATION (groovebox::Application)
