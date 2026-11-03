#include "Pilote.h"

namespace groovebox
{

namespace
{
    const juce::Colour fond { 0xff101216 }, texte { 0xffeeeeef }, discret { 0xff8e9099 }, actif { 0xff3fb27f };
    const juce::Colour premier { 0xffff9f43 };

    juce::String formaterTempo (double t)
    {
        const bool entier = std::abs (t - std::round (t)) < 0.005;
        return (entier ? juce::String (juce::roundToInt (t)) : juce::String (t, 1).replaceCharacter ('.', ',')) + " BPM";
    }

    juce::String formaterDb (float db)
    {
        return juce::String (juce::roundToInt (db)) + " dB";
    }

    juce::String nomDuDecompte (int n)
    {
        return n == 0 ? juce::String ("Aucun") : n == 1 ? juce::String ("1 mesure") : juce::String ("2 mesures");
    }

    constexpr double dureeDUneAnnonce = 1500.0;
}

Pilote::Pilote (Moteur& m, Surface& s)  : moteur (m), surface (s)
{
    auto modifier = [this] (std::function<void (Morceau&)> f) { moteur.modifier (f); };

    page[0] = { "Tempo",
                [this] { return formaterTempo (moteur.getMorceau().tempo); },
                [this] { return (float) ((moteur.getMorceau().tempo - Morceau::tempoMin) / (Morceau::tempoMax - Morceau::tempoMin)); },
                [modifier] (int pas, bool fin) { modifier ([=] (Morceau& m) { m.tempo += pas * (fin ? 0.025 : 0.25); }); } };

    page[1] = { "Volume clic",
                [this] { return juce::String (juce::roundToInt (moteur.getMorceau().clic.volume * 100.0f)) + " %"; },
                [this] { return moteur.getMorceau().clic.volume; },
                [modifier] (int pas, bool fin) { modifier ([=] (Morceau& m) { m.clic.volume += (float) pas * (fin ? 0.001f : 0.005f); }); } };

    page[2] = { "Casque",
                [this] { return formaterDb (moteur.getMorceau().musiqueDansCasqueDb); },
                [this] { return (moteur.getMorceau().musiqueDansCasqueDb + 40.0f) / 40.0f; },
                [modifier] (int pas, bool fin) { modifier ([=] (Morceau& m) { m.musiqueDansCasqueDb += (float) pas * (fin ? 0.05f : 0.25f); }); } };

    page[3] = { "Décompte"_fr,
                [this] { return nomDuDecompte (moteur.getMorceau().clic.decompte); },
                [this] { return (float) moteur.getMorceau().clic.decompte / 2.0f; },
                [this, modifier] (int pas, bool) { if (auto c = crantsDiscrets (3, pas)) modifier ([=] (Morceau& m) { m.clic.decompte += c; }); } };

    page[4] = { "Subdivisions",
                [this] { return moteur.getMorceau().clic.subdivisions ? juce::String ("Oui") : juce::String ("Non"); },
                [this] { return moteur.getMorceau().clic.subdivisions ? 1.0f : 0.0f; },
                [this, modifier] (int pas, bool) { if (auto c = crantsDiscrets (4, pas)) modifier ([=] (Morceau& m) { m.clic.subdivisions = c > 0; }); } };

    page[5] = { "Par groupe",
                [this] { return moteur.getMorceau().clic.unClicParGroupe ? juce::String ("Oui") : juce::String ("Non"); },
                [this] { return moteur.getMorceau().clic.unClicParGroupe ? 1.0f : 0.0f; },
                [this, modifier] (int pas, bool) { if (auto c = crantsDiscrets (5, pas)) modifier ([=] (Morceau& m) { m.clic.unClicParGroupe = c > 0; }); } };

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
            velocites[(size_t) e.numero] = e.valeur;

            if (e.appuye)
                annoncer ("Pad ligne "_fr + juce::String (e.ligne() + 1) + ", colonne " + juce::String (e.colonne() + 1),
                          "Vélocité "_fr + juce::String (e.valeur));
            break;

        case Evenement::Type::bouton:
            if (e.appuye)
                appuyer (e.numero);
            break;

        case Evenement::Type::encodeurTourne:
            tournerEncodeur (e.numero, e.valeur);
            break;

        case Evenement::Type::encodeurTouche:
            touches[(size_t) e.numero] = e.appuye;

            if (e.appuye && e.numero < 8 && page[(size_t) e.numero].valeur)
                annoncer (page[(size_t) e.numero].nom, page[(size_t) e.numero].valeur());
            else if (e.appuye)
                annoncer (nomDeLEncodeur (e.numero), "touché"_fr);
            break;

        case Evenement::Type::bandeTouchee:
            if (e.appuye)
                annoncer ("Bande tactile", "touchée"_fr);
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
        auto& r = page[(size_t) encodeur];

        if (r.tourner)
        {
            r.tourner (pas, fin);
            annoncer (r.nom, r.valeur());
        }
        else
        {
            annoncer (nomDeLEncodeur (encodeur), "Libre pour l'instant");
        }

        return;
    }

    switch ((Encodeur) encodeur)
    {
        case Encodeur::tempo:
            moteur.modifier ([=] (Morceau& m) { m.tempo += pas * (fin ? 0.1 : 1.0); });
            annoncer ("Tempo", formaterTempo (moteur.getMorceau().tempo));
            break;

        case Encodeur::master:
            moteur.modifier ([=] (Morceau& m) { m.volumeGeneralDb += (float) pas * (fin ? 0.05f : 0.25f); });
            annoncer ("Volume général"_fr, formaterDb (moteur.getMorceau().volumeGeneralDb));
            break;

        case Encodeur::swing:
            annoncer ("Swing", "Arrive à l'étape 5"_fr);
            break;

        default:
            break;
    }
}

void Pilote::appuyer (int bouton)
{
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
                moteur.modifier ([t] (Morceau& m) { m.tempo = std::round (*t); });

            annoncer ("Tap Tempo", formaterTempo (moteur.getMorceau().tempo));
            return;

        case Bouton::shift:
            annoncer ("Shift", "Réglage fin des encodeurs"_fr);
            return;

        default:
            break;
    }

    annoncer (nomDuBouton (bouton), "Prévu à une étape suivante"_fr);
}

//==============================================================================
EtatSurface Pilote::calculerEtat() const
{
    EtatSurface etat;

    for (int i = 0; i < 64; ++i)
    {
        const int v = velocites[(size_t) i];
        etat.pads[(size_t) i] = palette::indexDePiste (i % 8, v > 0 ? 1 + juce::jmin (2, v / 43) : 0);
    }

    const auto p = moteur.getPosition();
    const bool debutDeTemps = p.enLecture && p.fraction < 0.15;

    etat.boutons[(size_t) Bouton::play] = p.enLecture ? palette::vert : palette::grisClair;
    etat.boutons[(size_t) Bouton::metronome] = moteur.getMorceau().clic.actif ? palette::blancFort : palette::blancFaible;
    etat.boutons[(size_t) Bouton::tapTempo] = debutDeTemps ? palette::blancFort : palette::blancFaible;
    etat.boutons[(size_t) Bouton::shift] = surface.estAppuye (Bouton::shift) ? palette::blancFort : palette::blancFaible;

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
    g.setColour (morceau.clic.actif ? premier : juce::Colour (0xff2a2d35));
    g.fillRoundedRectangle (zoneClic, 6.0f);
    g.setColour (morceau.clic.actif ? fond : discret);
    g.setFont (juce::FontOptions (22.0f, juce::Font::bold));
    g.drawText (morceau.clic.actif ? juce::String ("Clic") : juce::String ("Clic coupé"_fr), zoneClic, juce::Justification::centred);

    g.setColour (juce::Colour (0xff2a2d35));
    g.fillRect (0.0f, 50.0f, largeur, 2.0f);

    // Annonce du mode d'essai, par-dessus les colonnes
    if (juce::Time::getMillisecondCounterHiRes() < annonce.fin)
    {
        g.setColour (texte);
        g.setFont (juce::FontOptions (30.0f));
        g.drawText (annonce.titre, juce::Rectangle<float> (0.0f, 56.0f, largeur, 44.0f), juce::Justification::centred);
        g.setFont (juce::FontOptions (44.0f, juce::Font::bold));
        g.drawText (annonce.valeur, juce::Rectangle<float> (0.0f, 100.0f, largeur, 56.0f), juce::Justification::centred);
        return;
    }

    // 8 colonnes alignées sur les encodeurs : nom, valeur, petit graphique
    const float colonne = largeur / 8.0f;

    for (int i = 0; i < 8; ++i)
    {
        const auto& r = page[(size_t) i];
        const float x = i * colonne;

        if (! r.valeur)
            continue;

        g.setColour (touches[(size_t) i] ? texte : discret);
        g.setFont (juce::FontOptions (21.0f));
        g.drawFittedText (r.nom, juce::Rectangle<float> (x + 4.0f, 58.0f, colonne - 8.0f, 28.0f).toNearestInt(), juce::Justification::centred, 1, 0.7f);

        // La valeur rétrécit plutôt que d'être coupée (« 120 BPM », « 2 mesures »)
        g.setColour (texte);
        g.setFont (juce::FontOptions (30.0f, juce::Font::bold));
        g.drawFittedText (r.valeur(), juce::Rectangle<float> (x + 3.0f, 88.0f, colonne - 6.0f, 38.0f).toNearestInt(), juce::Justification::centred, 1, 0.6f);

        const auto barre = juce::Rectangle<float> (x + 14.0f, 136.0f, colonne - 28.0f, 10.0f);
        g.setColour (juce::Colour (0xff2a2d35));
        g.fillRoundedRectangle (barre, 4.0f);
        g.setColour (touches[(size_t) i] ? premier : actif);
        g.fillRoundedRectangle (barre.withWidth (barre.getWidth() * juce::jlimit (0.0f, 1.0f, r.proportion())), 4.0f);
    }
}

void Pilote::rafraichir()
{
    dessinerEcran (ecran);
    surface.publier (calculerEtat(), ecran);
}

} // namespace groovebox
