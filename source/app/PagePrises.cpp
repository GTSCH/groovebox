#include "PagePrises.h"
#include "Style.h"

namespace groovebox
{

namespace
{
    juce::String formaterDuree (double secondes)
    {
        const int s = juce::roundToInt (secondes);
        return juce::String (s / 60) + " min " + juce::String (s % 60).paddedLeft ('0', 2) + " s";
    }
}

//==============================================================================
LecteurDePrise::LecteurDePrise (juce::AudioDeviceManager& c)  : cartes (c)
{
    formats.registerBasicFormats();
    lecture.startThread();

    for (auto& t : transports)
        melange.addInputSource (&t, false);

    cartes.addAudioCallback (this);
}

LecteurDePrise::~LecteurDePrise()
{
    cartes.removeAudioCallback (this);
    arreter();
    melange.removeAllInputs();
    lecture.stopThread (2000);
}

bool LecteurDePrise::ecouter (const juce::File& dossier)
{
    arreter();
    const juce::File fichiers[2] = { dossier.getChildFile ("sortie.wav"), dossier.getChildFile ("micros.wav") };
    bool ouvert = false;

    for (size_t k = 0; k < 2; ++k)
    {
        auto* lecteurDuFichier = fichiers[k].existsAsFile() ? formats.createReaderFor (fichiers[k]) : nullptr;

        if (lecteurDuFichier == nullptr)
            continue;

        const auto frequenceDuFichier = lecteurDuFichier->sampleRate;
        auto source = std::make_unique<juce::AudioFormatReaderSource> (lecteurDuFichier, true);
        transports[k].setSource (source.get(), 32768, &lecture, frequenceDuFichier, 2);

        const juce::ScopedLock l (verrou);
        sources[k] = std::move (source);
        ouvert = true;
    }

    for (size_t k = 0; k < 2; ++k)
        if (sources[k] != nullptr)
            transports[k].start();

    return ouvert;
}

void LecteurDePrise::arreter()
{
    for (auto& t : transports)
    {
        t.stop();
        t.setSource (nullptr);
    }

    const juce::ScopedLock l (verrou);

    for (auto& s : sources)
        s.reset();
}

bool LecteurDePrise::enCours() const
{
    return transports[0].isPlaying() || transports[1].isPlaying();
}

double LecteurDePrise::position() const     { return transports[0].getCurrentPosition(); }
double LecteurDePrise::duree() const        { return transports[0].getLengthInSeconds(); }

void LecteurDePrise::audioDeviceAboutToStart (juce::AudioIODevice* carte)
{
    frequence = carte->getCurrentSampleRate();
    taille = carte->getCurrentBufferSizeSamples();
    tampon.setSize (2, juce::jmax (taille, 4096));
    melange.prepareToPlay (taille, frequence);
}

void LecteurDePrise::audioDeviceStopped()
{
    melange.releaseResources();
}

void LecteurDePrise::audioDeviceIOCallbackWithContext (const float* const*, int, float* const* sorties, int nombreDeSorties,
                                                        int n, const juce::AudioIODeviceCallbackContext&)
{
    for (int c = 0; c < nombreDeSorties; ++c)
        if (sorties[c] != nullptr)
            juce::FloatVectorOperations::clear (sorties[c], n);

    const juce::ScopedTryLock l (verrou);

    if (! l.isLocked() || (sources[0] == nullptr && sources[1] == nullptr) || n > tampon.getNumSamples())
        return;

    // La prise sort sur 1-2 (la sono), jamais dans le casque
    juce::AudioSourceChannelInfo info (&tampon, 0, n);
    melange.getNextAudioBlock (info);

    for (int c = 0; c < juce::jmin (2, nombreDeSorties); ++c)
        if (sorties[c] != nullptr)
            juce::FloatVectorOperations::copy (sorties[c], tampon.getReadPointer (c), n);
}

//==============================================================================
PagePrises::PagePrises (te::Engine& engine, Moteur& m)  : moteur (m), lecteur (engine.getDeviceManager().deviceManager)
{
    titre.setFont (juce::FontOptions (30.0f, juce::Font::bold));
    titre.setColour (juce::Label::textColourId, couleurTexte);
    addAndMakeVisible (titre);

    for (auto* l : { &infos, &etatDeLaCarte })
    {
        l->setFont (juce::FontOptions (17.0f));
        l->setColour (juce::Label::textColourId, couleurDiscret);
        addAndMakeVisible (*l);
    }

    liste.setRowHeight (54);
    liste.setColour (juce::ListBox::backgroundColourId, couleurFond);
    liste.setColour (juce::ListBox::outlineColourId, couleurPanneau);
    liste.setOutlineThickness (1);
    addAndMakeVisible (liste);

    boutonRecord.onClick = [this]
    {
        if (moteur.priseEnCours())
            moteur.arreterPrise();
        else
            moteur.demarrerPrise();

        relire();
    };

    boutonEcouter.onClick = [this]
    {
        if (lecteur.enCours())
        {
            lecteur.arreter();
            dossierEnEcoute = {};
        }
        else if (auto* l = ligneChoisie())
        {
            // On n'écoute pas une prise par-dessus la musique : la lecture s'arrête
            if (moteur.enLecture())
                moteur.arreter();

            if (lecteur.ecouter (l->dossier))
                dossierEnEcoute = l->dossier.getFullPathName();
        }

        mettreAJourBoutons();
    };

    boutonRenommer.onClick = [this] { renommer(); };
    boutonFinder.onClick = [this] { if (auto* l = ligneChoisie()) l->dossier.revealToUser(); };
    boutonCorbeille.onClick = [this] { jeter(); };

    for (auto* b : { &boutonRecord, &boutonEcouter, &boutonRenommer, &boutonFinder, &boutonCorbeille })
    {
        b->setWantsKeyboardFocus (false);
        addAndMakeVisible (*b);
    }

    moteur.surPriseTerminee = [this] (const juce::File&) { relire(); };
    relire();
    startTimerHz (4);
}

PagePrises::~PagePrises()
{
    moteur.surPriseTerminee = nullptr;
}

void PagePrises::relire()
{
    const auto dossier = moteur.dossierDesPrises ? moteur.dossierDesPrises() : juce::File();
    const auto choisie = ligneChoisie() != nullptr ? ligneChoisie()->dossier : juce::File();
    dossierAffiche = dossier;
    lignes.clear();

    if (dossier.isDirectory())
        for (auto& d : dossier.findChildFiles (juce::File::findDirectories, false))
            if (auto p = Prise::charger (d.getChildFile ("prise.json")))
                lignes.push_back ({ d, p->nom, p->date.formatted ("%d.%m.%Y %H:%M"), p->dureeEnSecondes(), (int) p->evenements.size(), p->avecMicros,
                                    d.getChildFile ("prise.json").getLastModificationTime() });

    // La plus récente en haut
    std::sort (lignes.begin(), lignes.end(), [] (auto& a, auto& b) { return a.dossier.getFileName() > b.dossier.getFileName(); });
    liste.updateContent();

    for (int i = 0; i < (int) lignes.size(); ++i)
        if (lignes[(size_t) i].dossier == choisie)
            liste.selectRow (i);

    if (liste.getSelectedRow() < 0 && ! lignes.empty())
        liste.selectRow (0);

    liste.repaint();
    mettreAJourBoutons();
}

const PagePrises::Ligne* PagePrises::ligneChoisie() const
{
    const int i = liste.getSelectedRow();
    return i >= 0 && i < (int) lignes.size() ? &lignes[(size_t) i] : nullptr;
}

void PagePrises::mettreAJourBoutons()
{
    const bool prise = moteur.priseEnCours();
    const bool choisie = ligneChoisie() != nullptr;
    boutonRecord.setButtonText (prise ? "Arrêter la prise"_fr : juce::String ("Enregistrer une prise"));
    boutonRecord.setColour (juce::TextButton::buttonColourId, prise ? juce::Colour (0xffe5484d) : couleurPanneau);
    boutonEcouter.setButtonText (lecteur.enCours() ? juce::String ("Arrêter l'écoute"_fr) : juce::String ("Écouter"_fr));
    boutonEcouter.setEnabled (! prise && (choisie || lecteur.enCours()));

    for (auto* b : { &boutonRenommer, &boutonFinder })
        b->setEnabled (choisie);

    boutonCorbeille.setEnabled (choisie && ! prise && ! lecteur.enCours());
}

void PagePrises::renommer()
{
    auto* l = ligneChoisie();

    if (l == nullptr)
        return;

    auto fenetre = std::make_shared<juce::AlertWindow> ("Renommer la prise", "Nouveau nom de la prise :", juce::MessageBoxIconType::NoIcon);
    fenetre->addTextEditor ("nom", l->nom);
    fenetre->addButton ("Renommer", 1, juce::KeyPress (juce::KeyPress::returnKey));
    fenetre->addButton ("Annuler", 0, juce::KeyPress (juce::KeyPress::escapeKey));

    fenetre->enterModalState (true, juce::ModalCallbackFunction::create ([this, fenetre, fichier = l->dossier.getChildFile ("prise.json")] (int resultat)
    {
        const auto nom = fenetre->getTextEditorContents ("nom").trim();

        if (resultat != 1 || nom.isEmpty())
            return;

        if (auto p = Prise::charger (fichier))
        {
            p->nom = nom;
            p->enregistrer (fichier);
        }

        relire();
    }));
}

void PagePrises::jeter()
{
    auto* l = ligneChoisie();

    if (l == nullptr)
        return;

    auto options = juce::MessageBoxOptions().withIconType (juce::MessageBoxIconType::QuestionIcon)
                                            .withTitle ("Mettre à la corbeille"_fr)
                                            .withMessage ("« "_fr + l->nom + " » part dans la corbeille du Mac, d'où on peut encore la récupérer."_fr)
                                            .withButton ("Mettre à la corbeille"_fr)
                                            .withButton ("Annuler");

    juce::AlertWindow::showAsync (options, [this, dossier = l->dossier] (int choix)
    {
        if (choix == 1 && ! dossier.moveToTrash())
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Corbeille", "La prise n'a pas pu être déplacée."_fr);

        relire();
    });
}

void PagePrises::timerCallback()
{
    const auto dossier = moteur.dossierDesPrises ? moteur.dossierDesPrises() : juce::File();

    if (dossier != dossierAffiche)
        relire();

    titre.setText ("Prises de « "_fr + moteur.getMorceau().nom + " »"_fr, juce::dontSendNotification);

    if (moteur.priseEnCours())
        infos.setText (moteur.priseCourante()->nom + " en cours : "_fr + formaterDuree (moteur.dureeDeLaPrise())
                           + ", " + juce::String ((int) moteur.priseCourante()->evenements.size()) + " gestes. Record au Push, ou le bouton ci-dessous, pour l'arrêter."_fr,
                       juce::dontSendNotification);
    else if (lecteur.enCours())
        infos.setText ("Écoute : "_fr + formaterDuree (lecteur.position()) + " sur " + formaterDuree (lecteur.duree()), juce::dontSendNotification);
    else
        infos.setText ("Record au Push (ou le bouton ci-dessous) enregistre tout ce qui se joue, et le son.", juce::dontSendNotification);

    const int entrees = moteur.getEntreeDesMicros().canauxDisponibles();
    etatDeLaCarte.setText (entrees > 0 ? "Micros : entrées 1-2 de la carte son, enregistrées avec chaque prise."_fr
                                       : "Micros : la carte son n'a pas d'entrée (la Scarlett en aura) ; seul le son de la sortie est enregistré."_fr,
                           juce::dontSendNotification);

    if (dossierEnEcoute.isNotEmpty() && ! lecteur.enCours())
    {
        lecteur.arreter();
        dossierEnEcoute = {};
    }

    mettreAJourBoutons();
    liste.repaint();
}

void PagePrises::paintListBoxItem (int ligne, juce::Graphics& g, int largeur, int hauteur, bool choisie)
{
    if (ligne < 0 || ligne >= (int) lignes.size())
        return;

    const auto& l = lignes[(size_t) ligne];
    const bool ecoutee = l.dossier.getFullPathName() == dossierEnEcoute;
    g.fillAll (choisie ? couleurPanneau : couleurFond);

    if (ecoutee)
    {
        g.setColour (couleurActif);
        g.fillRect (0, 0, 6, hauteur);
    }

    g.setColour (couleurTexte);
    g.setFont (juce::FontOptions (20.0f, juce::Font::bold));
    g.drawText (l.nom, 18, 4, largeur / 2, 26, juce::Justification::centredLeft);

    g.setColour (couleurDiscret);
    g.setFont (juce::FontOptions (16.0f));
    g.drawText (l.date + ", " + formaterDuree (l.duree) + ", " + juce::String (l.gestes) + " gestes" + (l.micros ? juce::String (", avec les micros") : juce::String()),
                18, 28, largeur - 36, 22, juce::Justification::centredLeft);
}

void PagePrises::paint (juce::Graphics& g)
{
    g.fillAll (couleurFond);
}

void PagePrises::resized()
{
    auto zone = getLocalBounds().reduced (20);
    titre.setBounds (zone.removeFromTop (44));
    infos.setBounds (zone.removeFromTop (28));
    etatDeLaCarte.setBounds (zone.removeFromTop (28));
    zone.removeFromTop (10);

    auto boutons = zone.removeFromBottom (46);
    boutonRecord.setBounds (boutons.removeFromLeft (230).reduced (0, 2));
    boutons.removeFromLeft (16);

    for (auto* b : { &boutonEcouter, &boutonRenommer, &boutonFinder, &boutonCorbeille })
    {
        b->setBounds (boutons.removeFromLeft (170).reduced (4, 2));
    }

    zone.removeFromBottom (12);
    liste.setBounds (zone);
}

} // namespace groovebox
