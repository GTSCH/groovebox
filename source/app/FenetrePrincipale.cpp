#include "FenetrePrincipale.h"

namespace groovebox
{

namespace
{
    const auto couleurFond   = juce::Colour (0xff15171c);
    const auto couleurTexte  = juce::Colour (0xffe8e8ea);
    const auto couleurDiscret = juce::Colour (0xff9a9ca3);
    const auto couleurActif  = juce::Colour (0xff3fb27f);

    juce::String formaterLatence (int tampon, double frequence)
    {
        return juce::String (1000.0 * tampon / frequence, 1).replaceCharacter ('.', ',') + " ms";
    }

    juce::String formaterNombre (double valeur)
    {
        // 48000 devient « 48 000 », à la française
        auto texte = juce::String ((juce::int64) std::round (valeur));
        juce::String resultat;

        for (int i = 0; i < texte.length(); ++i)
        {
            if (i > 0 && (texte.length() - i) % 3 == 0)
                resultat << " ";

            resultat << texte[i];
        }

        return resultat;
    }
}

FenetrePrincipale::FenetrePrincipale (te::Engine& e, Moteur& m)
    : engine (e), moteur (m)
{
    titre.setText ("groovebox", juce::dontSendNotification);
    titre.setFont (juce::FontOptions (40.0f, juce::Font::bold));
    titre.setColour (juce::Label::textColourId, couleurTexte);
    addAndMakeVisible (titre);

    etat.setFont (juce::FontOptions (19.0f));
    etat.setColour (juce::Label::textColourId, couleurDiscret);
    etat.setJustificationType (juce::Justification::topLeft);
    addAndMakeVisible (etat);

    aideCasque.setFont (juce::FontOptions (16.0f));
    aideCasque.setColour (juce::Label::textColourId, couleurDiscret);
    addAndMakeVisible (aideCasque);

    for (auto* bouton : { &boutonBip, &boutonCasque })
    {
        bouton->setClickingTogglesState (true);
        bouton->setColour (juce::TextButton::buttonOnColourId, couleurActif);
        addAndMakeVisible (*bouton);
    }

    boutonBip.onClick = [this] { moteur.activerBip (Sortie::principale, boutonBip.getToggleState()); mettreAJourBoutons(); };
    boutonCasque.onClick = [this] { moteur.activerBip (Sortie::casque, boutonCasque.getToggleState()); mettreAJourBoutons(); };

    const std::pair<juce::Label*, const char*> libelles[] = { { &libelleSortie, "Sortie" }, { &libelleFrequence, "Fréquence" }, { &libelleTampon, "Tampon" } };

    for (auto [libelle, texte] : libelles)
    {
        libelle->setText (juce::String::fromUTF8 (texte), juce::dontSendNotification);
        libelle->setFont (juce::FontOptions (18.0f));
        libelle->setColour (juce::Label::textColourId, couleurTexte);
        libelle->setJustificationType (juce::Justification::centredRight);
        addAndMakeVisible (*libelle);
    }

    for (auto* choix : { &choixSortie, &choixFrequence, &choixTampon })
        addAndMakeVisible (*choix);

    choixSortie.onChange = [this]
    {
        const auto nom = choixSortie.getText();
        changerCarte ([nom] (auto& reglage) { reglage.outputDeviceName = nom; });
    };

    choixFrequence.onChange = [this]
    {
        const auto frequence = (double) choixFrequence.getSelectedId();
        changerCarte ([frequence] (auto& reglage) { reglage.sampleRate = frequence; });
    };

    choixTampon.onChange = [this]
    {
        const auto tampon = choixTampon.getSelectedId();
        changerCarte ([tampon] (auto& reglage) { reglage.bufferSize = tampon; });
    };

    auto& cartes = engine.getDeviceManager().deviceManager;
    cartes.addChangeListener (this);

    if (auto type = cartes.getCurrentDeviceTypeObject())
        type->addListener (this);

    mettreAJourChoixCarte();

    if (auto contexte = moteur.getEdit().getCurrentPlaybackContext())
        contexte->masterLevels.addClient (mesureNiveau);

    moteur.surChangementDeCarte = [this] { mettreAJourBoutons(); };

    mettreAJourBoutons();
    setSize (760, 600);
    startTimerHz (20);
}

FenetrePrincipale::~FenetrePrincipale()
{
    moteur.surChangementDeCarte = nullptr;

    auto& cartes = engine.getDeviceManager().deviceManager;
    cartes.removeChangeListener (this);

    if (auto type = cartes.getCurrentDeviceTypeObject())
        type->removeListener (this);

    if (auto contexte = moteur.getEdit().getCurrentPlaybackContext())
        contexte->masterLevels.removeClient (mesureNiveau);
}

void FenetrePrincipale::mettreAJourBoutons()
{
    boutonBip.setToggleState (moteur.bipActif (Sortie::principale), juce::dontSendNotification);
    boutonCasque.setToggleState (moteur.bipActif (Sortie::casque), juce::dontSendNotification);

    const bool casque = moteur.sortieCasqueDisponible();
    boutonCasque.setEnabled (casque);
    aideCasque.setText (casque ? "Bip plus aigu, entendu seulement au casque du batteur."
                               : "Cette carte son n'a pas de sorties 3-4 : le test casque attend la Scarlett.",
                        juce::dontSendNotification);
}

void FenetrePrincipale::changerCarte (const std::function<void (juce::AudioDeviceManager::AudioDeviceSetup&)>& modifier)
{
    auto& cartes = engine.getDeviceManager().deviceManager;
    auto reglage = cartes.getAudioDeviceSetup();
    modifier (reglage);

    // Toujours les sorties 1 à 4 quand la carte les a : 1-2 pour la sono, 3-4 pour le casque du batteur
    reglage.useDefaultOutputChannels = false;
    reglage.outputChannels.clear();
    reglage.outputChannels.setRange (0, 4, true);

    const auto erreur = cartes.setAudioDeviceSetup (reglage, true);

    if (erreur.isNotEmpty())
    {
        juce::Logger::writeToLog ("Changement de carte son impossible : " + erreur);
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Carte son",
                                                "Ce réglage n'a pas pu être appliqué :\n" + erreur);
    }
    else
    {
        engine.getDeviceManager().saveSettings();
    }

    mettreAJourChoixCarte();
}

void FenetrePrincipale::mettreAJourChoixCarte()
{
    auto& cartes = engine.getDeviceManager().deviceManager;
    auto carte = cartes.getCurrentAudioDevice();

    choixSortie.clear (juce::dontSendNotification);

    if (auto type = cartes.getCurrentDeviceTypeObject())
    {
        const auto noms = type->getDeviceNames (false);

        for (int i = 0; i < noms.size(); ++i)
            choixSortie.addItem (noms[i], i + 1);

        if (carte != nullptr)
            choixSortie.setSelectedId (noms.indexOf (carte->getName()) + 1, juce::dontSendNotification);
    }

    choixFrequence.clear (juce::dontSendNotification);
    choixTampon.clear (juce::dontSendNotification);

    if (carte == nullptr)
        return;

    for (auto frequence : carte->getAvailableSampleRates())
        choixFrequence.addItem (formaterNombre (frequence) + " Hz", juce::roundToInt (frequence));

    choixFrequence.setSelectedId (juce::roundToInt (carte->getCurrentSampleRate()), juce::dontSendNotification);

    for (auto tampon : carte->getAvailableBufferSizes())
        if (tampon >= 32 && tampon <= 1024)
            choixTampon.addItem (juce::String (tampon) + " échantillons (" + formaterLatence (tampon, carte->getCurrentSampleRate()) + ")", tampon);

    choixTampon.setSelectedId (carte->getCurrentBufferSizeSamples(), juce::dontSendNotification);
}

void FenetrePrincipale::timerCallback()
{
    auto& cartes = engine.getDeviceManager().deviceManager;
    juce::String texte;

    if (auto carte = cartes.getCurrentAudioDevice())
    {
        const auto frequence = carte->getCurrentSampleRate();
        const auto tampon = carte->getCurrentBufferSizeSamples();
        const auto decrochages = carte->getXRunCount();

        texte << "Carte son : " << carte->getName() << " (" << carte->getActiveOutputChannels().countNumberOfSetBits() << " sorties)\n"
              << formaterNombre (frequence) << " Hz, tampon de " << tampon << " échantillons ("
              << formaterLatence (tampon, frequence) << ")\n"
              << "Charge du moteur : " << juce::roundToInt (engine.getDeviceManager().getCpuUsage() * 100.0f) << " %\n"
              << "Coupures du son depuis l'ouverture : " << (decrochages < 0 ? juce::String ("non mesurées") : juce::String (decrochages));
    }
    else
    {
        texte << "Aucune carte son ouverte : choisis une sortie ci-dessous.";
    }

    etat.setText (texte, juce::dontSendNotification);

    const auto niveau = juce::jmax (mesureNiveau.getAndClearAudioLevel (0).dB, mesureNiveau.getAndClearAudioLevel (1).dB);
    const auto cible = juce::jlimit (0.0f, 1.0f, (niveau + 60.0f) / 60.0f);
    niveauAffiche = cible > niveauAffiche ? cible : niveauAffiche * 0.85f;
    repaint (zoneNiveau);
}

void FenetrePrincipale::paint (juce::Graphics& g)
{
    g.fillAll (couleurFond);

    g.setColour (couleurDiscret);
    g.setFont (juce::FontOptions (16.0f));
    g.drawText ("Niveau des sorties 1-2", zoneNiveau.withHeight (20).translated (0, -24), juce::Justification::left);

    g.setColour (juce::Colour (0xff2a2d35));
    g.fillRoundedRectangle (zoneNiveau.toFloat(), 6.0f);
    g.setColour (couleurActif);
    g.fillRoundedRectangle (zoneNiveau.toFloat().withWidth (zoneNiveau.getWidth() * niveauAffiche), 6.0f);
}

void FenetrePrincipale::resized()
{
    auto zone = getLocalBounds().reduced (28);

    titre.setBounds (zone.removeFromTop (54));
    etat.setBounds (zone.removeFromTop (110));
    zone.removeFromTop (12);

    auto ligneBoutons = zone.removeFromTop (56);
    boutonBip.setBounds (ligneBoutons.removeFromLeft (ligneBoutons.getWidth() / 2).reduced (0, 0).withTrimmedRight (8));
    boutonCasque.setBounds (ligneBoutons.withTrimmedLeft (8));
    aideCasque.setBounds (zone.removeFromTop (30));

    zone.removeFromTop (34);
    zoneNiveau = zone.removeFromTop (18);
    zone.removeFromTop (36);

    const std::pair<juce::Label*, juce::ComboBox*> lignes[] = { { &libelleSortie, &choixSortie },
                                                                { &libelleFrequence, &choixFrequence },
                                                                { &libelleTampon, &choixTampon } };

    for (auto [libelle, choix] : lignes)
    {
        auto ligne = zone.removeFromTop (36);
        libelle->setBounds (ligne.removeFromLeft (120));
        choix->setBounds (ligne.withTrimmedLeft (12).withHeight (32));
        zone.removeFromTop (10);
    }
}

} // namespace groovebox
