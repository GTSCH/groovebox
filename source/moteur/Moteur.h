#pragma once

#include "BipPlugin.h"

namespace groovebox
{

enum class Sortie
{
    principale, ///< Sorties 1-2 : la sono, jamais de clic
    casque      ///< Sorties 3-4 : le casque du batteur
};

/** Le moteur audio : une lecture qui tourne en continu, et un bip de test par sortie.
    Le bip du casque n'est jamais envoyé sur les sorties 1-2 : sans sorties 3-4, il reste muet. */
class Moteur  : private juce::ChangeListener
{
public:
    explicit Moteur (te::Engine&);
    ~Moteur() override;

    void activerBip (Sortie, bool actif);
    bool bipActif (Sortie) const;
    void reglerNiveauBip (Sortie, float niveau);

    /** Vrai si la carte son choisie a des sorties 3-4. */
    bool sortieCasqueDisponible() const;

    te::Edit& getEdit()                     { return *edit; }

    /** Appelé (fil principal) quand la carte son ou ses réglages changent. */
    std::function<void()> surChangementDeCarte;

private:
    te::Engine& engine;
    std::unique_ptr<te::Edit> edit;
    te::AudioTrack* pistePrincipale = nullptr;
    te::AudioTrack* pisteCasque = nullptr;
    BipPlugin* bipPrincipal = nullptr;
    BipPlugin* bipCasque = nullptr;

    BipPlugin* bip (Sortie) const;
    te::WaveOutputDevice* trouverSortie (int premierCanal) const;
    void appliquerRoutage();
    void changeListenerCallback (juce::ChangeBroadcaster*) override;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Moteur)
};

} // namespace groovebox
