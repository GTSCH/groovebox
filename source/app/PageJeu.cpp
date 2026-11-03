#include "PageJeu.h"
#include "Style.h"

namespace groovebox
{

namespace
{
    constexpr int idAutre = 1000;

    void styliserLibelle (juce::Label& l, const juce::String& texte, float taille = 18.0f)
    {
        l.setText (texte, juce::dontSendNotification);
        l.setFont (juce::FontOptions (taille));
        l.setColour (juce::Label::textColourId, couleurTexte);
    }

    void styliserCurseur (juce::Slider& s)
    {
        s.setSliderStyle (juce::Slider::LinearHorizontal);
        s.setTextBoxStyle (juce::Slider::TextBoxRight, false, 110, 28);
        s.setColour (juce::Slider::trackColourId, couleurActif);
        s.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        s.setWantsKeyboardFocus (false);
    }
}

PageJeu::PageJeu (Moteur& m)  : moteur (m)
{
    boutonLecture.setColour (juce::TextButton::buttonOnColourId, couleurActif);
    boutonLecture.onClick = [this] { lectureOuArret(); };
    addAndMakeVisible (boutonLecture);

    position.setFont (juce::FontOptions (46.0f, juce::Font::bold));
    position.setColour (juce::Label::textColourId, couleurTexte);
    position.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (position);

    aideMesure.setFont (juce::FontOptions (15.0f));
    aideMesure.setColour (juce::Label::textColourId, couleurDiscret);
    addAndMakeVisible (aideMesure);

    styliserLibelle (libelleTempo, "Tempo");
    styliserLibelle (libelleMesure, "Mesure");
    styliserLibelle (libelleGroupement, "Groupement");
    styliserLibelle (libelleClic, "Volume du clic");
    styliserLibelle (libelleCasque, "Musique dans le casque");

    for (auto* l : { &libelleTempo, &libelleMesure, &libelleGroupement, &libelleClic, &libelleCasque })
        addAndMakeVisible (*l);

    // Tempo : curseur, − et +, Tap Tempo
    styliserCurseur (tempo);
    tempo.setRange (Morceau::tempoMin, Morceau::tempoMax, 1.0);
    tempo.setTextValueSuffix (" BPM");
    tempo.onValueChange = [this] { tempoEnAttente = tempo.getValue(); };
    addAndMakeVisible (tempo);

    moinsTempo.onClick = [this] { tempoEnAttente = moteur.getMorceau().tempo - 1.0; };
    plusTempo.onClick = [this] { tempoEnAttente = moteur.getMorceau().tempo + 1.0; };
    boutonTap.onClick = [this] { taperLeTempo(); };

    // Mesure et groupement
    choixMesure.onChange = [this]
    {
        const auto id = choixMesure.getSelectedId();

        if (id == idAutre)
            choisirAutreMesure();
        else if (id > 0 && id <= (int) Mesure::mesuresProposees().size())
            moteur.modifier ([id] (Morceau& m) { m.mesure = Mesure::mesuresProposees()[(size_t) id - 1]; });
    };

    choixGroupement.onChange = [this]
    {
        const auto id = choixGroupement.getSelectedId();
        const auto& mesure = moteur.getMorceau().mesure;
        const auto proposes = Mesure::groupementsProposes (mesure.numerateur, mesure.denominateur);

        if (id == idAutre)
            choisirAutreGroupement();
        else if (id > 0 && id <= (int) proposes.size())
            moteur.modifier ([g = proposes[(size_t) id - 1]] (Morceau& m) { m.mesure.groupement = g; });
    };

    for (auto* c : { &choixMesure, &choixGroupement, &choixDecompte })
        addAndMakeVisible (*c);

    choixDecompte.addItem ("Pas de décompte"_fr, 1);
    choixDecompte.addItem ("Décompte d'une mesure"_fr, 2);
    choixDecompte.addItem ("Décompte de deux mesures"_fr, 3);
    choixDecompte.onChange = [this] { moteur.modifier ([n = choixDecompte.getSelectedId() - 1] (Morceau& m) { m.clic.decompte = n; }); };

    // Clic
    clicActif.onClick = [this] { moteur.modifier ([v = clicActif.getToggleState()] (Morceau& m) { m.clic.actif = v; }); };
    subdivisions.onClick = [this] { moteur.modifier ([v = subdivisions.getToggleState()] (Morceau& m) { m.clic.subdivisions = v; }); };
    unClicParGroupe.onClick = [this] { moteur.modifier ([v = unClicParGroupe.getToggleState()] (Morceau& m) { m.clic.unClicParGroupe = v; }); };
    ecouteSurMac.onClick = [this] { moteur.modifier ([v = ecouteSurMac.getToggleState()] (Morceau& m) { m.clic.ecouteSurMac = v; }); };

    for (auto* b : { &clicActif, &subdivisions, &unClicParGroupe, &ecouteSurMac })
    {
        b->setColour (juce::ToggleButton::textColourId, couleurTexte);
        b->setColour (juce::ToggleButton::tickColourId, couleurActif);
        b->setWantsKeyboardFocus (false);
        addAndMakeVisible (*b);
    }

    styliserCurseur (volumeClic);
    volumeClic.setRange (0.0, 100.0, 1.0);
    volumeClic.setTextValueSuffix (" %");
    volumeClic.onValueChange = [this] { moteur.modifier ([v = (float) volumeClic.getValue() / 100.0f] (Morceau& m) { m.clic.volume = v; }); };
    addAndMakeVisible (volumeClic);

    styliserCurseur (musiqueDansCasque);
    musiqueDansCasque.setRange (-40.0, 0.0, 1.0);
    musiqueDansCasque.setTextValueSuffix (" dB");
    musiqueDansCasque.onValueChange = [this] { moteur.modifier ([v = (float) musiqueDansCasque.getValue()] (Morceau& m) { m.musiqueDansCasqueDb = v; }); };
    addAndMakeVisible (musiqueDansCasque);

    for (auto* b : { &boutonLecture, &moinsTempo, &plusTempo, &boutonTap })
    {
        b->setWantsKeyboardFocus (false);
        addAndMakeVisible (*b);
    }

    for (auto* c : { &choixMesure, &choixGroupement, &choixDecompte })
        c->setWantsKeyboardFocus (false);

    mettreAJour();
    startTimerHz (30);
}

PageJeu::~PageJeu() = default;

//==============================================================================
void PageJeu::lectureOuArret()
{
    if (moteur.enLecture())
        moteur.arreter();
    else
        moteur.lancer();

    mettreAJour();
}

void PageJeu::taperLeTempo()
{
    if (auto t = tapTempo.taper (juce::Time::getMillisecondCounterHiRes() / 1000.0))
        tempoEnAttente = std::round (*t);
}

void PageJeu::remplirMesures()
{
    const auto& mesure = moteur.getMorceau().mesure;
    const auto proposees = Mesure::mesuresProposees();
    choixMesure.clear (juce::dontSendNotification);
    int choisi = 0;

    for (size_t i = 0; i < proposees.size(); ++i)
    {
        choixMesure.addItem (proposees[i].nom(), (int) i + 1);

        if (proposees[i].numerateur == mesure.numerateur && proposees[i].denominateur == mesure.denominateur)
            choisi = (int) i + 1;
    }

    if (choisi == 0)
    {
        // Mesure saisie à la main : elle apparaît dans la liste
        choixMesure.addItem (mesure.nom(), (int) proposees.size() + 1);
        choisi = (int) proposees.size() + 1;
    }

    choixMesure.addItem ("Autre…"_fr, idAutre);
    choixMesure.setSelectedId (choisi, juce::dontSendNotification);
}

void PageJeu::remplirGroupements()
{
    const auto& mesure = moteur.getMorceau().mesure;
    const auto proposes = Mesure::groupementsProposes (mesure.numerateur, mesure.denominateur);
    choixGroupement.clear (juce::dontSendNotification);
    int choisi = 0;

    for (size_t i = 0; i < proposes.size(); ++i)
    {
        choixGroupement.addItem (Mesure::nomGroupement (proposes[i]), (int) i + 1);

        if (proposes[i] == mesure.groupement)
            choisi = (int) i + 1;
    }

    if (choisi == 0)
    {
        choixGroupement.addItem (mesure.nomGroupement(), (int) proposes.size() + 1);
        choisi = (int) proposes.size() + 1;
    }

    choixGroupement.addItem ("Autre…"_fr, idAutre);
    choixGroupement.setSelectedId (choisi, juce::dontSendNotification);
}

void PageJeu::choisirAutreMesure()
{
    auto fenetre = std::make_shared<juce::AlertWindow> ("Autre mesure",
                                                        "Haut et bas de la mesure, puis le groupement des temps forts (par exemple 2+2+3).",
                                                        juce::MessageBoxIconType::NoIcon);
    const auto& actuelle = moteur.getMorceau().mesure;
    fenetre->addTextEditor ("haut", juce::String (actuelle.numerateur), "Haut (nombre de temps)");
    fenetre->addTextEditor ("bas", juce::String (actuelle.denominateur), "Bas (2, 4, 8 ou 16)");
    fenetre->addTextEditor ("groupement", actuelle.nomGroupement(), "Groupement (vide : un seul groupe)");
    fenetre->addButton ("Choisir", 1, juce::KeyPress (juce::KeyPress::returnKey));
    fenetre->addButton ("Annuler", 0, juce::KeyPress (juce::KeyPress::escapeKey));

    fenetre->enterModalState (true, juce::ModalCallbackFunction::create ([this, fenetre] (int resultat)
    {
        if (resultat == 1)
        {
            Mesure m;
            m.numerateur = fenetre->getTextEditorContents ("haut").getIntValue();
            m.denominateur = fenetre->getTextEditorContents ("bas").getIntValue();
            const auto texte = fenetre->getTextEditorContents ("groupement").trim();
            m.groupement = texte.isEmpty() ? std::vector<int> { m.numerateur }
                                           : Mesure::lireGroupement (texte).value_or (std::vector<int> {});

            if (m.estValide())
                moteur.modifier ([m] (Morceau& morceau) { morceau.mesure = m; });
            else
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Mesure impossible",
                                                        "Le haut va de 1 à 32, le bas vaut 2, 4, 8 ou 16, et le groupement doit faire le total du haut."_fr);
        }

        mettreAJour();
    }));
}

void PageJeu::choisirAutreGroupement()
{
    auto fenetre = std::make_shared<juce::AlertWindow> ("Autre groupement",
                                                        "Groupement des temps forts, par exemple 2+2+3. Le total doit faire " + juce::String (moteur.getMorceau().mesure.numerateur) + ".",
                                                        juce::MessageBoxIconType::NoIcon);
    fenetre->addTextEditor ("groupement", moteur.getMorceau().mesure.nomGroupement(), "Groupement");
    fenetre->addButton ("Choisir", 1, juce::KeyPress (juce::KeyPress::returnKey));
    fenetre->addButton ("Annuler", 0, juce::KeyPress (juce::KeyPress::escapeKey));

    fenetre->enterModalState (true, juce::ModalCallbackFunction::create ([this, fenetre] (int resultat)
    {
        if (resultat == 1)
        {
            auto m = moteur.getMorceau().mesure;
            m.groupement = Mesure::lireGroupement (fenetre->getTextEditorContents ("groupement")).value_or (std::vector<int> {});

            if (m.estValide())
                moteur.modifier ([m] (Morceau& morceau) { morceau.mesure = m; });
            else
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Groupement impossible",
                                                        "Le total du groupement doit faire " + juce::String (m.numerateur) + ".");
        }

        mettreAJour();
    }));
}

//==============================================================================
void PageJeu::mettreAJour()
{
    const auto& morceau = moteur.getMorceau();
    const bool lecture = moteur.enLecture();

    boutonLecture.setButtonText (lecture ? "Arrêt"_fr : juce::String ("Lecture"));
    boutonLecture.setToggleState (lecture, juce::dontSendNotification);

    if (! tempoEnAttente)
        tempo.setValue (morceau.tempo, juce::dontSendNotification);

    remplirMesures();
    remplirGroupements();
    choixMesure.setEnabled (! lecture);
    choixGroupement.setEnabled (! lecture);
    aideMesure.setText (lecture ? "Arrête la lecture pour changer de mesure."_fr
                                : "Tempo à la noire. Barre d'espace : lecture et arrêt. Touche T : Tap Tempo."_fr,
                        juce::dontSendNotification);

    choixDecompte.setSelectedId (morceau.clic.decompte + 1, juce::dontSendNotification);
    clicActif.setToggleState (morceau.clic.actif, juce::dontSendNotification);
    subdivisions.setToggleState (morceau.clic.subdivisions, juce::dontSendNotification);
    unClicParGroupe.setToggleState (morceau.clic.unClicParGroupe, juce::dontSendNotification);
    volumeClic.setValue (morceau.clic.volume * 100.0, juce::dontSendNotification);
    musiqueDansCasque.setValue (morceau.musiqueDansCasqueDb, juce::dontSendNotification);

    // L'écoute sur le Mac n'existe que sans sorties 3-4
    ecouteSurMac.setVisible (! moteur.sortieCasqueDisponible());
    ecouteSurMac.setToggleState (morceau.clic.ecouteSurMac, juce::dontSendNotification);
}

void PageJeu::timerCallback()
{
    if (tempoEnAttente)
    {
        // Au plus 30 changements de tempo par seconde, même en tournant vite
        const auto t = juce::jlimit (Morceau::tempoMin, Morceau::tempoMax, *tempoEnAttente);
        tempoEnAttente.reset();

        if (t != moteur.getMorceau().tempo)
            moteur.modifier ([t] (Morceau& m) { m.tempo = t; });

        tempo.setValue (moteur.getMorceau().tempo, juce::dontSendNotification);
    }

    const auto p = moteur.getPosition();

    if (p.enLecture != positionAffichee.enLecture || ecouteSurMac.isVisible() == moteur.sortieCasqueDisponible())
        mettreAJour();

    juce::String texte;

    if (! p.enLecture)
        texte = "À l'arrêt"_fr;
    else if (p.mesure < 1)
        texte = "Décompte : "_fr + juce::String (1 - p.mesure);
    else
        texte = "Mesure " + juce::String (p.mesure) + ", temps " + juce::String (p.temps + 1);

    position.setText (texte, juce::dontSendNotification);

    if (p.enLecture != positionAffichee.enLecture || p.temps != positionAffichee.temps || p.mesure != positionAffichee.mesure)
        repaint (zoneDesTemps);

    positionAffichee = p;
}

void PageJeu::paint (juce::Graphics& g)
{
    g.fillAll (couleurFond);

    // Une case par temps, un petit écart entre les groupes ; le temps en cours s'allume
    const auto& mesure = moteur.getMorceau().mesure;
    const auto types = mesure.typesDesTemps();
    const int ecartDeGroupe = 14, ecart = 6;
    const int nombreDeGroupes = (int) mesure.groupement.size();
    const auto largeur = (float) (zoneDesTemps.getWidth() - (nombreDeGroupes - 1) * ecartDeGroupe - (mesure.numerateur - 1) * ecart) / (float) mesure.numerateur;
    float x = (float) zoneDesTemps.getX();

    for (int i = 0; i < mesure.numerateur; ++i)
    {
        if (i > 0)
            x += types[(size_t) i] != TypeDeTemps::autre ? (float) (ecart + ecartDeGroupe) : (float) ecart;

        const auto rectangle = juce::Rectangle<float> (x, (float) zoneDesTemps.getY(), largeur, (float) zoneDesTemps.getHeight());
        const bool enCours = positionAffichee.enLecture && positionAffichee.temps == i;
        const auto couleur = types[(size_t) i] == TypeDeTemps::premier ? couleurPremier
                           : types[(size_t) i] == TypeDeTemps::groupe  ? couleurGroupe
                                                                        : couleurTexte;

        g.setColour (enCours ? couleur : couleurPanneau);
        g.fillRoundedRectangle (rectangle, 8.0f);
        g.setColour (couleur.withAlpha (enCours ? 1.0f : 0.6f));
        g.drawRoundedRectangle (rectangle.reduced (1.0f), 8.0f, 2.0f);

        g.setColour (enCours ? couleurFond : couleurDiscret);
        g.setFont (juce::FontOptions (20.0f, juce::Font::bold));
        g.drawText (juce::String (i + 1), rectangle, juce::Justification::centred);

        x += largeur;
    }
}

void PageJeu::resized()
{
    auto zone = getLocalBounds().reduced (24, 18);

    auto haut = zone.removeFromTop (70);
    boutonLecture.setBounds (haut.removeFromLeft (170).reduced (0, 4));
    position.setBounds (haut.withTrimmedLeft (20));

    zone.removeFromTop (12);
    zoneDesTemps = zone.removeFromTop (64);
    zone.removeFromTop (6);
    aideMesure.setBounds (zone.removeFromTop (24));
    zone.removeFromTop (12);

    auto ligne = [&zone] (int hauteur = 38)
    {
        auto l = zone.removeFromTop (hauteur);
        zone.removeFromTop (10);
        return l;
    };

    const int colonne = 200;

    {
        auto l = ligne();
        libelleTempo.setBounds (l.removeFromLeft (colonne));
        boutonTap.setBounds (l.removeFromRight (130));
        l.removeFromRight (10);
        plusTempo.setBounds (l.removeFromRight (44));
        l.removeFromRight (6);
        moinsTempo.setBounds (l.removeFromLeft (44));
        l.removeFromLeft (6);
        tempo.setBounds (l);
    }
    {
        auto l = ligne();
        libelleMesure.setBounds (l.removeFromLeft (colonne));
        choixMesure.setBounds (l.removeFromLeft (150));
        l.removeFromLeft (24);
        libelleGroupement.setBounds (l.removeFromLeft (120));
        choixGroupement.setBounds (l.removeFromLeft (170));
    }

    zone.removeFromTop (8);

    {
        auto l = ligne();
        clicActif.setBounds (l.removeFromLeft (colonne));
        choixDecompte.setBounds (l.removeFromLeft (260));
    }
    {
        auto l = ligne (32);
        subdivisions.setBounds (l.removeFromLeft (colonne));
        unClicParGroupe.setBounds (l.removeFromLeft (260));
    }
    {
        auto l = ligne();
        libelleClic.setBounds (l.removeFromLeft (colonne));
        volumeClic.setBounds (l);
    }
    {
        auto l = ligne();
        libelleCasque.setBounds (l.removeFromLeft (colonne));
        musiqueDansCasque.setBounds (l);
    }

    ecouteSurMac.setBounds (ligne (32));
}

} // namespace groovebox
