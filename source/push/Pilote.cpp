#include "Pilote.h"

namespace groovebox
{

namespace
{
    const juce::Colour fond { 0xff101216 }, texte { 0xffeeeeef }, discret { 0xff8e9099 }, actif { 0xff3fb27f };
    const juce::Colour premier { 0xffff9f43 }, panneau { 0xff2a2d35 };

    juce::String formaterTempo (double t)
    {
        const bool entier = std::abs (t - std::round (t)) < 0.005;
        return (entier ? juce::String (juce::roundToInt (t)) : juce::String (t, 1).replaceCharacter ('.', ',')) + " BPM";
    }

    juce::String formaterDb (float db)
    {
        return db <= -59.5f ? juce::String ("Muet") : juce::String (juce::roundToInt (db)) + " dB";
    }

    juce::String nomDuDecompte (int n)
    {
        return n == 0 ? juce::String ("Aucun") : n == 1 ? juce::String ("1 mesure") : juce::String ("2 mesures");
    }

    constexpr double dureeDUneAnnonce = 1500.0;
}

Pilote::Pilote (Moteur& m, Surface& s)  : moteur (m), surface (s)
{
    auto modifier = [this] (int encodeur, std::function<void (Morceau&)> f)
    {
        moteur.modifier (f, "encodeur " + juce::String (encodeur));
    };

    pageClic[0] = { "Tempo",
                    [this] { return formaterTempo (moteur.getMorceau().tempo); },
                    [this] { return (float) ((moteur.getMorceau().tempo - Morceau::tempoMin) / (Morceau::tempoMax - Morceau::tempoMin)); },
                    [modifier] (int pas, bool fin) { modifier (0, [=] (Morceau& m) { m.tempo += pas * (fin ? 0.025 : 0.25); }); }, actif };

    pageClic[1] = { "Volume clic",
                    [this] { return juce::String (juce::roundToInt (moteur.getMorceau().clic.volume * 100.0f)) + " %"; },
                    [this] { return moteur.getMorceau().clic.volume; },
                    [modifier] (int pas, bool fin) { modifier (1, [=] (Morceau& m) { m.clic.volume += (float) pas * (fin ? 0.001f : 0.005f); }); }, actif };

    pageClic[2] = { "Casque",
                    [this] { return formaterDb (moteur.getMorceau().musiqueDansCasqueDb); },
                    [this] { return (moteur.getMorceau().musiqueDansCasqueDb + 40.0f) / 40.0f; },
                    [modifier] (int pas, bool fin) { modifier (2, [=] (Morceau& m) { m.musiqueDansCasqueDb = juce::jmax (-40.0f, m.musiqueDansCasqueDb + (float) pas * (fin ? 0.05f : 0.25f)); }); }, actif };

    pageClic[3] = { "Décompte"_fr,
                    [this] { return nomDuDecompte (moteur.getMorceau().clic.decompte); },
                    [this] { return (float) moteur.getMorceau().clic.decompte / 2.0f; },
                    [this, modifier] (int pas, bool) { if (auto c = crantsDiscrets (3, pas)) modifier (3, [=] (Morceau& m) { m.clic.decompte += c; }); }, actif };

    pageClic[4] = { "Subdivisions",
                    [this] { return moteur.getMorceau().clic.subdivisions ? juce::String ("Oui") : juce::String ("Non"); },
                    [this] { return moteur.getMorceau().clic.subdivisions ? 1.0f : 0.0f; },
                    [this, modifier] (int pas, bool) { if (auto c = crantsDiscrets (4, pas)) modifier (4, [=] (Morceau& m) { m.clic.subdivisions = c > 0; }); }, actif };

    pageClic[5] = { "Par groupe",
                    [this] { return moteur.getMorceau().clic.unClicParGroupe ? juce::String ("Oui") : juce::String ("Non"); },
                    [this] { return moteur.getMorceau().clic.unClicParGroupe ? 1.0f : 0.0f; },
                    [this, modifier] (int pas, bool) { if (auto c = crantsDiscrets (5, pas)) modifier (5, [=] (Morceau& m) { m.clic.unClicParGroupe = c > 0; }); }, actif };

    // Page Mix : le volume de chaque piste, dans sa couleur
    for (int i = 0; i < nombreDePistes; ++i)
        pageMix[(size_t) i] = { {},
                                [this, i] { return formaterDb (moteur.getMorceau().pistes[(size_t) i].volumeDb); },
                                [this, i] { return (moteur.getMorceau().pistes[(size_t) i].volumeDb + 60.0f) / 66.0f; },
                                [modifier, i] (int pas, bool fin) { modifier (i, [=] (Morceau& m) { m.pistes[(size_t) i].volumeDb += (float) pas * (fin ? 0.05f : 0.3f); }); },
                                palette::couleurDePiste (i) };

    surface.surEvenement = [this] (const Evenement& e) { traiter (e); };
    rafraichir();
    startTimerHz (60);
}

Pilote::~Pilote()
{
    stopTimer();
    surface.surEvenement = nullptr;
}

bool Pilote::shift() const
{
    return surface.estAppuye (Bouton::shift) || juce::ModifierKeys::currentModifiers.isShiftDown();
}

void Pilote::annoncer (const juce::String& titre, const juce::String& valeur)
{
    annonce = { titre, valeur, juce::Time::getMillisecondCounterHiRes() + dureeDUneAnnonce };
}

int Pilote::crantsDiscrets (int encodeur, int pas)
{
    // Réglages à quelques valeurs : un changement tous les 8 pas, pour ne pas sauter par accident
    auto& c = crans[(size_t) encodeur];
    c += pas;
    const int changement = c / 8;
    c -= changement * 8;
    return changement;
}

//==============================================================================
void Pilote::traiter (const Evenement& e)
{
    switch (e.type)
    {
        case Evenement::Type::pad:
            if (e.appuye)
                appuyerPad (e.ligne(), e.colonne());
            break;

        case Evenement::Type::bouton:
            if (e.appuye)
                appuyer (e.numero);
            else
                relacher (e.numero);
            break;

        case Evenement::Type::encodeurTourne:
            tournerEncodeur (e.numero, e.valeur);
            break;

        case Evenement::Type::encodeurTouche:
            touches[(size_t) e.numero] = e.appuye;

            if (e.appuye && e.numero < 8 && reglages()[(size_t) e.numero].valeur)
            {
                const auto& r = reglages()[(size_t) e.numero];
                annoncer (page == Page::mix ? moteur.getMorceau().pistes[(size_t) e.numero].nom : r.nom, r.valeur());
            }
            break;

        case Evenement::Type::bandeTouchee:
            if (e.appuye)
                annoncer ("Bande tactile", "Filtre global à l'étape 7"_fr);
            break;

        case Evenement::Type::bandeDeplacee:
            annoncer ("Bande tactile", juce::String (juce::roundToInt (e.valeur * 100.0 / 16383.0)) + " %");
            break;
    }

    rafraichir();
}

void Pilote::tournerEncodeur (int encodeur, int pas)
{
    const bool fin = shift();

    if (encodeur < 8)
    {
        const auto& r = reglages()[(size_t) encodeur];

        if (r.tourner)
        {
            r.tourner (pas, fin);
            annoncer (page == Page::mix ? moteur.getMorceau().pistes[(size_t) encodeur].nom : r.nom, r.valeur());
        }

        return;
    }

    switch ((Encodeur) encodeur)
    {
        case Encodeur::tempo:
            moteur.modifier ([=] (Morceau& m) { m.tempo += pas * (fin ? 0.1 : 1.0); }, "encodeur tempo");
            annoncer ("Tempo", formaterTempo (moteur.getMorceau().tempo));
            break;

        case Encodeur::master:
            moteur.modifier ([=] (Morceau& m) { m.volumeGeneralDb += (float) pas * (fin ? 0.05f : 0.25f); }, "encodeur master");
            annoncer ("Volume général"_fr, formaterDb (moteur.getMorceau().volumeGeneralDb));
            break;

        case Encodeur::swing:
            annoncer ("Swing", "Arrive à l'étape 5"_fr);
            break;

        default:
            break;
    }
}

//==============================================================================
void Pilote::appuyerPad (int ligne, int colonne)
{
    const int scene = sceneDeLaLigne (ligne);
    const int piste = colonne;
    const auto& morceau = moteur.getMorceau();
    const auto* clip = morceau.clip (piste, scene);
    pisteChoisie = piste;

    if (tenu (Bouton::deleteBouton))
    {
        if (clip != nullptr)
        {
            moteur.modifier ([=] (Morceau& m) { m.scenes[(size_t) scene].clips[(size_t) piste].reset(); });
            annoncer ("Clip effacé"_fr, morceau.pistes[(size_t) piste].nom + ", " + morceau.scenes[(size_t) scene].nom);
        }

        return;
    }

    if (tenu (Bouton::duplicate))
    {
        // Duplicate tenu : premier pad = la source, second pad = la destination. Sinon, au relâché, copie juste en dessous.
        if (! sourceDeCopie)
        {
            if (clip != nullptr)
                sourceDeCopie = std::pair { piste, scene };
        }
        else
        {
            const auto [p0, s0] = *sourceDeCopie;
            moteur.modifier ([=] (Morceau& m)
            {
                m.assurerScenes (scene + 1);
                m.scenes[(size_t) scene].clips[(size_t) piste] = m.scenes[(size_t) s0].clips[(size_t) p0];
            });
            copieFaite = true;
            annoncer ("Clip copié"_fr, morceau.scenes[(size_t) s0].nom + " vers " + moteur.getMorceau().scenes[(size_t) scene].nom);
        }

        return;
    }

    if (tenu (Bouton::select))
    {
        caseChoisie = std::pair { piste, scene };
        annoncer ("Clip choisi", clip != nullptr ? clip->nom : juce::String ("Case vide"));
        return;
    }

    caseChoisie = std::pair { piste, scene };

    if (clip != nullptr)
    {
        moteur.lancerClip (piste, scene);
        annoncer (morceau.pistes[(size_t) piste].nom, clip->nom + (moteur.enLecture() ? juce::String (", ") + nomDuCalage (morceau.calage).toLowerCase() : juce::String()));
    }
    else
    {
        moteur.arreterPiste (piste);
        annoncer (morceau.pistes[(size_t) piste].nom, juce::String::fromUTF8 ("Arrêt"));
    }
}

void Pilote::dupliquerVersLeBas (int piste, int scene)
{
    // Vers la première case vide en dessous ; une scène de plus s'il n'y en a pas
    moteur.modifier ([=] (Morceau& m)
    {
        int cible = scene + 1;

        while (cible < (int) m.scenes.size() && m.scenes[(size_t) cible].clips[(size_t) piste].has_value())
            ++cible;

        m.assurerScenes (cible + 1);
        m.scenes[(size_t) cible].clips[(size_t) piste] = m.scenes[(size_t) scene].clips[(size_t) piste];
    });

    annoncer ("Clip dupliqué"_fr, moteur.getMorceau().pistes[(size_t) piste].nom);
}

void Pilote::appuyerScene (int scene)
{
    const auto& morceau = moteur.getMorceau();

    if (scene >= (int) morceau.scenes.size())
        return;

    if (tenu (Bouton::deleteBouton))
    {
        moteur.modifier ([=] (Morceau& m) { for (auto& c : m.scenes[(size_t) scene].clips) c.reset(); });
        annoncer ("Scène vidée"_fr, morceau.scenes[(size_t) scene].nom);
        return;
    }

    if (tenu (Bouton::duplicate))
    {
        if (! sceneSourceDeCopie)
        {
            sceneSourceDeCopie = scene;
        }
        else
        {
            const int source = *sceneSourceDeCopie;
            moteur.modifier ([=] (Morceau& m) { m.scenes[(size_t) scene].clips = m.scenes[(size_t) source].clips; });
            copieFaite = true;
            annoncer ("Scène copiée"_fr, morceau.scenes[(size_t) source].nom + " vers " + morceau.scenes[(size_t) scene].nom);
        }

        return;
    }

    moteur.lancerScene (scene);
    annoncer ("Scène "_fr + juce::String (scene + 1), morceau.scenes[(size_t) scene].nom);
}

void Pilote::appuyerPiste (int piste)
{
    const auto& p = moteur.getMorceau().pistes[(size_t) piste];
    pisteChoisie = piste;

    if (tenu (Bouton::mute))
    {
        moteur.modifier ([=] (Morceau& m) { m.pistes[(size_t) piste].mute = ! m.pistes[(size_t) piste].mute; });
        annoncer (p.nom, moteur.getMorceau().pistes[(size_t) piste].mute ? juce::String ("Coupée"_fr) : juce::String ("Rétablie"_fr));
    }
    else if (tenu (Bouton::solo))
    {
        moteur.modifier ([=] (Morceau& m) { m.pistes[(size_t) piste].solo = ! m.pistes[(size_t) piste].solo; });
        annoncer (p.nom, moteur.getMorceau().pistes[(size_t) piste].solo ? juce::String ("Solo") : juce::String ("Solo retiré"_fr));
    }
    else if (tenu (Bouton::stopClip))
    {
        moteur.arreterPiste (piste);
        annoncer (p.nom, "Arrêt au prochain calage"_fr);
    }
    else
    {
        annoncer ("Piste "_fr + juce::String (piste + 1), p.nom);
    }
}

void Pilote::appuyer (int bouton)
{
    if (bouton >= 36 && bouton <= 43)
        return appuyerScene (premiereScene + (43 - bouton));

    if (bouton >= premierBoutonDuBas && bouton < premierBoutonDuBas + 8)
        return appuyerPiste (bouton - premierBoutonDuBas);

    const auto& morceau = moteur.getMorceau();

    switch ((Bouton) bouton)
    {
        case Bouton::play:
            if (moteur.enLecture())
                moteur.arreter();
            else
                moteur.lancer();

            annoncer ("Play", moteur.enLecture() ? "Lecture" : juce::String ("Arrêt"_fr));
            return;

        case Bouton::metronome:
            moteur.modifier ([] (Morceau& m) { m.clic.actif = ! m.clic.actif; });
            annoncer ("Metronome", moteur.getMorceau().clic.actif ? "Clic actif" : juce::String ("Clic coupé"_fr));
            return;

        case Bouton::tapTempo:
            if (auto t = tapTempo.taper (juce::Time::getMillisecondCounterHiRes() / 1000.0))
                moteur.modifier ([t] (Morceau& m) { m.tempo = std::round (*t); }, "tap tempo");

            annoncer ("Tap Tempo", formaterTempo (moteur.getMorceau().tempo));
            return;

        case Bouton::shift:
            return;

        case Bouton::stopClip:
            if (shift())
            {
                moteur.arreterTousLesClips();
                annoncer ("Stop Clip", "Tous les clips s'arrêtent"_fr);
            }
            return;

        case Bouton::mute:
        case Bouton::solo:
        case Bouton::deleteBouton:
        case Bouton::select:
            return;     // se tiennent, avec un pad ou un bouton de piste

        case Bouton::duplicate:
            sourceDeCopie.reset();
            sceneSourceDeCopie.reset();
            copieFaite = false;
            return;

        case Bouton::undo:
            if (shift() ? moteur.retablir() : moteur.annuler())
                annoncer (shift() ? juce::String ("Rétablir"_fr) : juce::String ("Annuler"), "Fait");
            else
                annoncer (shift() ? juce::String ("Rétablir"_fr) : juce::String ("Annuler"), "Rien à faire"_fr);
            return;

        case Bouton::quantize:
        {
            const auto suivant = (Calage) (((int) morceau.calage + 1) % 4);
            moteur.modifier ([suivant] (Morceau& m) { m.calage = suivant; });
            annoncer ("Calage des lancements", nomDuCalage (suivant));
            return;
        }

        case Bouton::doubleLoop:
            if (caseChoisie)
            {
                const auto [p, s] = *caseChoisie;

                if (auto c = morceau.clip (p, s))
                {
                    const int longueur = c->longueurMesures;
                    moteur.modifier ([=] (Morceau& m) { m.scenes[(size_t) s].clips[(size_t) p]->doubler (m.mesure.numerateur); });
                    annoncer ("Double Loop", moteur.getMorceau().clip (p, s)->longueurMesures > longueur
                                                 ? juce::String (moteur.getMorceau().clip (p, s)->longueurMesures) + " mesures"
                                                 : juce::String ("16 mesures au plus"));
                    return;
                }
            }

            annoncer ("Double Loop", "Choisis d'abord un clip");
            return;

        case Bouton::haut:
        case Bouton::bas:
        {
            const int pas = (shift() ? 8 : 1) * ((Bouton) bouton == Bouton::haut ? -1 : 1);
            const int maximum = juce::jmax (0, (int) morceau.scenes.size() - 8);
            premiereScene = juce::jlimit (0, maximum, premiereScene + pas);
            annoncer ("Scènes"_fr, juce::String (premiereScene + 1) + " à " + juce::String (premiereScene + 8));
            return;
        }

        case Bouton::nouveau:
            moteur.modifier ([] (Morceau& m) { m.assurerScenes ((int) m.scenes.size() + 1); });
            annoncer ("New", "Scène "_fr + juce::String ((int) moteur.getMorceau().scenes.size()) + " ajoutée"_fr);
            return;

        case Bouton::mix:
            page = Page::mix;
            annoncer ("Mix", "Volumes des pistes sur les encodeurs");
            return;

        case Bouton::setup:
            page = Page::clic;
            annoncer ("Setup", "Réglages du clic sur les encodeurs"_fr);
            return;

        default:
            break;
    }

    annoncer (nomDuBouton (bouton), "Prévu à une étape suivante"_fr);
}

void Pilote::relacher (int bouton)
{
    if ((Bouton) bouton == Bouton::duplicate)
    {
        // Duplicate relâché après un seul pad : copie juste en dessous
        if (! copieFaite && sourceDeCopie)
            dupliquerVersLeBas (sourceDeCopie->first, sourceDeCopie->second);
        else if (! copieFaite && sceneSourceDeCopie)
        {
            const int source = *sceneSourceDeCopie;
            moteur.modifier ([=] (Morceau& m)
            {
                m.scenes.insert (m.scenes.begin() + source + 1, m.scenes[(size_t) source]);
                m.scenes[(size_t) source + 1].nom += " (copie)";
            });
            annoncer ("Scène dupliquée"_fr, moteur.getMorceau().scenes[(size_t) source + 1].nom);
        }

        sourceDeCopie.reset();
        sceneSourceDeCopie.reset();
        copieFaite = false;
    }
}

//==============================================================================
EtatSurface Pilote::calculerEtat() const
{
    EtatSurface etat;
    const auto& morceau = moteur.getMorceau();
    const auto p = moteur.getPosition();
    const bool clignote = std::fmod (p.fraction * 2.0, 1.0) < 0.5;
    const bool pulse = p.fraction < 0.5;

    // Pads : la Session
    for (int ligne = 0; ligne < 8; ++ligne)
        for (int colonne = 0; colonne < 8; ++colonne)
        {
            const auto c = moteur.etatCase (colonne, sceneDeLaLigne (ligne));
            int intensite = -1;

            if (c.departEnAttente)          intensite = clignote ? 3 : 0;
            else if (c.arretEnAttente)      intensite = clignote ? 2 : 0;
            else if (c.joue)                intensite = pulse ? 3 : 2;
            else if (c.clip)                intensite = 1;

            etat.pads[(size_t) (ligne * 8 + colonne)] = intensite < 0 ? palette::noir
                                                       : intensite == 0 ? palette::indexDePiste (colonne, 0)
                                                                        : palette::indexDePiste (colonne, intensite);
        }

    // Boutons de scène : allumés s'il y a des clips, verts si la scène joue
    for (int k = 0; k < 8; ++k)
    {
        const int scene = premiereScene + k;
        int index = palette::noir;

        if (scene < (int) morceau.scenes.size() && ! morceau.scenes[(size_t) scene].estVide())
        {
            bool joue = true;

            for (int piste = 0; piste < nombreDePistes; ++piste)
                if (morceau.clip (piste, scene) != nullptr && ! moteur.etatCase (piste, scene).joue)
                    joue = false;

            index = joue ? palette::vert : palette::grisClair;
        }

        etat.boutons[(size_t) (43 - k)] = index;
    }

    // Boutons de piste sous l'écran : couleur de la piste, éteints si coupée, bleus en solo
    for (int piste = 0; piste < nombreDePistes; ++piste)
    {
        const auto& pp = morceau.pistes[(size_t) piste];
        etat.boutons[(size_t) (premierBoutonDuBas + piste)] = pp.solo ? palette::bleu
                                                             : pp.mute ? palette::noir
                                                                       : palette::indexDePiste (piste, piste == pisteChoisie ? 3 : 1);
    }

    auto blanc = [&etat] (Bouton b, bool fort) { etat.boutons[(size_t) b] = fort ? palette::blancFort : palette::blancFaible; };
    auto couleur = [&etat] (Bouton b, bool fort) { etat.boutons[(size_t) b] = fort ? palette::blanc : palette::grisFonce; };

    etat.boutons[(size_t) Bouton::play] = p.enLecture ? palette::vert : palette::grisClair;
    blanc (Bouton::metronome, morceau.clic.actif);
    blanc (Bouton::tapTempo, p.enLecture && p.fraction < 0.15);
    blanc (Bouton::shift, tenu (Bouton::shift));
    couleur (Bouton::mute, tenu (Bouton::mute));
    couleur (Bouton::solo, tenu (Bouton::solo));
    couleur (Bouton::stopClip, tenu (Bouton::stopClip));
    blanc (Bouton::deleteBouton, tenu (Bouton::deleteBouton));
    blanc (Bouton::duplicate, tenu (Bouton::duplicate));
    blanc (Bouton::select, tenu (Bouton::select));
    blanc (Bouton::quantize, false);
    blanc (Bouton::nouveau, false);
    blanc (Bouton::mix, page == Page::mix);
    blanc (Bouton::setup, page == Page::clic);

    if (caseChoisie && morceau.clip (caseChoisie->first, caseChoisie->second) != nullptr)
        blanc (Bouton::doubleLoop, false);

    if (moteur.peutAnnuler() || moteur.peutRetablir())
        blanc (Bouton::undo, false);

    if (premiereScene > 0)
        blanc (Bouton::haut, false);

    if (premiereScene + 8 < (int) morceau.scenes.size())
        blanc (Bouton::bas, false);

    return etat;
}

void Pilote::dessinerEcran (juce::Image& image) const
{
    juce::Graphics g (image);
    const auto& morceau = moteur.getMorceau();
    const auto p = moteur.getPosition();
    const float largeur = (float) image.getWidth();

    g.fillAll (fond);

    // Bandeau permanent : tempo, mesure, position, clic
    g.setColour (texte);
    g.setFont (juce::FontOptions (32.0f, juce::Font::bold));
    g.drawText (formaterTempo (morceau.tempo), juce::Rectangle<float> (14.0f, 4.0f, 200.0f, 40.0f), juce::Justification::centredLeft);

    g.setFont (juce::FontOptions (28.0f));
    g.drawText (morceau.mesure.nom() + "  " + morceau.mesure.nomGroupement(), juce::Rectangle<float> (220.0f, 4.0f, 230.0f, 40.0f), juce::Justification::centredLeft);

    juce::String position;

    if (! p.enLecture)
        position = "À l'arrêt"_fr;
    else if (p.mesure < 1)
        position = "Décompte "_fr + juce::String (1 - p.mesure);
    else
        position = "Mesure " + juce::String (p.mesure) + "   temps " + juce::String (p.temps + 1);

    g.setColour (p.enLecture ? actif : discret);
    g.setFont (juce::FontOptions (28.0f, juce::Font::bold));
    g.drawText (position, juce::Rectangle<float> (455.0f, 4.0f, 340.0f, 40.0f), juce::Justification::centredLeft);

    const auto zoneClic = juce::Rectangle<float> (largeur - 150.0f, 9.0f, 136.0f, 30.0f);
    g.setColour (morceau.clic.actif ? premier : panneau);
    g.fillRoundedRectangle (zoneClic, 6.0f);
    g.setColour (morceau.clic.actif ? fond : discret);
    g.setFont (juce::FontOptions (22.0f, juce::Font::bold));
    g.drawText (morceau.clic.actif ? juce::String ("Clic") : juce::String ("Clic coupé"_fr), zoneClic, juce::Justification::centred);

    g.setColour (panneau);
    g.fillRect (0.0f, 50.0f, largeur, 2.0f);

    // Annonce, par-dessus les colonnes
    if (juce::Time::getMillisecondCounterHiRes() < annonce.fin)
    {
        g.setColour (texte);
        g.setFont (juce::FontOptions (30.0f));
        g.drawFittedText (annonce.titre, juce::Rectangle<int> (10, 56, (int) largeur - 20, 44), juce::Justification::centred, 1, 0.6f);
        g.setFont (juce::FontOptions (44.0f, juce::Font::bold));
        g.drawFittedText (annonce.valeur, juce::Rectangle<int> (10, 100, (int) largeur - 20, 56), juce::Justification::centred, 1, 0.5f);
        return;
    }

    // 8 colonnes alignées sur les encodeurs
    const float colonne = largeur / 8.0f;
    const auto& liste = reglages();

    for (int i = 0; i < 8; ++i)
    {
        const auto& r = liste[(size_t) i];
        const float x = i * colonne;

        if (! r.valeur)
            continue;

        juce::String nom = r.nom;

        if (page == Page::mix)
        {
            // Colonne d'une piste : bandeau de couleur, nom, état
            const auto& piste = morceau.pistes[(size_t) i];
            const int scene = moteur.sceneJouee (i);
            g.setColour (piste.mute ? panneau : r.couleur.withAlpha (i == pisteChoisie ? 1.0f : 0.6f));
            g.fillRect (x + 4.0f, 55.0f, colonne - 8.0f, i == pisteChoisie ? 6.0f : 3.0f);
            nom = piste.nom + (piste.solo ? " S" : "") + (scene >= 0 ? juce::String (" ") + juce::String::charToString (0x25B6) : juce::String());
        }

        g.setColour (touches[(size_t) i] ? texte : discret);
        g.setFont (juce::FontOptions (21.0f));
        g.drawFittedText (nom, juce::Rectangle<float> (x + 4.0f, 62.0f, colonne - 8.0f, 26.0f).toNearestInt(), juce::Justification::centred, 1, 0.7f);

        g.setColour (texte);
        g.setFont (juce::FontOptions (30.0f, juce::Font::bold));
        g.drawFittedText (r.valeur(), juce::Rectangle<float> (x + 3.0f, 90.0f, colonne - 6.0f, 36.0f).toNearestInt(), juce::Justification::centred, 1, 0.6f);

        const auto barre = juce::Rectangle<float> (x + 14.0f, 136.0f, colonne - 28.0f, 10.0f);
        g.setColour (panneau);
        g.fillRoundedRectangle (barre, 4.0f);
        g.setColour (touches[(size_t) i] ? premier : r.couleur);
        g.fillRoundedRectangle (barre.withWidth (barre.getWidth() * juce::jlimit (0.0f, 1.0f, r.proportion())), 4.0f);
    }
}

void Pilote::rafraichir()
{
    dessinerEcran (ecran);
    surface.publier (calculerEtat(), ecran);
}

} // namespace groovebox
