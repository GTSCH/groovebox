#pragma once

#include "BanqueDeSons.h"
#include "modele/Kit.h"

namespace groovebox
{
namespace te = tracktion;

/** La batterie : 16 pads (notes 36 à 51), chacun lisant un son avec hauteur, décroissance, saturation et couleur ;
    groupes d'étouffement ; compression et largeur du kit. Aucune réservation de mémoire dans le fil audio. */
class BatteriePlugin  : public te::Plugin
{
public:
    static const char* xmlTypeName;

    explicit BatteriePlugin (te::PluginCreationInfo);
    ~BatteriePlugin() override;

    juce::String getName() const override                       { return "Batterie"; }
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

    /** Fil principal : applique un kit. Un pad dont le son est un dossier de niveaux (batterie acoustique) choisit
        le niveau de frappe selon la vélocité, avec un léger hasard pour éviter l'effet mitraillette. */
    void appliquerKit (const Kit&, BanqueDeSons&);

    /** Position latérale de chaque pad quand la largeur vaut 1 (-1 gauche, +1 droite). */
    static float placementDuPad (int pad);

private:
    static constexpr int niveauxMax = 12;

    struct Reglages
    {
        std::array<std::atomic<const Echantillon*>, niveauxMax> niveaux {};    // du plus doux au plus fort
        std::atomic<int> nombreDeNiveaux { 0 };
        std::atomic<float> vitesse { 1.0f };        // 2^(demi-tons / 12)
        std::atomic<float> dureeDeVie { 0.0f };     // secondes ; 0 : le son entier
        std::atomic<float> saturation { 0.0f }, couleur { 0.0f }, gain { 1.0f }, panoramique { 0.0f };
        std::atomic<int> etouffement { 0 };
    };

    struct Voix
    {
        const Echantillon* son = nullptr;
        double position = 0.0, pas = 1.0;
        float gain = 0.0f, fondu = 1.0f, pasDeFondu = 0.0f;
        double age = 0.0, dureeDeVie = 0.0;
        float bas = 0.0f, bas2 = 0.0f, haut = 0.0f; // mémoires du filtre de couleur
        bool active = false;
    };

    std::array<Reglages, Kit::nombreDePads> reglages;
    std::array<std::array<Voix, 2>, Kit::nombreDePads> voix;     // 2 voix par pad : la nouvelle et celle qui s'efface
    std::atomic<float> compression { 0.0f };
    double frequenceEchantillonnage = 48000.0;
    float enveloppeDuCompresseur = 0.0f;
    juce::uint32 hasardDesNiveaux = 12345;      // fil audio seulement

    void frapper (int pad, float velocite);
    void rendre (juce::AudioBuffer<float>&, int debut, int nombre);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BatteriePlugin)
};

} // namespace groovebox
