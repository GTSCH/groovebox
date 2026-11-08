#pragma once

#include "BanqueDeSons.h"
#include "modele/Sampler.h"

namespace groovebox
{
namespace te = tracktion;

/** Le sampler : un son lu en un coup, en notes, découpé en 16 tranches ou en boucle calée au tempo ;
    hauteur, filtre, attaque, relâche, lecture à l'envers. 8 voix. Aucune réservation de mémoire dans le fil audio. */
class SamplerPlugin  : public te::Plugin
{
public:
    static const char* xmlTypeName;
    static constexpr int nombreDeVoix = 8;

    explicit SamplerPlugin (te::PluginCreationInfo);
    ~SamplerPlugin() override;

    juce::String getName() const override                       { return "Sampler"; }
    juce::String getPluginType() override                       { return xmlTypeName; }
    juce::String getShortName (int) override                    { return getName(); }
    juce::String getSelectableDescription() override            { return getName(); }
    bool producesAudioWhenNoAudioInput() override               { return true; }
    bool isSynth() override                                     { return true; }
    bool takesMidiInput() override                              { return true; }
    int getNumOutputChannelsGivenInputs (int) override          { return 2; }

    void initialise (const te::PluginInitialisationInfo&) override;
    void deinitialise() override {}
    void applyToBuffer (const te::PluginRenderContext&) override;

    /** Fil principal : applique les réglages (le son doit déjà être chargé dans la banque).
        secondesDeBoucle : durée que doit avoir la boucle au tempo du morceau (mode boucle). */
    void appliquer (const ReglagesSampler&, const Echantillon* son, double secondesDeBoucle);

    /** N'importe quel fil : la boucle déjà étirée au tempo (nulle : on lit le son d'origine, accéléré ou ralenti).
        Le son doit rester en vie tant qu'il peut être lu (voir EtireurDeBoucles). */
    void installerBoucle (const Echantillon* etiree)            { boucleEtiree = etiree; }
    const Echantillon* getBoucleEtiree() const                  { return boucleEtiree.load(); }

private:
    struct Voix
    {
        bool active = false, relachee = false, boucle = false;
        int note = -1;
        const Echantillon* son = nullptr;
        double position = 0.0, pas = 1.0;       // dans le son, en échantillons, depuis le début de la partie
        double debut = 0.0, longueur = 0.0;     // partie lue, en échantillons du son
        float gain = 0.0f, niveau = 0.0f, pasDAttaque = 1.0f, facteurDeRelache = 0.0f;
        float fondu = 1.0f, pasDeFondu = 0.0f;
    };

    std::atomic<const Echantillon*> son { nullptr }, boucleEtiree { nullptr };
    std::atomic<int> mode { 0 };
    std::atomic<double> debut { 0.0 }, fin { 1.0 }, secondesDeBoucle { 2.0 };
    std::atomic<float> vitesse { 1.0f }, filtre { 0.0f }, attaque { 0.0f }, relache { 0.2f }, gain { 1.0f };
    std::atomic<bool> envers { false };

    std::array<Voix, nombreDeVoix> voix;
    double frequenceEchantillonnage = 48000.0;
    std::array<float, 2> bas1 {}, bas2 {}, bande1 {};      // filtre à variable d'état, par canal

    void demarrer (int note, float velocite);
    void relacher (int note);
    void toutRelacher();
    void rendre (juce::AudioBuffer<float>&, int debut, int nombre);
    float lire (const Voix&, int canal) const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SamplerPlugin)
};

/** Étire les boucles au tempo, sur un fil à part (SoundTouch par le moteur audio), et les installe dans les samplers.
    Les boucles remplacées restent en mémoire quelques secondes : le fil audio a fini de les lire avant qu'on les libère. */
class EtireurDeBoucles  : private juce::Thread
{
public:
    EtireurDeBoucles();
    ~EtireurDeBoucles() override;

    struct Demande
    {
        const Echantillon* son = nullptr;
        double debut = 0.0, fin = 1.0;          // partie du son, en part (0 à 1)
        double secondesVisees = 2.0;
        float demiTons = 0.0f;
        std::vector<SamplerPlugin*> samplers;

        bool memeChose (const Demande& d) const
        {
            return son == d.son && debut == d.debut && fin == d.fin && std::abs (secondesVisees - d.secondesVisees) < 1.0e-6 && demiTons == d.demiTons;
        }
    };

    /** Fil principal. Une demande par piste : la plus récente remplace celle qui attendait.
        Renvoie faux si cette boucle est déjà prête ou déjà demandée. */
    bool demander (int piste, Demande);
    void annuler (int piste);

    /** Attend que tout soit étiré (essais). */
    void attendre();

    /** Le calcul lui-même : la partie, répétée trois fois, étirée ; on garde le tour du milieu (bouclage sans raccord). */
    static std::unique_ptr<Echantillon> etirer (const Echantillon&, double debut, double fin, double secondesVisees, float demiTons, double frequenceDeSortie);

private:
    juce::CriticalSection verrou;
    std::map<int, Demande> enAttente;
    std::map<int, Demande> faites;
    std::vector<std::pair<std::unique_ptr<Echantillon>, double>> enService;     // boucle, instant où elle a été remplacée (0 : en service)
    std::map<int, Echantillon*> courantes;
    std::atomic<bool> occupe { false };

    void run() override;
    void ranger (int piste, std::unique_ptr<Echantillon>);
};

} // namespace groovebox
