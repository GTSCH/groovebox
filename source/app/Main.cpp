#include "FenetrePrincipale.h"
#include "moteur/Reglages.h"

namespace groovebox
{

/** Comportement du moteur : on ouvre la carte son nous-mêmes, sorties seulement à l'étape 1. */
struct ComportementMoteur  : public te::EngineBehaviour
{
    bool autoInitialiseDeviceManager() override     { return false; }
};

class Application  : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override       { return "groovebox"; }
    const juce::String getApplicationVersion() override    { return ProjectInfo::versionString; }
    bool moreThanOneInstanceAllowed() override             { return false; }

    void initialise (const juce::String& ligneDeCommande) override
    {
        journal.reset (juce::FileLogger::createDefaultAppLogger ("groovebox", "journal.txt",
                                                                 "groovebox " + getApplicationVersion() + " démarré"));
        juce::Logger::setCurrentLogger (journal.get());

        engine = std::make_unique<te::Engine> (std::make_unique<Reglages>(),
                                               std::make_unique<te::UIBehaviour>(),
                                               std::make_unique<ComportementMoteur>());
        engine->getDeviceManager().initialise (0, 4);
        appliquerReglagesDeDepart (*engine);
        moteur = std::make_unique<Moteur> (*engine);

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
        fenetre.reset();
        diagnostic.reset();

        if (engine != nullptr)
        {
            engine->getDeviceManager().saveSettings();
            engine->getPropertyStorage().getPropertiesFile().saveIfNeeded();
        }

        moteur.reset();
        engine.reset();
        juce::Logger::writeToLog ("groovebox fermé");
        juce::Logger::setCurrentLogger (nullptr);
        journal.reset();
    }

    void systemRequestedQuit() override                    { quit(); }

private:
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
                      << (moteur.getEdit().getTransport().isPlaying() ? "en cours" : "arrêtée");
            else
                bilan << "aucune carte son";

            juce::Logger::writeToLog (bilan);
            std::cout << bilan << std::endl;
            juce::JUCEApplication::getInstance()->systemRequestedQuit();
        }

        te::Engine& engine;
        Moteur& moteur;
        int restant, coupuresAuDepart = 0;
        float chargeMax = 0.0f;
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
