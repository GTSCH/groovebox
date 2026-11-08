#include "PageRetouche.h"
#include "Style.h"
#include "push/Palette.h"

namespace groovebox
{

namespace
{
    constexpr int largeurDesNoms = 120;

    juce::String formaterMesures (double mesures)
    {
        const bool entier = std::abs (mesures - std::round (mesures)) < 0.01;
        const auto texte = entier ? juce::String (juce::roundToInt (mesures)) : juce::String (mesures, 1).replaceCharacter ('.', ',');
        return texte + (entier && juce::roundToInt (mesures) == 1 ? " mesure" : " mesures");
    }

    juce::String decrireEntree (const Section& s)
    {
        if (s.scene < 0)
            return "rien n'est lancé"_fr;

        if (! s.entree)
            return "suite, la scène continue"_fr;

        const auto e = *s.entree;
        auto texte = nomDeLEnchainement (e.type).toLowerCase();

        if (e.type != TypeEnchainement::coupe)
            texte << ", " << formaterMesures (e.mesures);

        return texte + (s.aLaMain ? juce::String (", à la main"_fr) : juce::String());
    }
}

PageRetouche::PageRetouche (te::Engine& engine, Moteur& m)  : moteur (m), lecteur (engine.getDeviceManager().deviceManager)
{
    titre.setFont (juce::FontOptions (30.0f, juce::Font::bold));
    titre.setColour (juce::Label::textColourId, couleurTexte);
    titre.setText ("Retouche", juce::dontSendNotification);
    addAndMakeVisible (titre);

    infos.setFont (juce::FontOptions (17.0f));
    infos.setColour (juce::Label::textColourId, couleurDiscret);
    addAndMakeVisible (infos);

    for (auto* l : { &etiquetteEntree, &etiquetteCourbe })
    {
        l->setFont (juce::FontOptions (17.0f));
        l->setColour (juce::Label::textColourId, couleurTexte);
        l->setJustificationType (juce::Justification::centredRight);
        addAndMakeVisible (*l);
    }

    choixDeLaSource.setTextWhenNothingSelected ("Choisir une prise ou un déroulé"_fr);
    choixDeLaSource.onChange = [this] { choisirSource(); };

    choixDeLEntree.addItem ("Suite", 1);

    for (int t = 0; t < nombreDeTypesDEnchainement; ++t)
        choixDeLEntree.addItem (nomDeLEnchainement ((TypeEnchainement) t), t + 2);

    choixDeLEntree.onChange = [this] { changerEntree(); };

    for (auto d : Enchainement::dureesPossibles)
        choixDeLaDuree.addItem (formaterMesures (d), d);

    choixDeLaDuree.onChange = [this] { changerEntree(); };
    choixDeLaCourbe.onChange = [this] { repaint(); };

    for (auto* c : { &choixDeLaSource, &choixDeLEntree, &choixDeLaDuree, &choixDeLaCourbe })
    {
        c->setWantsKeyboardFocus (false);
        addAndMakeVisible (*c);
    }

    boutonEcouter.onClick = [this] { ecouter(); };
    boutonCouper.onClick = [this]
    {
        modifier ([this] (Deroule& d) { return retouche::couper (d, sectionChoisie, curseur - d.debutDeLaSection (sectionChoisie)); });
    };
    boutonSupprimer.onClick = [this]
    {
        modifier ([this] (Deroule& d) { return retouche::supprimer (d, sectionChoisie); });
        sectionChoisie = juce::jmin (sectionChoisie, (int) deroule->sections.size() - 1);
        miseAJour();
    };
    boutonAvant.onClick = [this] { modifier ([this] (Deroule& d) { const bool ok = retouche::deplacer (d, sectionChoisie, -1); if (ok) --sectionChoisie; return ok; }); };
    boutonApres.onClick = [this] { modifier ([this] (Deroule& d) { const bool ok = retouche::deplacer (d, sectionChoisie, 1); if (ok) ++sectionChoisie; return ok; }); };
    boutonRepeter.onClick = [this] { modifier ([this] (Deroule& d) { return retouche::repeter (d, sectionChoisie); }); };
    boutonRenommer.onClick = [this] { renommer(); };
    boutonLisser.onClick = [this]
    {
        const int i = choixDeLaCourbe.getSelectedId() - 1;

        if (auto* s = section(); s != nullptr && i >= 0)
        {
            const auto liste = retouche::courbes (*s, ligneChoisie);

            if (i < liste.size())
                modifier ([this, c = liste[i]] (Deroule& d) { retouche::lisser (d.sections[(size_t) sectionChoisie], c); return true; });
        }
    };
    boutonEffacer.onClick = [this]
    {
        const int i = choixDeLaCourbe.getSelectedId() - 1;

        if (auto* s = section(); s != nullptr && i >= 0)
        {
            const auto liste = retouche::courbes (*s, ligneChoisie);

            if (i < liste.size())
                modifier ([this, c = liste[i]] (Deroule& d) { retouche::effacer (d.sections[(size_t) sectionChoisie], c); return true; });
        }
    };
    boutonAnnuler.onClick = [this]
    {
        if (historique.empty())
            return;

        deroule = historique.back();
        historique.pop_back();
        modifie = true;
        sectionChoisie = juce::jmin (sectionChoisie, (int) deroule->sections.size() - 1);
        calculerBornes();
        miseAJour();
    };
    boutonCreer.onClick = [this] { creer(); };
    boutonOuverte.onClick = [this] { modifier ([this] (Deroule& d) { auto& s = d.sections[(size_t) sectionChoisie]; s.ouverte = ! s.ouverte; return true; }); };
    boutonRejouer.onClick = [this] { rejouer(); };
    choixPourPlay.setColour (juce::ToggleButton::textColourId, couleurTexte);
    choixPourPlay.setWantsKeyboardFocus (false);
    choixPourPlay.onClick = [this] { synchroniserPlay(); };
    addAndMakeVisible (choixPourPlay);

    for (auto* b : { &boutonEcouter, &boutonCouper, &boutonSupprimer, &boutonAvant, &boutonApres, &boutonRepeter, &boutonRenommer,
                     &boutonLisser, &boutonEffacer, &boutonAnnuler, &boutonCreer, &boutonOuverte, &boutonRejouer })
    {
        b->setWantsKeyboardFocus (false);
        addAndMakeVisible (*b);
    }

    relireLesSources();
    miseAJour();
    startTimerHz (20);
}

juce::File PageRetouche::dossierDesPrises() const
{
    return moteur.dossierDesPrises ? moteur.dossierDesPrises() : juce::File();
}

juce::File PageRetouche::dossierDesDeroules() const
{
    const auto prises = dossierDesPrises();
    return prises == juce::File() ? juce::File() : prises.getParentDirectory().getChildFile ("deroules");
}

//==============================================================================
void PageRetouche::relireLesSources()
{
    std::vector<Source> trouvees;

    for (auto& f : dossierDesDeroules().findChildFiles (juce::File::findFiles, false, "*.json"))
        if (auto d = Deroule::charger (f))
            trouvees.push_back ({ "Déroulé : "_fr + d->nom, f, true });

    std::vector<std::pair<juce::String, juce::File>> prises;

    for (auto& d : dossierDesPrises().findChildFiles (juce::File::findDirectories, false))
        if (auto p = Prise::charger (d.getChildFile ("prise.json")))
            prises.push_back ({ "Prise : " + p->nom + " (" + p->date.formatted ("%d.%m.%Y %H:%M") + ")", d });

    std::sort (prises.begin(), prises.end(), [] (auto& a, auto& b) { return a.second.getFileName() > b.second.getFileName(); });

    for (auto& [nom, d] : prises)
        trouvees.push_back ({ nom, d, false });

    // On ne refait la liste que si elle a changé : sinon le menu se fermerait sous les doigts
    bool pareil = trouvees.size() == sources.size();

    for (size_t i = 0; pareil && i < trouvees.size(); ++i)
        pareil = trouvees[i].nom == sources[i].nom && trouvees[i].fichier == sources[i].fichier;

    if (pareil)
        return;

    const auto choisie = choixDeLaSource.getSelectedId() > 0 && choixDeLaSource.getSelectedId() <= (int) sources.size()
                           ? sources[(size_t) choixDeLaSource.getSelectedId() - 1].fichier : juce::File();
    sources = std::move (trouvees);
    choixDeLaSource.clear (juce::dontSendNotification);

    for (size_t i = 0; i < sources.size(); ++i)
    {
        choixDeLaSource.addItem (sources[i].nom, (int) i + 1);

        if (sources[i].fichier == choisie || (fichierDuDeroule != juce::File() && sources[i].fichier == fichierDuDeroule))
            choixDeLaSource.setSelectedId ((int) i + 1, juce::dontSendNotification);
    }
}

void PageRetouche::choisirSource()
{
    const int i = choixDeLaSource.getSelectedId() - 1;

    if (i < 0 || i >= (int) sources.size())
        return;

    const auto source = sources[(size_t) i];
    auto ouvrir = [this, source]
    {
        if (source.estUnDeroule)
            ouvrirDeroule (source.fichier);
        else
            ouvrirPrise (source.fichier);
    };

    if (! modifie)
        return ouvrir();

    auto options = juce::MessageBoxOptions().withIconType (juce::MessageBoxIconType::QuestionIcon)
                                            .withTitle ("Retouches non enregistrées"_fr)
                                            .withMessage ("Les retouches en cours ne sont pas enregistrées. Les abandonner ?"_fr)
                                            .withButton ("Abandonner")
                                            .withButton ("Garder");

    juce::AlertWindow::showAsync (options, [this, ouvrir] (int choix)
    {
        if (choix == 1)
            ouvrir();
        else
            relireLesSources();
    });
}

void PageRetouche::choisirUneLigneQuiBouge()
{
    // Si rien ne bouge sur la ligne choisie dans cette section, on va sur la première ligne où un réglage bouge
    auto* s = section();

    if (s == nullptr || ! retouche::courbes (*s, ligneChoisie).isEmpty())
        return;

    for (int ligne = -1; ligne < nombreDePistes; ++ligne)
        if (! retouche::courbes (*s, ligne).isEmpty())
        {
            ligneChoisie = ligne;
            return;
        }
}

void PageRetouche::montrerDansLeMenu (const juce::File& fichier)
{
    relireLesSources();

    for (size_t i = 0; i < sources.size(); ++i)
        if (sources[i].fichier == fichier)
            choixDeLaSource.setSelectedId ((int) i + 1, juce::dontSendNotification);
}

void PageRetouche::ouvrirPrise (const juce::File& dossier)
{
    if (auto p = Prise::charger (dossier.getChildFile ("prise.json")))
    {
        lecteur.arreter();
        deroule = retouche::decouper (*p, dossier.getFileName());
        fichierDuDeroule = juce::File();
        historique.clear();
        modifie = false;
        sectionChoisie = deroule->sections.empty() ? -1 : 0;
        curseur = -1.0;
        calculerBornes();
        choisirUneLigneQuiBouge();
        montrerDansLeMenu (dossier);
        miseAJour();
    }
}

void PageRetouche::ouvrirDeroule (const juce::File& fichier)
{
    if (auto d = Deroule::charger (fichier))
    {
        lecteur.arreter();
        deroule = std::move (d);
        fichierDuDeroule = fichier;
        historique.clear();
        modifie = false;
        sectionChoisie = deroule->sections.empty() ? -1 : 0;
        curseur = -1.0;
        calculerBornes();
        choisirUneLigneQuiBouge();
        montrerDansLeMenu (fichier);
        miseAJour();
    }
}

void PageRetouche::modifier (const std::function<bool (Deroule&)>& action)
{
    if (! deroule || sectionChoisie < 0)
        return;

    auto avant = *deroule;

    if (! action (*deroule))
        return;

    historique.push_back (std::move (avant));

    if (historique.size() > 200)
        historique.erase (historique.begin());

    modifie = true;
    curseur = -1.0;
    sectionChoisie = juce::jlimit (-1, (int) deroule->sections.size() - 1, sectionChoisie);
    calculerBornes();
    miseAJour();
}

Section* PageRetouche::section()
{
    return deroule && sectionChoisie >= 0 && sectionChoisie < (int) deroule->sections.size() ? &deroule->sections[(size_t) sectionChoisie] : nullptr;
}

void PageRetouche::calculerBornes()
{
    bornes.clear();

    if (! deroule)
        return;

    for (auto& s : deroule->sections)
        for (int ligne = -1; ligne < nombreDePistes; ++ligne)
            for (auto& c : retouche::courbes (s, ligne))
                for (auto& [t, v] : retouche::points (s, c))
                {
                    auto [b, inseree] = bornes.insert ({ c, { v, v } });

                    if (! inseree)
                        b->second = { juce::jmin (b->second.first, v), juce::jmax (b->second.second, v) };
                }
}

void PageRetouche::miseAJour()
{
    auto* s = section();
    const bool aUneSection = s != nullptr;

    for (auto* b : { &boutonSupprimer, &boutonRepeter, &boutonRenommer })
        b->setEnabled (aUneSection);

    boutonEcouter.setEnabled (aUneSection || lecteur.enCours());
    boutonAvant.setEnabled (aUneSection && sectionChoisie > 0);
    boutonApres.setEnabled (aUneSection && sectionChoisie + 1 < (int) deroule->sections.size());
    boutonCouper.setEnabled (aUneSection && curseur > deroule->debutDeLaSection (sectionChoisie) + 1.0e-6
                             && curseur < deroule->debutDeLaSection (sectionChoisie) + s->longueur - 1.0e-6);
    boutonAnnuler.setEnabled (! historique.empty());
    boutonCreer.setEnabled (deroule.has_value() && ! deroule->sections.empty());
    boutonOuverte.setEnabled (aUneSection);
    boutonOuverte.setButtonText (aUneSection && s->ouverte ? "Section ouverte : oui" : "Section ouverte : non");
    boutonRejouer.setEnabled (deroule.has_value() && ! deroule->sections.empty());
    choixPourPlay.setEnabled (deroule.has_value() && ! deroule->sections.empty());

    if (choixPourPlay.getToggleState())
        synchroniserPlay();
    boutonCreer.setButtonText (fichierDuDeroule == juce::File() ? juce::String ("Créer le déroulé…"_fr) : juce::String ("Enregistrer le déroulé"_fr));
    choixDeLEntree.setEnabled (aUneSection && s->scene >= 0);

    if (aUneSection)
    {
        choixDeLEntree.setSelectedId (s->entree ? (int) s->entree->type + 2 : 1, juce::dontSendNotification);
        choixDeLaDuree.setSelectedId (s->entree ? s->entree->mesures : 4, juce::dontSendNotification);
    }

    choixDeLaDuree.setEnabled (aUneSection && s->entree && s->entree->type != TypeEnchainement::coupe);

    // Les courbes de la ligne choisie, dans la section choisie
    const int avant = choixDeLaCourbe.getSelectedId();
    choixDeLaCourbe.clear (juce::dontSendNotification);
    const auto liste = aUneSection ? retouche::courbes (*s, ligneChoisie) : juce::StringArray();

    for (int i = 0; i < liste.size(); ++i)
        choixDeLaCourbe.addItem (retouche::nomDuReglage (liste[i]), i + 1);

    choixDeLaCourbe.setTextWhenNoChoicesAvailable (aUneSection ? "Aucun réglage ne bouge ici"_fr : juce::String ("Choisir une section"));
    choixDeLaCourbe.setSelectedId (avant > 0 && avant <= liste.size() ? avant : (liste.isEmpty() ? 0 : 1), juce::dontSendNotification);
    boutonLisser.setEnabled (! liste.isEmpty());
    boutonEffacer.setEnabled (! liste.isEmpty());

    // Le texte d'aide
    if (! deroule)
    {
        infos.setText ("Choisis une prise : elle s'affiche découpée en sections, une par scène jouée."_fr, juce::dontSendNotification);
    }
    else if (aUneSection)
    {
        infos.setText (juce::String (sectionChoisie + 1) + ". " + s->nom + " : " + formaterMesures (s->mesures()) + ", entrée : "_fr + decrireEntree (*s)
                           + ". Clique dans la ligne du temps pour placer le curseur de coupe.",
                       juce::dontSendNotification);
    }
    else
    {
        infos.setText (juce::String ((int) deroule->sections.size()) + " sections. Clique sur une section pour la choisir.", juce::dontSendNotification);
    }

    repaint();
}

//==============================================================================
void PageRetouche::changerEntree()
{
    const int type = choixDeLEntree.getSelectedId();
    const int duree = choixDeLaDuree.getSelectedId();

    modifier ([this, type, duree] (Deroule& d)
    {
        auto& s = d.sections[(size_t) sectionChoisie];

        if (type <= 1)
        {
            s.entree.reset();
            s.aLaMain = false;
        }
        else
        {
            auto e = s.entree.value_or (Enchainement {});
            e.type = (TypeEnchainement) (type - 2);
            e.mesures = duree > 0 ? duree : e.mesures;
            s.entree = e;
        }

        return true;
    });
}

void PageRetouche::synchroniserPlay()
{
    // Coché : Play (Push ou barre d'espace) rejoue le déroulé tel qu'il est à l'écran, retouches comprises
    if (choixPourPlay.getToggleState() && deroule && ! deroule->sections.empty())
        moteur.deroulePourPlay = *deroule;
    else
        moteur.deroulePourPlay.reset();
}

void PageRetouche::rejouer()
{
    if (moteur.rejeuEnCours())
    {
        moteur.arreter();
        return;
    }

    if (! deroule || deroule->sections.empty())
        return;

    lecteur.arreter();
    ecouteEnCours = false;
    moteur.rejouer (*deroule, juce::jmax (0, sectionChoisie));
}

void PageRetouche::ecouter()
{
    if (lecteur.enCours())
    {
        lecteur.arreter();
        ecouteEnCours = false;
        miseAJour();
        return;
    }

    auto* s = section();

    if (s == nullptr)
        return;

    const auto dossier = dossierDesPrises().getChildFile (s->prise);
    auto prise = Prise::charger (dossier.getChildFile ("prise.json"));

    if (! prise)
    {
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Écouter"_fr, "La prise d'où vient cette section est introuvable."_fr);
        return;
    }

    // Depuis le curseur s'il est dans la section, sinon depuis son début ; jusqu'à sa fin
    const double debutDeSection = deroule->debutDeLaSection (sectionChoisie);
    const double depart = curseur >= debutDeSection && curseur < debutDeSection + s->longueur ? curseur - debutDeSection : 0.0;

    if (moteur.enLecture())
        moteur.arreter();

    ecouteEnCours = lecteur.ecouter (dossier, prise->secondesDansLeSon (s->debutDansLaPrise + depart),
                                     prise->secondesDansLeSon (s->debutDansLaPrise + s->longueur));
    miseAJour();
}

void PageRetouche::renommer()
{
    auto* s = section();

    if (s == nullptr)
        return;

    auto fenetre = std::make_shared<juce::AlertWindow> ("Renommer la section", "Nouveau nom de la section :", juce::MessageBoxIconType::NoIcon);
    fenetre->addTextEditor ("nom", s->nom);
    fenetre->addButton ("Renommer", 1, juce::KeyPress (juce::KeyPress::returnKey));
    fenetre->addButton ("Annuler", 0, juce::KeyPress (juce::KeyPress::escapeKey));

    fenetre->enterModalState (true, juce::ModalCallbackFunction::create ([this, fenetre] (int resultat)
    {
        const auto nom = fenetre->getTextEditorContents ("nom").trim();

        if (resultat == 1 && nom.isNotEmpty())
            modifier ([this, nom] (Deroule& d) { d.sections[(size_t) sectionChoisie].nom = nom; return true; });
    }));
}

void PageRetouche::creer()
{
    if (! deroule)
        return;

    auto enregistrer = [this] (const juce::File& fichier)
    {
        if (! deroule->enregistrer (fichier))
        {
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Déroulé"_fr, "Le déroulé n'a pas pu être enregistré."_fr);
            return;
        }

        fichierDuDeroule = fichier;
        modifie = false;
        relireLesSources();
        miseAJour();
    };

    if (fichierDuDeroule != juce::File())
        return enregistrer (fichierDuDeroule);

    auto fenetre = std::make_shared<juce::AlertWindow> ("Créer le déroulé"_fr, "Nom du déroulé :"_fr, juce::MessageBoxIconType::NoIcon);
    fenetre->addTextEditor ("nom", deroule->nom);
    fenetre->addButton ("Créer"_fr, 1, juce::KeyPress (juce::KeyPress::returnKey));
    fenetre->addButton ("Annuler", 0, juce::KeyPress (juce::KeyPress::escapeKey));

    fenetre->enterModalState (true, juce::ModalCallbackFunction::create ([this, fenetre, enregistrer] (int resultat)
    {
        const auto nom = fenetre->getTextEditorContents ("nom").trim();

        if (resultat != 1 || nom.isEmpty())
            return;

        const auto fichier = dossierDesDeroules().getChildFile (juce::File::createLegalFileName (nom) + ".json");

        if (fichier.existsAsFile())
        {
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Nom pris", "Un autre déroulé porte déjà ce nom."_fr);
            return;
        }

        deroule->nom = nom;
        enregistrer (fichier);
    }));
}

//==============================================================================
double PageRetouche::versTemps (int x) const
{
    if (! deroule || deroule->longueur() <= 0.0)
        return 0.0;

    const auto zone = zoneDesSections.withTrimmedLeft (largeurDesNoms);
    return juce::jlimit (0.0, deroule->longueur(), (double) (x - zone.getX()) / zone.getWidth() * deroule->longueur());
}

float PageRetouche::versX (double temps) const
{
    const auto zone = zoneDesSections.withTrimmedLeft (largeurDesNoms);
    const auto longueur = deroule ? deroule->longueur() : 0.0;
    return longueur > 0.0 ? (float) zone.getX() + (float) (temps / longueur) * (float) zone.getWidth() : (float) zone.getX();
}

void PageRetouche::mouseDown (const juce::MouseEvent& e)
{
    if (! deroule)
        return;

    const auto p = e.getPosition();

    if (! zoneDesSections.contains (p) && ! zoneDesLignes.contains (p))
        return;

    // La ligne : le morceau en haut, puis les 8 pistes
    if (zoneDesLignes.contains (p))
        ligneChoisie = juce::jlimit (-1, nombreDePistes - 1, (p.y - zoneDesLignes.getY()) * (nombreDePistes + 1) / zoneDesLignes.getHeight() - 1);

    const bool dansLesSections = zoneDesSections.contains (p);

    if (p.x < zoneDesSections.getX() + largeurDesNoms)
        return miseAJour();

    const double t = versTemps (p.x);

    for (int i = 0; i < (int) deroule->sections.size(); ++i)
    {
        const double debut = deroule->debutDeLaSection (i);
        const auto& s = deroule->sections[(size_t) i];

        if (t >= debut && t <= debut + s.longueur)
        {
            sectionChoisie = i;

            // Le curseur se pose sur la mesure la plus proche, dans la section
            const double parMesure = s.tempsParMesure();
            curseur = debut + juce::jlimit (0.0, s.longueur, std::round ((t - debut) / parMesure) * parMesure);

            if (dansLesSections)
                choisirUneLigneQuiBouge();

            break;
        }
    }

    miseAJour();
}

void PageRetouche::paint (juce::Graphics& g)
{
    g.fillAll (couleurFond);

    if (! deroule)
        return;

    const auto zone = zoneDesSections.withTrimmedLeft (largeurDesNoms);
    const auto& etat = moteur.getMorceau();

    // Les sections, en blocs proportionnels à leur durée
    for (int i = 0; i < (int) deroule->sections.size(); ++i)
    {
        const auto& s = deroule->sections[(size_t) i];
        const double debut = deroule->debutDeLaSection (i);
        auto bloc = juce::Rectangle<float> (versX (debut), (float) zone.getY(), versX (debut + s.longueur) - versX (debut), (float) zone.getHeight()).reduced (1.0f, 0.0f);
        const auto couleur = s.scene >= 0 ? palette::couleurDePiste (s.scene % 8) : couleurPanneau;

        g.setColour (couleur.withMultipliedBrightness (i == sectionChoisie ? 1.0f : 0.6f));
        g.fillRoundedRectangle (bloc, 5.0f);

        if (i == sectionChoisie)
        {
            g.setColour (couleurTexte);
            g.drawRoundedRectangle (bloc, 5.0f, 3.0f);
        }

        // Une graduation par mesure
        g.setColour (couleurFond.withAlpha (0.35f));

        for (double m = s.tempsParMesure(); m < s.longueur - 1.0e-6; m += s.tempsParMesure())
            g.drawVerticalLine ((int) versX (debut + m), bloc.getBottom() - 10.0f, bloc.getBottom());

        g.setColour (couleurFond);
        g.setFont (juce::FontOptions (17.0f, juce::Font::bold));
        g.drawFittedText (s.nom, bloc.reduced (6.0f, 4.0f).removeFromTop (24.0f).toNearestInt(), juce::Justification::centredLeft, 1, 0.6f);
        g.setFont (juce::FontOptions (14.0f));
        g.drawFittedText (formaterMesures (s.mesures()) + (s.entree && s.entree->type != TypeEnchainement::coupe ? ", " + nomDeLEnchainement (s.entree->type).toLowerCase() : juce::String())
                              + (s.ouverte ? juce::String (", ouverte") : juce::String()),
                          bloc.reduced (6.0f, 4.0f).withTrimmedTop (24.0f).removeFromTop (20.0f).toNearestInt(), juce::Justification::centredLeft, 1, 0.6f);
    }

    g.setColour (couleurTexte);
    g.setFont (juce::FontOptions (17.0f, juce::Font::bold));
    g.drawText ("Sections", zoneDesSections.withWidth (largeurDesNoms - 8), juce::Justification::centredLeft);

    // Les lignes : le morceau, puis les 8 pistes, avec leurs courbes
    const float hauteur = (float) zoneDesLignes.getHeight() / (float) (nombreDePistes + 1);
    const auto courbeChoisie = [&]
    {
        auto* s = const_cast<PageRetouche*> (this)->section();
        const int i = choixDeLaCourbe.getSelectedId() - 1;
        const auto liste = s != nullptr ? retouche::courbes (*s, ligneChoisie) : juce::StringArray();
        return i >= 0 && i < liste.size() ? liste[i] : juce::String();
    }();

    for (int ligne = -1; ligne < nombreDePistes; ++ligne)
    {
        const float y = (float) zoneDesLignes.getY() + (float) (ligne + 1) * hauteur;
        const auto bande = juce::Rectangle<float> ((float) zone.getX(), y, (float) zone.getWidth(), hauteur).reduced (0.0f, 2.0f);
        const auto couleur = ligne < 0 ? couleurPremier : palette::couleurDePiste (ligne);

        g.setColour (ligne == ligneChoisie ? couleurPanneau.brighter (0.15f) : couleurPanneau.withAlpha (0.5f));
        g.fillRect (bande);

        g.setColour (ligne == ligneChoisie ? couleurTexte : couleurDiscret);
        g.setFont (juce::FontOptions (15.0f, ligne == ligneChoisie ? juce::Font::bold : juce::Font::plain));
        g.drawFittedText (ligne < 0 ? juce::String ("Morceau") : etat.pistes[(size_t) ligne].nom,
                          juce::Rectangle<float> ((float) zoneDesLignes.getX(), y, (float) largeurDesNoms - 8.0f, hauteur).toNearestInt(),
                          juce::Justification::centredLeft, 1, 0.6f);

        for (int i = 0; i < (int) deroule->sections.size(); ++i)
        {
            const auto& s = deroule->sections[(size_t) i];
            const double debut = deroule->debutDeLaSection (i);

            for (auto& c : retouche::courbes (s, ligne))
            {
                const auto pts = retouche::points (s, c);
                const auto [bas, haut] = bornes.count (c) ? bornes[c] : std::pair { 0.0, 1.0 };
                auto versY = [&] (double v) { return haut > bas ? bande.getBottom() - 3.0f - (float) ((v - bas) / (haut - bas)) * (bande.getHeight() - 6.0f) : bande.getCentreY(); };

                juce::Path trace;
                trace.startNewSubPath (versX (debut + pts.front().first), versY (pts.front().second));

                for (size_t k = 1; k < pts.size(); ++k)
                {
                    trace.lineTo (versX (debut + pts[k].first), versY (pts[k - 1].second));     // en marches : la valeur tient jusqu'au cran suivant
                    trace.lineTo (versX (debut + pts[k].first), versY (pts[k].second));
                }

                trace.lineTo (versX (debut + s.longueur), versY (pts.back().second));
                const bool choisie = i == sectionChoisie && ligne == ligneChoisie && c == courbeChoisie;
                g.setColour (choisie ? couleurTexte : couleur.withAlpha (0.8f));
                g.strokePath (trace, juce::PathStrokeType (choisie ? 3.0f : 1.5f));
            }
        }
    }

    // Pendant le rejeu : où en est le déroulé
    if (const auto r = moteur.etatDuRejeu(); r && r->deroule == deroule->nom && r->numero - 1 < (int) deroule->sections.size())
    {
        const auto& s = deroule->sections[(size_t) r->numero - 1];
        const double t = deroule->debutDeLaSection (r->numero - 1) + juce::jlimit (0.0, s.longueur, s.longueur - r->mesuresRestantes * s.tempsParMesure());
        g.setColour (couleurActif);
        g.fillRect (versX (t) - 1.0f, (float) zoneDesSections.getY() - 4.0f, 3.0f, (float) (zoneDesLignes.getBottom() - zoneDesSections.getY()) + 4.0f);
    }

    // Le curseur de coupe
    if (curseur >= 0.0)
    {
        g.setColour (couleurGroupe);
        g.fillRect (versX (curseur) - 1.0f, (float) zoneDesSections.getY() - 4.0f, 3.0f, (float) (zoneDesLignes.getBottom() - zoneDesSections.getY()) + 4.0f);
    }
}

void PageRetouche::timerCallback()
{
    if (++compteur % 40 == 0)
        relireLesSources();

    if (ecouteEnCours && ! lecteur.enCours())
    {
        lecteur.arreter();
        ecouteEnCours = false;
        miseAJour();
    }

    boutonEcouter.setButtonText (lecteur.enCours() ? juce::String ("Arrêter l'écoute"_fr) : juce::String ("Écouter"_fr));
    boutonRejouer.setButtonText (moteur.rejeuEnCours() ? juce::String ("Arrêter le rejeu"_fr) : juce::String ("Rejouer depuis la section"));

    if (moteur.rejeuEnCours())
        repaint (zoneDesSections.getUnion (zoneDesLignes).expanded (0, 6));

    // La case suit ce que Play rejouera vraiment (un déroulé a pu être choisi ailleurs, par la Démo 2)
    choixPourPlay.setToggleState (deroule && moteur.deroulePourPlay && moteur.deroulePourPlay->nom == deroule->nom, juce::dontSendNotification);
}

void PageRetouche::resized()
{
    auto zone = getLocalBounds().reduced (20);
    auto haut = zone.removeFromTop (44);
    titre.setBounds (haut.removeFromLeft (180));
    choixDeLaSource.setBounds (haut.removeFromLeft (520).reduced (0, 6));
    infos.setBounds (zone.removeFromTop (30));
    zone.removeFromTop (8);

    auto ligneC = zone.removeFromBottom (42);
    zone.removeFromBottom (6);
    auto ligneB = zone.removeFromBottom (42);
    zone.removeFromBottom (6);
    auto ligneA = zone.removeFromBottom (42);
    zone.removeFromBottom (12);

    const int largeur = ligneA.getWidth() / 7;

    for (auto* b : { &boutonEcouter, &boutonCouper, &boutonSupprimer, &boutonAvant, &boutonApres, &boutonRepeter, &boutonRenommer })
        b->setBounds (ligneA.removeFromLeft (largeur).reduced (3, 2));

    etiquetteEntree.setBounds (ligneB.removeFromLeft (80));
    choixDeLEntree.setBounds (ligneB.removeFromLeft (190).reduced (3, 4));
    choixDeLaDuree.setBounds (ligneB.removeFromLeft (130).reduced (3, 4));
    etiquetteCourbe.setBounds (ligneB.removeFromLeft (90));
    choixDeLaCourbe.setBounds (ligneB.removeFromLeft (230).reduced (3, 4));
    boutonLisser.setBounds (ligneB.removeFromLeft (100).reduced (3, 2));
    boutonEffacer.setBounds (ligneB.removeFromLeft (100).reduced (3, 2));

    boutonAnnuler.setBounds (ligneC.removeFromLeft (165).reduced (3, 2));
    boutonOuverte.setBounds (ligneC.removeFromLeft (175).reduced (3, 2));
    boutonRejouer.setBounds (ligneC.removeFromLeft (215).reduced (3, 2));
    choixPourPlay.setBounds (ligneC.removeFromLeft (200).reduced (8, 2));
    boutonCreer.setBounds (ligneC.reduced (3, 2));

    zoneDesSections = zone.removeFromTop (70);
    zone.removeFromTop (8);
    zoneDesLignes = zone;
}

} // namespace groovebox
