// Essais automatiques de groovebox : ce qui se vérifie sans oreille.
// Une carte son simulée (4 sorties, puis 2) remplace la vraie : aucun son ne sort du Mac.

#include <JuceHeader.h>
#include <tracktion_engine/testing/tracktion_EnginePlayer.h>
#include "moteur/Moteur.h"
#include "moteur/Reglages.h"

namespace groovebox
{

namespace
{
    struct ComportementEssai  : public te::EngineBehaviour
    {
        bool autoInitialiseDeviceManager() override     { return false; }
    };

    te::HostedAudioDeviceInterface::Parameters carteSimulee (int sorties)
    {
        return { .sampleRate = 48000.0, .blockSize = 128, .inputChannels = 0, .outputChannels = sorties,
                 .inputNames = {}, .outputNames = {} };
    }

    struct Mesure
    {
        float niveauEfficace = 0.0f;
        float crete = 0.0f;
        float sautMax = 0.0f;
        double frequence = 0.0;
    };

    Mesure mesurer (const choc::buffer::ChannelArrayBuffer<float>& audio, int canal, int debut, int fin)
    {
        Mesure m;
        auto donnees = audio.getChannel ((choc::buffer::ChannelCount) canal).data.data;
        double somme = 0.0;
        int passagesParZero = 0;

        for (int i = debut; i < fin; ++i)
        {
            const auto x = donnees[i];
            somme += x * x;
            m.crete = juce::jmax (m.crete, std::abs (x));

            if (i > debut)
            {
                m.sautMax = juce::jmax (m.sautMax, std::abs (x - donnees[i - 1]));

                if (donnees[i - 1] <= 0.0f && x > 0.0f)
                    ++passagesParZero;
            }
        }

        const auto n = fin - debut;
        m.niveauEfficace = (float) std::sqrt (somme / n);
        // Le bip sonne la moitié du temps : on compte les montées par zéro sur la durée totale, puis on double
        m.frequence = 2.0 * passagesParZero * 48000.0 / n;
        return m;
    }
}

class EssaisMoteur  : public juce::UnitTest
{
public:
    EssaisMoteur()  : juce::UnitTest ("Moteur", "groovebox") {}

    void runTest() override
    {
        auto& engine = *te::Engine::getEngines()[0];
        const int uneSeconde = 48000;

        beginTest ("Le son de test sort sur les sorties 1-2, et seulement là");
        {
            te::test_utilities::EnginePlayer lecteur (engine, carteSimulee (4));
            Moteur moteur (engine);
            expect (moteur.sortieCasqueDisponible(), "les sorties 3-4 devraient être vues");

            moteur.activerBip (Sortie::principale, true);
            lecteur.process (2 * uneSeconde);
            auto audio = lecteur.getOutput();

            for (int canal : { 0, 1 })
            {
                auto m = mesurer (audio, canal, uneSeconde / 2, 2 * uneSeconde);
                expect (m.niveauEfficace > 0.05f, "niveau trop faible sur la sortie " + juce::String (canal + 1) + " : " + juce::String (m.niveauEfficace));
                expectWithinAbsoluteError (m.frequence, 440.0, 15.0, "hauteur du bip sur la sortie " + juce::String (canal + 1));
            }

            for (int canal : { 2, 3 })
                expectEquals (mesurer (audio, canal, 0, 2 * uneSeconde).crete, 0.0f, "rien ne doit sortir sur " + juce::String (canal + 1));
        }

        beginTest ("Le test casque sort sur les sorties 3-4, jamais sur 1-2");
        {
            te::test_utilities::EnginePlayer lecteur (engine, carteSimulee (4));
            Moteur moteur (engine);

            moteur.activerBip (Sortie::casque, true);
            expect (moteur.bipActif (Sortie::casque));
            lecteur.process (2 * uneSeconde);
            auto audio = lecteur.getOutput();

            for (int canal : { 0, 1 })
                expectEquals (mesurer (audio, canal, 0, 2 * uneSeconde).crete, 0.0f, "le casque ne doit jamais aller sur " + juce::String (canal + 1));

            for (int canal : { 2, 3 })
            {
                auto m = mesurer (audio, canal, uneSeconde / 2, 2 * uneSeconde);
                expect (m.niveauEfficace > 0.05f, "niveau trop faible sur la sortie " + juce::String (canal + 1) + " : " + juce::String (m.niveauEfficace));
                expectWithinAbsoluteError (m.frequence, 880.0, 25.0, "hauteur du bip casque sur la sortie " + juce::String (canal + 1));
            }
        }

        beginTest ("Sans sorties 3-4, le test casque reste muet");
        {
            te::test_utilities::EnginePlayer lecteur (engine, carteSimulee (2));
            Moteur moteur (engine);
            expect (! moteur.sortieCasqueDisponible());

            moteur.activerBip (Sortie::casque, true);
            expect (! moteur.bipActif (Sortie::casque), "le bouton casque doit refuser de s'allumer");
            lecteur.process (uneSeconde);
            auto audio = lecteur.getOutput();

            for (int canal : { 0, 1 })
                expectEquals (mesurer (audio, canal, 0, uneSeconde).crete, 0.0f);
        }

        beginTest ("Une minute de son sans coupure ni clic, et la lecture continue");
        {
            te::test_utilities::EnginePlayer lecteur (engine, carteSimulee (2));
            Moteur moteur (engine);
            moteur.activerBip (Sortie::principale, true);

            for (int seconde = 0; seconde < 60; ++seconde)
                lecteur.process (uneSeconde);

            auto audio = lecteur.getOutput();
            auto toutLeSon = mesurer (audio, 0, 0, 60 * uneSeconde);
            auto derniereSeconde = mesurer (audio, 0, 59 * uneSeconde, 60 * uneSeconde);

            // Un sinus de 440 Hz à 0,25 ne bouge jamais de plus de 0,015 d'un échantillon au suivant
            expect (toutLeSon.sautMax < 0.03f, "saut brusque (clic) : " + juce::String (toutLeSon.sautMax));
            expect (toutLeSon.crete < 0.3f, "niveau trop fort : " + juce::String (toutLeSon.crete));
            expect (derniereSeconde.niveauEfficace > 0.05f, "le son s'est arrêté avant la fin de la minute");
            expect (moteur.getEdit().getTransport().isPlaying(), "la lecture s'est arrêtée");
        }

        beginTest ("Couper puis rallumer le bip ne fait pas de clic");
        {
            te::test_utilities::EnginePlayer lecteur (engine, carteSimulee (2));
            Moteur moteur (engine);

            for (int i = 0; i < 10; ++i)
            {
                moteur.activerBip (Sortie::principale, i % 2 == 0);
                lecteur.process (uneSeconde / 7);
            }

            auto audio = lecteur.getOutput();
            expect (mesurer (audio, 0, 0, audio.getNumFrames()).sautMax < 0.03f);
        }
    }
};

class EssaisReglages  : public juce::UnitTest
{
public:
    EssaisReglages()  : juce::UnitTest ("Réglages", "groovebox") {}

    void runTest() override
    {
        beginTest ("Les réglages se sauvegardent et se rechargent");
        {
            auto dossier = juce::File::createTempFile ("groovebox-reglages");
            dossier.createDirectory();

            {
                Reglages reglages (dossier);
                reglages.getPropertiesFile().setValue ("essai", 128);
                reglages.getPropertiesFile().saveIfNeeded();
            }

            expect (dossier.getChildFile ("Settings.xml").existsAsFile(), "fichier de réglages absent");

            {
                Reglages reglages (dossier);
                expectEquals (reglages.getPropertiesFile().getIntValue ("essai"), 128);
            }

            dossier.deleteRecursively();
        }

        beginTest ("Le dossier des réglages est dans Application Support");
        expect (Reglages::dossierParDefaut().getFullPathName().contains ("Library/Application Support/groovebox"));
    }
};

static EssaisMoteur essaisMoteur;
static EssaisReglages essaisReglages;

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
