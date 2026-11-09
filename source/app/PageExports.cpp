#include "PageExports.h"
#include "Style.h"
#include "modele/Prise.h"

namespace groovebox
{

//==============================================================================
PageExports::Tache::Tache (juce::File d, std::function<void (const juce::String&)> ligne, std::function<void (bool)> fin)
    : juce::Thread ("Export"), demande (std::move (d)), surLigne (std::move (ligne)), surFin (std::move (fin))
{
}

PageExports::Tache::~Tache()
{
    // Fermer la page pendant un export : le second groovebox s'arrête
    if (processus.isRunning())
        processus.kill();

    stopThread (4000);
}

void PageExports::Tache::run()
{
    const auto programme = juce::File::getSpecialLocation (juce::File::currentExecutableFile).getFullPathName();
    bool reussi = processus.start (juce::StringArray { programme, "--exporter", demande.getFullPathName() }, juce::ChildProcess::wantStdOut);
    juce::String reste;
    char tampon[1024];

    while (reussi && ! threadShouldExit())
    {
        const int lus = processus.readProcessOutput (tampon, sizeof (tampon));

        if (lus <= 0)
        {
            if (! processus.isRunning())
                break;

            juce::Thread::sleep (20);
            continue;
        }

        reste << juce::String::fromUTF8 (tampon, lus);

        while (reste.containsChar ('\n'))
        {
            surLigne (reste.upToFirstOccurrenceOf ("\n", false, false));
            reste = reste.fromFirstOccurrenceOf ("\n", false, false);
        }
    }

    if (reste.isNotEmpty())
        surLigne (reste);

    reussi = reussi && ! threadShouldExit() && processus.getExitCode() == 0;
    demande.deleteFile();
    surFin (reussi);
}

//==============================================================================
PageExports::PageExports (Moteur& m)  : moteur (m)
{
    titre.setText ("Exports", juce::dontSendNotification);
    titre.setFont (juce::FontOptions (30.0f, juce::Font::bold));
    titre.setColour (juce::Label::textColourId, couleurTexte);
    addAndMakeVisible (titre);

    for (auto* l : { &infos, &etat, &resultat })
    {
        l->setFont (juce::FontOptions (17.0f));
        l->setColour (juce::Label::textColourId, couleurDiscret);
        l->setJustificationType (juce::Justification::topLeft);
        addAndMakeVisible (*l);
    }

    infos.setText ("En WAV 48 kHz 24 bits, dans le dossier « exports » du morceau. Le calcul va plus vite que la lecture, "
                   "sans couper la musique qui joue."_fr, juce::dontSendNotification);

    for (auto [l, texte] : { std::pair { &etiquetteSource, "Quoi" }, std::pair { &etiquetteMesures, "Longueur" }, std::pair { &etiquettePrise, "Prise" } })
    {
        l->setText (juce::String::fromUTF8 (texte), juce::dontSendNotification);
        l->setFont (juce::FontOptions (19.0f, juce::Font::bold));
        l->setColour (juce::Label::textColourId, couleurTexte);
        addAndMakeVisible (*l);
    }

    for (int mesures : { 1, 2, 4, 8, 16, 32, 64 })
        choixMesures.addItem (juce::String (mesures) + (mesures > 1 ? " mesures" : " mesure"), mesures);

    choixMesures.setSelectedId (8, juce::dontSendNotification);
    choixSource.onChange = [this] { mettreAJour(); };

    for (auto* c : { &choixSource, &choixMesures, &choixPrise })
    {
        c->setWantsKeyboardFocus (false);
        addAndMakeVisible (*c);
    }

    // Le geste unique du cahier des charges : la musique sans la guide, avec le clic, pour répéter chez soi
    boutonUnGeste.setColour (juce::TextButton::buttonColourId, couleurActif);
    boutonUnGeste.setColour (juce::TextButton::textColourOffId, juce::Colours::black);
    boutonUnGeste.onClick = [this] { exporter ({ exports::Rendu::sansGuideAvecClic }); };

    boutonSelection.onClick = [this]
    {
        std::vector<exports::Rendu> rendus;

        if (caseComplet.getToggleState())     rendus.push_back (exports::Rendu::complet);
        if (caseSansGuide.getToggleState())   rendus.push_back (exports::Rendu::sansGuide);
        if (caseAvecClic.getToggleState())    rendus.push_back (exports::Rendu::sansGuideAvecClic);
        if (casePistes.getToggleState())      rendus.push_back (exports::Rendu::pistes);

        if (! rendus.empty())
            exporter (rendus);
    };

    boutonFinder.onClick = [this]
    {
        if (dernierFichier.existsAsFile())
            dernierFichier.revealToUser();
        else if (dossierDesExports().isDirectory())
            dossierDesExports().revealToUser();
    };

    boutonPrise.onClick = [this] { exporterLaPrise(); };

    caseComplet.setToggleState (true, juce::dontSendNotification);
    caseAvecClic.setToggleState (true, juce::dontSendNotification);

    for (auto* c : { &caseComplet, &caseSansGuide, &caseAvecClic, &casePistes })
    {
        c->setColour (juce::ToggleButton::textColourId, couleurTexte);
        c->setColour (juce::ToggleButton::tickColourId, couleurActif);
        c->setWantsKeyboardFocus (false);
        c->onClick = [this] { mettreAJour(); };
        addAndMakeVisible (*c);
    }

    for (auto* b : { &boutonUnGeste, &boutonSelection, &boutonFinder, &boutonPrise })
    {
        b->setWantsKeyboardFocus (false);
        addAndMakeVisible (*b);
    }

    barre.setColour (juce::ProgressBar::foregroundColourId, couleurActif);
    barre.setColour (juce::ProgressBar::backgroundColourId, couleurPanneau);
    addAndMakeVisible (barre);

    relire();
    startTimerHz (2);
}

PageExports::~PageExports()
{
    tache.reset();
}

juce::File PageExports::dossierDuMorceau() const
{
    const auto prises = moteur.dossierDesPrises ? moteur.dossierDesPrises() : juce::File();
    return prises == juce::File() ? juce::File() : prises.getParentDirectory();
}

void PageExports::visibilityChanged()
{
    if (isVisible())
        relire();
}

void PageExports::relire()
{
    const auto& morceau = moteur.getMorceau();
    morceauAffiche = morceau.nom;
    const auto avant = choixSource.getText();
    sources.clear();

    // Les déroulés du morceau, puis chaque scène qui a des clips
    for (auto& f : dossierDuMorceau().getChildFile ("deroules").findChildFiles (juce::File::findFiles, false, "*.json"))
        if (auto d = Deroule::charger (f); d && ! d->sections.empty())
            sources.push_back ({ "Déroulé « "_fr + d->nom + " »"_fr, d, 0 });

    std::sort (sources.begin(), sources.end(), [] (auto& a, auto& b) { return a.nom.compareNatural (b.nom) < 0; });

    for (int s = 0; s < (int) morceau.scenes.size(); ++s)
        if (! morceau.scenes[(size_t) s].estVide())
        {
            // Le nom de la scène, s'il dit autre chose que « Scène N »
            const auto numero = "Scène "_fr + juce::String (s + 1);
            const auto& nom = morceau.scenes[(size_t) s].nom;
            sources.push_back ({ numero + (nom.isNotEmpty() && nom != numero ? " « "_fr + nom + " »"_fr : juce::String()), std::nullopt, s });
        }

    choixSource.clear (juce::dontSendNotification);

    for (int i = 0; i < (int) sources.size(); ++i)
        choixSource.addItem (sources[(size_t) i].nom, i + 1);

    choixSource.setText (avant, juce::dontSendNotification);

    if (choixSource.getSelectedId() == 0 && ! sources.empty())
        choixSource.setSelectedId (1, juce::dontSendNotification);

    // Les prises, la plus récente en premier
    prises.clear();
    choixPrise.clear (juce::dontSendNotification);

    for (auto& d : dossierDuMorceau().getChildFile ("prises").findChildFiles (juce::File::findDirectories, false))
        if (d.getChildFile ("sortie.wav").existsAsFile())
            prises.push_back (d);

    std::sort (prises.begin(), prises.end(), [] (auto& a, auto& b) { return a.getFileName() > b.getFileName(); });

    for (int i = 0; i < (int) prises.size(); ++i)
    {
        const auto p = Prise::charger (prises[(size_t) i].getChildFile ("prise.json"));
        choixPrise.addItem (p ? p->nom + ", " + p->date.formatted ("%d.%m.%Y %H:%M") : prises[(size_t) i].getFileName(), i + 1);
    }

    if (! prises.empty())
        choixPrise.setSelectedId (1, juce::dontSendNotification);

    mettreAJour();
}

void PageExports::mettreAJour()
{
    const int i = choixSource.getSelectedId() - 1;
    const bool scene = i >= 0 && i < (int) sources.size() && ! sources[(size_t) i].deroule;
    const bool libre = tache == nullptr;
    const bool avecGuide = std::any_of (moteur.getMorceau().pistes.begin(), moteur.getMorceau().pistes.end(), [] (const Piste& p) { return p.estUneGuide(); });

    etiquetteMesures.setVisible (scene);
    choixMesures.setVisible (scene);
    boutonUnGeste.setButtonText (avecGuide ? "Mix sans guide + clic" : "Mix + clic");
    caseAvecClic.setButtonText (avecGuide ? "Mix sans guide + clic" : "Mix + clic");
    caseSansGuide.setEnabled (avecGuide && libre);

    choixSource.setEnabled (libre);
    choixMesures.setEnabled (libre);

    boutonUnGeste.setEnabled (libre && i >= 0);
    boutonSelection.setEnabled (libre && i >= 0 && (caseComplet.getToggleState() || (caseSansGuide.getToggleState() && avecGuide)
                                                    || caseAvecClic.getToggleState() || casePistes.getToggleState()));

    for (auto* c : { &caseComplet, &caseAvecClic, &casePistes })
        c->setEnabled (libre);

    boutonPrise.setEnabled (! prises.empty());
    choixPrise.setEnabled (! prises.empty());
    boutonFinder.setEnabled (dossierDesExports().isDirectory());

    if (sources.empty() && libre)
        etat.setText ("Rien à exporter : ce morceau n'a ni déroulé ni scène avec des clips."_fr, juce::dontSendNotification);
}

void PageExports::exporter (std::vector<exports::Rendu> rendus)
{
    const int i = choixSource.getSelectedId() - 1;

    if (tache != nullptr || i < 0 || i >= (int) sources.size() || dossierDuMorceau() == juce::File())
        return;

    const auto& source = sources[(size_t) i];
    exports::Demande demande;
    demande.morceau = moteur.getMorceau();
    demande.deroule = source.deroule;
    demande.scene = source.scene;
    demande.mesures = juce::jmax (1, choixMesures.getSelectedId());
    demande.rendus = std::move (rendus);
    demande.dossier = dossierDesExports();
    demande.nom = source.deroule ? source.deroule->nom
                                 : "Scène "_fr + juce::String (source.scene + 1) + ", " + juce::String (demande.mesures) + (demande.mesures > 1 ? " mesures" : " mesure");

    const auto fichier = juce::File::getSpecialLocation (juce::File::tempDirectory).getNonexistentChildFile ("groovebox-export", ".json");
    fichier.replaceWithText (juce::JSON::toString (demande.versJson()));

    progression = 0.0;
    fichiersFaits.clear();
    remarques.clear();
    resultat.setText ({}, juce::dontSendNotification);
    etat.setText ("Calcul de « "_fr + demande.nom + " »..."_fr, juce::dontSendNotification);

    juce::Component::SafePointer<PageExports> page (this);
    tache = std::make_unique<Tache> (fichier,
        [page] (const juce::String& ligne) { juce::MessageManager::callAsync ([page, ligne] { if (page != nullptr) page->recevoir (ligne); }); },
        [page] (bool reussi) { juce::MessageManager::callAsync ([page, reussi] { if (page != nullptr) page->terminer (reussi); }); });
    tache->startThread();
    mettreAJour();
}

void PageExports::recevoir (const juce::String& ligne)
{
    const auto mot = ligne.upToFirstOccurrenceOf (" ", false, false);
    const auto reste = ligne.fromFirstOccurrenceOf (" ", false, false);

    if (mot == "progression")
    {
        progression = reste.upToFirstOccurrenceOf (" ", false, false).getDoubleValue();
        etat.setText ("Calcul : "_fr + reste.fromFirstOccurrenceOf (" ", false, false), juce::dontSendNotification);
    }
    else if (mot == "fichier")
    {
        fichiersFaits.add (juce::File (reste).getFileName());
        dernierFichier = juce::File (reste);
    }
    else if (mot == "remarque")
    {
        remarques.add (reste);
    }
    else if (mot == "fin")
    {
        const auto musique = reste.upToFirstOccurrenceOf (" ", false, false).getDoubleValue();
        const auto calcul = reste.fromFirstOccurrenceOf (" ", false, false).getDoubleValue();
        etat.setText ("Fini : " + juce::String (fichiersFaits.size()) + (fichiersFaits.size() > 1 ? " fichiers" : " fichier") + " de "
                      + juce::String (musique, 1).replaceCharacter ('.', ',') + " s, calculés en " + juce::String (calcul, 1).replaceCharacter ('.', ',') + " s."_fr,
                      juce::dontSendNotification);
    }
}

void PageExports::terminer (bool reussi)
{
    tache.reset();
    progression = reussi ? 1.0 : 0.0;

    if (! reussi)
        etat.setText ("L'export n'a pas abouti. Le journal de groovebox en dit plus."_fr, juce::dontSendNotification);

    auto texte = fichiersFaits.joinIntoString ("\n");

    if (! remarques.isEmpty())
        texte << (texte.isEmpty() ? "" : "\n") << remarques.joinIntoString ("\n");

    resultat.setText (texte, juce::dontSendNotification);
    mettreAJour();
}

void PageExports::exporterLaPrise()
{
    const int i = choixPrise.getSelectedId() - 1;

    if (i < 0 || i >= (int) prises.size())
        return;

    const auto p = Prise::charger (prises[(size_t) i].getChildFile ("prise.json"));
    const auto nom = p ? p->nom + " " + p->date.formatted ("%d.%m.%Y %Hh%M") : prises[(size_t) i].getFileName();
    const auto faits = exports::copierLaPrise (prises[(size_t) i], dossierDesExports(), nom);

    fichiersFaits.clear();

    for (auto& f : faits)
        fichiersFaits.add (juce::File (f).getFileName());

    if (! faits.isEmpty())
        dernierFichier = juce::File (faits[0]);

    etat.setText (faits.isEmpty() ? "La prise n'a pas pu être copiée."_fr : "Prise copiée dans les exports."_fr, juce::dontSendNotification);
    resultat.setText (fichiersFaits.joinIntoString ("\n"), juce::dontSendNotification);
    mettreAJour();
}

void PageExports::timerCallback()
{
    // Un autre morceau ouvert : ses déroulés, ses scènes, ses prises
    if (tache == nullptr && isShowing() && moteur.getMorceau().nom != morceauAffiche)
        relire();
}

void PageExports::paint (juce::Graphics& g)
{
    g.fillAll (couleurFond);
}

void PageExports::resized()
{
    auto zone = getLocalBounds().reduced (20);
    titre.setBounds (zone.removeFromTop (44));
    infos.setBounds (zone.removeFromTop (30));
    zone.removeFromTop (16);

    auto ligne = zone.removeFromTop (40);
    etiquetteSource.setBounds (ligne.removeFromLeft (110));
    choixSource.setBounds (ligne.removeFromLeft (420).reduced (0, 4));
    ligne.removeFromLeft (30);
    etiquetteMesures.setBounds (ligne.removeFromLeft (110));
    choixMesures.setBounds (ligne.removeFromLeft (160).reduced (0, 4));
    zone.removeFromTop (20);

    boutonUnGeste.setBounds (zone.removeFromTop (64).removeFromLeft (420));
    zone.removeFromTop (20);

    auto cases = zone.removeFromTop (36);

    for (auto* c : { &caseComplet, &caseSansGuide, &caseAvecClic, &casePistes })
        c->setBounds (cases.removeFromLeft (220));

    zone.removeFromTop (10);
    boutonSelection.setBounds (zone.removeFromTop (44).removeFromLeft (280).reduced (0, 2));
    zone.removeFromTop (24);

    barre.setBounds (zone.removeFromTop (26).removeFromLeft (juce::jmin (zone.getWidth(), 880)));
    zone.removeFromTop (8);
    etat.setBounds (zone.removeFromTop (28));

    auto prise = zone.removeFromBottom (44);
    etiquettePrise.setBounds (prise.removeFromLeft (110));
    choixPrise.setBounds (prise.removeFromLeft (420).reduced (0, 4));
    prise.removeFromLeft (16);
    boutonPrise.setBounds (prise.removeFromLeft (200).reduced (0, 2));
    prise.removeFromLeft (16);
    boutonFinder.setBounds (prise.removeFromLeft (230).reduced (0, 2));
    zone.removeFromBottom (16);

    resultat.setBounds (zone);
}

} // namespace groovebox
