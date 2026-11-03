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

    // Page Device : le pad choisi de la batterie, puis le kit entier (encodeurs 7 et 8)
    auto pad = [this] () -> const PadBatterie& { return moteur.getMorceau().pistes[(size_t) pisteChoisie].kit.pads[(size_t) padChoisi]; };
    auto kit = [this] () -> const Kit& { return moteur.getMorceau().pistes[(size_t) pisteChoisie].kit; };
    auto pourcent = [] (float v) { return juce::String (juce::roundToInt (v * 100.0f)) + " %"; };

    pageDevice[0] = { "Hauteur", [pad] { const auto h = pad().hauteur; return (h > 0.04f ? "+" : "") + juce::String (h, 1).replaceCharacter ('.', ',') + " dt"; },
                      [pad] { return (pad().hauteur + 24.0f) / 48.0f; },
                      [this] (int pas, bool fin) { modifierPad (0, [=] (PadBatterie& p) { p.hauteur = juce::jlimit (-24.0f, 24.0f, p.hauteur + (float) pas * (fin ? 0.01f : 0.1f)); }); }, actif };
    pageDevice[1] = { "Décroissance"_fr, [pad, pourcent] { return pourcent (pad().decroissance); }, [pad] { return pad().decroissance; },
                      [this] (int pas, bool fin) { modifierPad (1, [=] (PadBatterie& p) { p.decroissance = juce::jlimit (0.0f, 1.0f, p.decroissance + (float) pas * (fin ? 0.001f : 0.005f)); }); }, actif };
    pageDevice[2] = { "Saturation", [pad, pourcent] { return pourcent (pad().saturation); }, [pad] { return pad().saturation; },
                      [this] (int pas, bool fin) { modifierPad (2, [=] (PadBatterie& p) { p.saturation = juce::jlimit (0.0f, 1.0f, p.saturation + (float) pas * (fin ? 0.001f : 0.005f)); }); }, actif };
    pageDevice[3] = { "Couleur",
                      [pad] { const auto c = pad().couleur; return std::abs (c) < 0.005f ? juce::String ("Neutre") : (c < 0 ? "Sombre " : "Brillant ") + juce::String (juce::roundToInt (std::abs (c) * 100.0f)); },
                      [pad] { return (pad().couleur + 1.0f) / 2.0f; },
                      [this] (int pas, bool fin) { modifierPad (3, [=] (PadBatterie& p) { p.couleur = juce::jlimit (-1.0f, 1.0f, p.couleur + (float) pas * (fin ? 0.002f : 0.01f)); }); }, actif };
    pageDevice[4] = { "Volume", [pad] { return formaterDb (pad().volumeDb); }, [pad] { return (pad().volumeDb + 40.0f) / 46.0f; },
                      [this] (int pas, bool fin) { modifierPad (4, [=] (PadBatterie& p) { p.volumeDb = juce::jlimit (-40.0f, 6.0f, p.volumeDb + (float) pas * (fin ? 0.05f : 0.25f)); }); }, actif };
    pageDevice[5] = { "Étouffement"_fr, [pad] { return pad().etouffement == 0 ? juce::String ("Aucun") : "Groupe " + juce::String (pad().etouffement); },
                      [pad] { return (float) pad().etouffement / 4.0f; },
                      [this] (int pas, bool) { if (auto c = crantsDiscrets (5, pas)) modifierPad (5, [=] (PadBatterie& p) { p.etouffement = juce::jlimit (0, 4, p.etouffement + c); }); }, actif };
    pageDevice[6] = { "Compression", [kit, pourcent] { return pourcent (kit().compression); }, [kit] { return kit().compression; },
                      [this] (int pas, bool fin) { if (pisteChoisieEstUneBatterie()) moteur.modifier ([=, p = pisteChoisie] (Morceau& m) { auto& k = m.pistes[(size_t) p].kit; k.compression = juce::jlimit (0.0f, 1.0f, k.compression + (float) pas * (fin ? 0.001f : 0.005f)); }, "encodeur 6"); },
                      premier };
    pageDevice[7] = { "Largeur", [kit, pourcent] { return pourcent (kit().largeur); }, [kit] { return kit().largeur; },
                      [this] (int pas, bool fin) { if (pisteChoisieEstUneBatterie()) moteur.modifier ([=, p = pisteChoisie] (Morceau& m) { auto& k = m.pistes[(size_t) p].kit; k.largeur = juce::jlimit (0.0f, 1.0f, k.largeur + (float) pas * (fin ? 0.001f : 0.005f)); }, "encodeur 7"); },
                      premier };

    // Page Générateur, moteur euclidien : une voix à la fois
    auto gen = [this] () -> Generateur { const auto* g = generateurVise(); return g != nullptr ? *g : Generateur::parDefaut (MoteurRythmique::euclidien, moteur.getMorceau().mesure); };
    auto voixE = [this, gen] { return gen().voix[(size_t) voixEuclidienne]; };
    static const juce::StringArray nomsDesVoix { "Grosse caisse", "Caisse claire", "Charleston", "Clap" };
    const auto nPas = [this] { return generateurs::pasParMesure (moteur.getMorceau().mesure); };

    pageEuclide[0] = { "Voix", [] { return juce::String(); }, [this] { return (float) voixEuclidienne / 3.0f; },
                       [this] (int pas, bool) { if (auto c = crantsDiscrets (0, pas)) voixEuclidienne = juce::jlimit (0, 3, voixEuclidienne + c); }, premier };
    pageEuclide[0].valeur = [this] { return nomsDesVoix[voixEuclidienne]; };
    pageEuclide[1] = { "Coups", [voixE] { return juce::String (voixE().coups); }, [voixE, nPas] { return (float) voixE().coups / (float) nPas(); },
                       [this, nPas] (int pas, bool) { if (auto c = crantsDiscrets (1, pas)) modifierGenerateur (1, [=, v = voixEuclidienne] (Generateur& g) { auto& x = g.voix[(size_t) v]; x.coups = juce::jlimit (0, nPas(), x.coups + c); x.accents = juce::jmin (x.accents, x.coups); }); }, actif };
    pageEuclide[2] = { "Rotation", [voixE] { return juce::String (voixE().rotation); }, [voixE, nPas] { return (float) voixE().rotation / (float) juce::jmax (1, nPas() - 1); },
                       [this, nPas] (int pas, bool) { if (auto c = crantsDiscrets (2, pas)) modifierGenerateur (2, [=, v = voixEuclidienne] (Generateur& g) { auto& x = g.voix[(size_t) v]; x.rotation = ((x.rotation + c) % nPas() + nPas()) % nPas(); }); }, actif };
    pageEuclide[3] = { "Accents", [voixE] { return juce::String (voixE().accents); }, [voixE] { return voixE().coups == 0 ? 0.0f : (float) voixE().accents / (float) voixE().coups; },
                       [this] (int pas, bool) { if (auto c = crantsDiscrets (3, pas)) modifierGenerateur (3, [=, v = voixEuclidienne] (Generateur& g) { auto& x = g.voix[(size_t) v]; x.accents = juce::jlimit (0, x.coups, x.accents + c); }); }, actif };
    pageEuclide[4] = { "Probabilité"_fr, [voixE, pourcent] { return pourcent (voixE().probabilite); }, [voixE] { return voixE().probabilite; },
                       [this] (int pas, bool fin) { modifierGenerateur (4, [=, v = voixEuclidienne] (Generateur& g) { auto& x = g.voix[(size_t) v]; x.probabilite = juce::jlimit (0.0f, 1.0f, x.probabilite + (float) pas * (fin ? 0.002f : 0.01f)); }); }, actif };
    pageEuclide[5] = { "Humanisation", [gen, pourcent] { return pourcent (gen().humanisation); }, [gen] { return gen().humanisation; },
                       [this] (int pas, bool fin) { modifierGenerateur (5, [=] (Generateur& g) { g.humanisation = juce::jlimit (0.0f, 1.0f, g.humanisation + (float) pas * (fin ? 0.002f : 0.01f)); }); }, actif };
    pageEuclide[6] = { "Roulement", [gen] { return gen().roulement == 0 ? juce::String ("Aucun") : "Toutes les " + juce::String (gen().roulement); },
                       [gen] { return (float) gen().roulement / 8.0f; },
                       [this] (int pas, bool) { if (auto c = crantsDiscrets (6, pas)) modifierGenerateur (6, [=] (Generateur& g) {
                           static constexpr int choix[] = { 0, 2, 4, 8 };
                           int i = 0; while (i < 3 && choix[i] != g.roulement) ++i;
                           g.roulement = choix[juce::jlimit (0, 3, i + c)]; }); }, actif };
    pageEuclide[7] = { "Moteur", [gen] { return nomDuMoteur (gen().moteur); }, [gen] { return (float) gen().moteur / 2.0f; },
                       [this] (int pas, bool) { if (auto c = crantsDiscrets (7, pas)) modifierGenerateur (7, [=, m = moteur.getMorceau().mesure] (Generateur& g) {
                           auto suivant = (MoteurRythmique) juce::jlimit (0, 2, (int) g.moteur + c);
                           if (suivant == MoteurRythmique::grooves && ! generateurs::carteDeGroovesDisponible (m))
                               suivant = c > 0 ? MoteurRythmique::maison : MoteurRythmique::euclidien;
                           g.moteur = suivant; }); }, premier };

    // Page Générateur, cartes (grooves en 4/4, maison ailleurs)
    pageCartes[0] = { "X", [gen, pourcent] { return pourcent (gen().x); }, [gen] { return gen().x; },
                      [this] (int pas, bool fin) { modifierGenerateur (0, [=] (Generateur& g) { g.x = juce::jlimit (0.0f, 1.0f, g.x + (float) pas * (fin ? 0.001f : 0.005f)); }); }, actif };
    pageCartes[1] = { "Y", [gen, pourcent] { return pourcent (gen().y); }, [gen] { return gen().y; },
                      [this] (int pas, bool fin) { modifierGenerateur (1, [=] (Generateur& g) { g.y = juce::jlimit (0.0f, 1.0f, g.y + (float) pas * (fin ? 0.001f : 0.005f)); }); }, actif };

    for (int v = 0; v < 3; ++v)
        pageCartes[(size_t) (2 + v)] = { "Densité "_fr + juce::String (v == 0 ? "GC" : v == 1 ? "CC" : "CH"),
                                         [gen, pourcent, v] { return pourcent (gen().densites[(size_t) v]); }, [gen, v] { return gen().densites[(size_t) v]; },
                                         [this, v] (int pas, bool fin) { modifierGenerateur (2 + v, [=] (Generateur& g) { auto& d = g.densites[(size_t) v]; d = juce::jlimit (0.0f, 1.0f, d + (float) pas * (fin ? 0.001f : 0.005f)); }); }, actif };

    pageCartes[5] = { "Chaos", [gen, pourcent] { return pourcent (gen().chaos); }, [gen] { return gen().chaos; },
                      [this] (int pas, bool fin) { modifierGenerateur (5, [=] (Generateur& g) { g.chaos = juce::jlimit (0.0f, 1.0f, g.chaos + (float) pas * (fin ? 0.002f : 0.01f)); }); }, actif };
    pageCartes[6] = pageEuclide[5];
    pageCartes[7] = pageEuclide[7];

    // Page Commun : swing de la piste, roulement, graine, longueur du clip
    pageCommun[0] = { "Swing piste", [this] { const auto& p = moteur.getMorceau().pistes[(size_t) pisteChoisie];
                                              return p.swing ? juce::String (juce::roundToInt (*p.swing * 100.0f)) + " %" : juce::String ("Global"); },
                      [this] { return moteur.getMorceau().swingDeLaPiste (pisteChoisie) / 0.75f; },
                      [this] (int pas, bool fin) { moteur.modifier ([=, p = pisteChoisie] (Morceau& m) { auto& s = m.pistes[(size_t) p].swing; s = juce::jlimit (0.0f, 0.75f, s.value_or (m.swing) + (float) pas * (fin ? 0.001f : 0.005f)); }, "encodeur commun 0"); }, premier };
    pageCommun[1] = pageEuclide[5];
    pageCommun[2] = pageEuclide[6];
    pageCommun[3] = { "Graine", [gen] { return juce::String ((juce::int64) gen().graine); }, [] { return 0.0f; },
                      [this] (int pas, bool) { if (auto c = crantsDiscrets (3, pas)) modifierGenerateur (3, [=] (Generateur& g) { g.graine = (juce::uint32) juce::jmax (1, (int) g.graine + c); }); }, actif };
    pageCommun[4] = { "Longueur", [this] { const auto c = caseDuPasAPas(); const auto* clip = c ? moteur.getMorceau().clip (c->first, c->second) : nullptr;
                                           return clip ? juce::String (clip->longueurMesures) + (clip->longueurMesures > 1 ? " mesures" : " mesure") : juce::String(); },
                      [] { return 0.0f; },
                      [this] (int pas, bool) { if (auto c = crantsDiscrets (4, pas)) modifierClipVise (4, [=] (Clip& clip) {
                          const auto& l = Clip::longueursPossibles;
                          int i = 0; while (i < 4 && l[(size_t) i] != clip.longueurMesures) ++i;
                          clip.longueurMesures = l[(size_t) juce::jlimit (0, 4, i + c)]; }); }, actif };

    // Page Variation (bouton Clip) : ce que fera Shift + Duplicate
    auto variation = [this] () -> const Variation& { return moteur.getMorceau().variation; };
    pageVariation[0] = { "Quantité"_fr, [variation, pourcent] { return pourcent (variation().quantite); }, [variation] { return variation().quantite; },
                         [this] (int pas, bool) { moteur.modifier ([=] (Morceau& m) { m.variation.quantite = juce::jlimit (0.0f, 1.0f, m.variation.quantite + (float) pas * 0.005f); }, "variation 0"); }, premier };
    pageVariation[1] = { "Densité"_fr, [variation] { const auto d = juce::roundToInt (variation().densite * 100.0f); return (d > 0 ? "+" : "") + juce::String (d) + " %"; },
                         [variation] { return (variation().densite + 1.0f) / 2.0f; },
                         [this] (int pas, bool) { moteur.modifier ([=] (Morceau& m) { m.variation.densite = juce::jlimit (-1.0f, 1.0f, m.variation.densite + (float) pas * 0.01f); }, "variation 1"); }, premier };
    pageVariation[2] = { "Rotation", [variation] { return juce::String (variation().rotation); }, [] { return 0.0f; },
                         [this] (int pas, bool) { if (auto c = crantsDiscrets (2, pas)) moteur.modifier ([=] (Morceau& m) { m.variation.rotation = juce::jlimit (-16, 16, m.variation.rotation + c); }, "variation 2"); }, premier };
    pageVariation[3] = { "Inversion", [variation] { return variation().inversion ? juce::String ("Oui") : juce::String ("Non"); }, [variation] { return variation().inversion ? 1.0f : 0.0f; },
                         [this] (int pas, bool) { if (auto c = crantsDiscrets (3, pas)) moteur.modifier ([=] (Morceau& m) { m.variation.inversion = c > 0; }, "variation 3"); }, premier };

    // Page Browse : catégorie, puis son (ou kit entier) pour le pad choisi, avec préécoute
    pageBrowse[0] = { "Catégorie"_fr, [this] { return categoriesDuNavigateur[categorieChoisie]; }, [this] { return categoriesDuNavigateur.isEmpty() ? 0.0f : (float) categorieChoisie / (float) juce::jmax (1, categoriesDuNavigateur.size() - 1); },
                      [this] (int pas, bool) { if (auto c = crantsDiscrets (0, pas)) { categorieChoisie = juce::jlimit (0, juce::jmax (0, categoriesDuNavigateur.size() - 1), categorieChoisie + c); sonChoisi = 0; } }, premier };
    pageBrowse[1] = { "Son", [this] { const auto s = sonsDeLaCategorie(); return s.isEmpty() ? juce::String ("Dossier vide") : juce::File (s[sonChoisi]).getFileNameWithoutExtension(); },
                      [this] { const auto s = sonsDeLaCategorie(); return s.size() < 2 ? 0.0f : (float) sonChoisi / (float) (s.size() - 1); },
                      [this] (int pas, bool) { if (auto c = crantsDiscrets (1, pas)) { sonChoisi = juce::jlimit (0, juce::jmax (0, sonsDeLaCategorie().size() - 1), sonChoisi + c); choisirDansLeNavigateur(); } }, actif };

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

const Generateur* Pilote::generateurVise() const
{
    const auto c = caseDuPasAPas();
    const auto* clip = c ? moteur.getMorceau().clip (c->first, c->second) : nullptr;
    return clip != nullptr && clip->generateur ? &*clip->generateur : nullptr;
}

void Pilote::modifierClipVise (int encodeur, std::function<void (Clip&)> f)
{
    if (! pisteChoisieEstUneBatterie())
        return;

    auto cible = caseDuPasAPas();
    const int scene = cible ? cible->second : (caseChoisie && caseChoisie->first == pisteChoisie ? caseChoisie->second : 0);

    moteur.modifier ([=, p = pisteChoisie] (Morceau& m)
    {
        m.assurerScenes (scene + 1);
        auto& c = m.scenes[(size_t) scene].clips[(size_t) p];

        if (! c)
            c = Clip { "Rythme", 1, {} };

        f (*c);
        c->regenerer (m.mesure);
    }, "clip " + juce::String (encodeur));

    if (! cible)
        caseChoisie = std::pair { pisteChoisie, scene };
}

void Pilote::modifierGenerateur (int encodeur, std::function<void (Generateur&)> f)
{
    const bool devientGenere = generateurVise() == nullptr;

    modifierClipVise (encodeur, [=, m = moteur.getMorceau().mesure] (Clip& c)
    {
        // Un clip écrit à la main devient généré (Undo pour revenir)
        if (! c.generateur)
        {
            // Départ sur une carte : X, Y et une densité suffisent pour un rythme jouable
            c.generateur = Generateur::parDefaut (generateurs::carteDeGroovesDisponible (m) ? MoteurRythmique::grooves : MoteurRythmique::maison, m);
            c.nom = "Généré"_fr;
        }

        f (*c.generateur);
        c.nom = nomDuMoteur (c.generateur->moteur);
    });

    if (devientGenere)
        annoncer ("Générateur"_fr, "Le clip est maintenant généré (Undo pour revenir)"_fr);
}

void Pilote::appliquerVariation (Clip& c, Morceau& m)
{
    const auto graine = m.prochaineGraine++;

    if (c.generateur)
    {
        // Clip généré : nouvelle graine, densité décalée ; il reste réglable
        auto& g = *c.generateur;
        g.graine = graine;

        for (auto& d : g.densites)
            d = juce::jlimit (0.0f, 1.0f, d + m.variation.densite * 0.2f);

        for (auto& v : g.voix)
        {
            v.coups = juce::jlimit (0, generateurs::pasParMesure (m.mesure), v.coups + juce::roundToInt (m.variation.densite * 2.0f));
            v.rotation += m.variation.rotation;
        }

        g.chaos = juce::jmax (g.chaos, m.variation.quantite * 0.5f);
        c.regenerer (m.mesure);
    }
    else
    {
        c.notes = generateurs::varier (c.notes, m.variation, graine, m.mesure, c.longueurMesures);
    }

    c.nom = c.nom.upToFirstOccurrenceOf (" var.", false, false) + " var.";
}

void Pilote::figerLeClip()
{
    const auto c = mode == Mode::note ? caseDuPasAPas() : caseChoisie;

    if (! c || moteur.getMorceau().clip (c->first, c->second) == nullptr || ! moteur.getMorceau().clip (c->first, c->second)->generateur)
    {
        annoncer ("Convert", "Aucun clip généré choisi"_fr);
        return;
    }

    moteur.modifier ([c] (Morceau& m) { m.scenes[(size_t) c->second].clips[(size_t) c->first]->figer(); });
    annoncer ("Convert", "Clip figé : notes retouchables au pas à pas"_fr);
}

bool Pilote::pisteChoisieEstUneBatterie() const
{
    return moteur.getMorceau().pistes[(size_t) pisteChoisie].estUneBatterie();
}

void Pilote::modifierPad (int encodeur, std::function<void (PadBatterie&)> f)
{
    if (! pisteChoisieEstUneBatterie())
        return;

    moteur.modifier ([=, p = pisteChoisie, k = padChoisi] (Morceau& m) { f (m.pistes[(size_t) p].kit.pads[(size_t) k]); },
                     "encodeur " + juce::String (encodeur));
}

juce::StringArray Pilote::sonsDeLaCategorie() const
{
    const auto categorie = categoriesDuNavigateur[categorieChoisie];

    if (categorie == "Kits")
        return kitsDeDepart::noms();

    return moteur.getBanque().sons (categorie);
}

void Pilote::choisirDansLeNavigateur()
{
    if (! pisteChoisieEstUneBatterie())
        return;

    const auto sons = sonsDeLaCategorie();

    if (sons.isEmpty())
        return;

    const auto choix = sons[sonChoisi];

    if (categoriesDuNavigateur[categorieChoisie] == "Kits")
        moteur.modifier ([=, p = pisteChoisie] (Morceau& m) { m.pistes[(size_t) p].kit = kitsDeDepart::kit (choix); }, "navigateur");
    else
        moteur.modifier ([=, p = pisteChoisie, k = padChoisi] (Morceau& m) { m.pistes[(size_t) p].kit.pads[(size_t) k].son = choix; }, "navigateur");

    // Préécoute : le pad choisi, tout de suite
    moteur.jouerNote (pisteChoisie, Kit::premiereNote + padChoisi, 110);
}

double Pilote::longueurDUnPas() const
{
    const auto& mesure = moteur.getMorceau().mesure;
    return (double) mesure.denominateur / (double) mesure.grilleParDefaut();
}

int Pilote::nombreDePas() const
{
    const auto c = caseDuPasAPas();
    const auto* clip = c ? moteur.getMorceau().clip (c->first, c->second) : nullptr;
    const auto longueur = clip != nullptr ? clip->longueurMesures : 1;
    return juce::roundToInt (longueur * moteur.getMorceau().mesure.numerateur / longueurDUnPas());
}

std::optional<std::pair<int, int>> Pilote::caseDuPasAPas() const
{
    const auto& m = moteur.getMorceau();

    if (const int s = moteur.sceneJouee (pisteChoisie); s >= 0)
        return std::pair { pisteChoisie, s };

    if (caseChoisie && caseChoisie->first == pisteChoisie && m.clip (pisteChoisie, caseChoisie->second) != nullptr)
        return caseChoisie;

    for (int s = 0; s < (int) m.scenes.size(); ++s)
        if (m.clip (pisteChoisie, s) != nullptr)
            return std::pair { pisteChoisie, s };

    return std::nullopt;
}

void Pilote::basculerPas (int pas)
{
    auto cible = caseDuPasAPas();

    // Pas de clip sur la piste : on en crée un d'une mesure, dans la case choisie (ou la première scène)
    if (! cible)
    {
        const int scene = caseChoisie && caseChoisie->first == pisteChoisie ? caseChoisie->second : 0;
        moteur.modifier ([=, p = pisteChoisie] (Morceau& m) { m.assurerScenes (scene + 1); m.scenes[(size_t) scene].clips[(size_t) p] = Clip { "Rythme", 1, {} }; });
        cible = std::pair { pisteChoisie, scene };
        caseChoisie = cible;
    }

    if (pas >= nombreDePas())
        return;

    const auto [piste, scene] = *cible;
    const auto longueur = longueurDUnPas();
    const auto debut = pas * longueur;
    const int note = Kit::premiereNote + padChoisi;
    const int velocite = tenu (Bouton::accent) ? 127 : 100;

    moteur.modifier ([=] (Morceau& m)
    {
        auto& notes = m.scenes[(size_t) scene].clips[(size_t) piste]->notes;
        auto dansLePas = [&] (const Note& n) { return n.hauteur == note && n.debut > debut - 1.0e-6 && n.debut < debut + longueur - 1.0e-6; };
        const auto avant = notes.size();
        notes.erase (std::remove_if (notes.begin(), notes.end(), dansLePas), notes.end());

        if (notes.size() == avant)
            notes.push_back ({ debut, longueur * 0.9, note, velocite });
    });
}

void Pilote::appuyerPadDeNote (int ligne, int colonne, int velocite)
{
    // En bas à gauche : les 16 pads de la batterie ; en haut : 32 pas du pas à pas
    if (ligne < 4 && colonne < 4)
    {
        const int pad = ligne * 4 + colonne;
        const bool nouveau = pad != padChoisi;
        padChoisi = pad;
        padsAppuyes[(size_t) pad] = true;
        moteur.jouerNote (pisteChoisie, Kit::premiereNote + pad, tenu (Bouton::accent) ? 127 : velocite);

        if (nouveau)
            annoncer (kitsDeDepart::nomsDesPads()[(size_t) pad], juce::File (moteur.getMorceau().pistes[(size_t) pisteChoisie].kit.pads[(size_t) pad].son).getFileNameWithoutExtension());

        return;
    }

    if (ligne >= 4)
        basculerPas (pageDePas * 32 + (7 - ligne) * 8 + colonne);
}

//==============================================================================
void Pilote::traiter (const Evenement& e)
{
    switch (e.type)
    {
        case Evenement::Type::pad:
            if (mode == Mode::note)
            {
                if (e.appuye)
                    appuyerPadDeNote (e.ligne(), e.colonne(), e.valeur);
                else if (e.ligne() < 4 && e.colonne() < 4)
                {
                    padsAppuyes[(size_t) (e.ligne() * 4 + e.colonne())] = false;
                    moteur.relacherNote (pisteChoisie, Kit::premiereNote + e.ligne() * 4 + e.colonne());
                }
            }
            else if (e.appuye)
            {
                appuyerPad (e.ligne(), e.colonne(), e.valeur);
            }
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
            if (fin)
            {
                // Shift : le swing propre à la piste choisie
                moteur.modifier ([=, p = pisteChoisie] (Morceau& m) { auto& s = m.pistes[(size_t) p].swing; s = juce::jlimit (0.0f, 0.75f, s.value_or (m.swing) + (float) pas * 0.005f); }, "encodeur swing piste");
                annoncer ("Swing de " + moteur.getMorceau().pistes[(size_t) pisteChoisie].nom, juce::String (juce::roundToInt (moteur.getMorceau().swingDeLaPiste (pisteChoisie) * 100.0f)) + " %");
            }
            else
            {
                moteur.modifier ([=] (Morceau& m) { m.swing = juce::jlimit (0.0f, 0.75f, m.swing + (float) pas * 0.005f); }, "encodeur swing");
                annoncer ("Swing", juce::String (juce::roundToInt (moteur.getMorceau().swing * 100.0f)) + " %");
            }
            break;

        default:
            break;
    }
}

//==============================================================================
void Pilote::appuyerPad (int ligne, int colonne, int)
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
    const bool varier = varierEnDupliquant;

    moteur.modifier ([=, this] (Morceau& m)
    {
        int cible = scene + 1;

        while (cible < (int) m.scenes.size() && m.scenes[(size_t) cible].clips[(size_t) piste].has_value())
            ++cible;

        m.assurerScenes (cible + 1);
        m.scenes[(size_t) cible].clips[(size_t) piste] = m.scenes[(size_t) scene].clips[(size_t) piste];

        if (varier && m.scenes[(size_t) cible].clips[(size_t) piste])
            appliquerVariation (*m.scenes[(size_t) cible].clips[(size_t) piste], m);
    });

    annoncer (varier ? juce::String ("Dupliqué et varié"_fr) : juce::String ("Clip dupliqué"_fr), moteur.getMorceau().pistes[(size_t) piste].nom);
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

    if (mode == Mode::note && ! p.estUneBatterie())
        mode = Mode::session;

    pageDePas = 0;
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
            varierEnDupliquant = shift();

            // En mode Note : Shift + Duplicate vise tout de suite le clip du pas à pas
            if (mode == Mode::note)
                sourceDeCopie = caseDuPasAPas();
            return;

        case Bouton::convert:
            figerLeClip();
            return;

        case Bouton::clip:
            page = Page::variation;
            annoncer ("Clip", "Réglages de Shift + Duplicate (dupliquer et varier)"_fr);
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
            if (mode == Mode::note && caseDuPasAPas())
                caseChoisie = caseDuPasAPas();

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

        case Bouton::note:
            if (pisteChoisieEstUneBatterie())
            {
                mode = Mode::note;
                page = Page::device;
                annoncer ("Note", "Batterie : pads en bas à gauche, pas à pas en haut"_fr);
            }
            else
            {
                annoncer ("Note", "Les pads en gamme des synthés arrivent à l'étape 6"_fr);
            }
            return;

        case Bouton::session:
            mode = Mode::session;
            annoncer ("Session", "Pistes et scènes"_fr);
            return;

        case Bouton::device:
            if (! pisteChoisieEstUneBatterie())
            {
                annoncer ("Device", "Réglages des synthés à l'étape 6"_fr);
                return;
            }

            // Device fait tourner les pages : pad, générateur, réglages communs
            page = page == Page::device ? Page::generateur : page == Page::generateur ? Page::commun : Page::device;
            annoncer ("Device", page == Page::device ? kitsDeDepart::nomsDesPads()[(size_t) padChoisi] + ", kit " + morceau.pistes[(size_t) pisteChoisie].kit.nom
                              : page == Page::generateur ? "Générateur "_fr + (generateurVise() ? nomDuMoteur (generateurVise()->moteur) : juce::String ("(le clip deviendra généré)"_fr))
                                                         : juce::String ("Swing, humanisation, roulement, graine, longueur"));
            return;

        case Bouton::browse:
            if (page == Page::browse)
            {
                page = Page::device;
                annoncer ("Browse", "Fermé"_fr);
                return;
            }

            categoriesDuNavigateur = moteur.getBanque().categories();
            categoriesDuNavigateur.insert (0, "Kits");
            categorieChoisie = juce::jlimit (0, categoriesDuNavigateur.size() - 1, categorieChoisie);
            page = Page::browse;
            annoncer ("Browse", pisteChoisieEstUneBatterie() ? "Encodeur 1 : catégorie ; encodeur 2 : son"_fr : juce::String ("Choisis d'abord une piste batterie"));
            return;

        case Bouton::pageGauche:
        case Bouton::pageDroite:
            if (mode == Mode::note)
            {
                const int pages = juce::jmax (1, (nombreDePas() + 31) / 32);
                pageDePas = juce::jlimit (0, pages - 1, pageDePas + ((Bouton) bouton == Bouton::pageDroite ? 1 : -1));
                annoncer ("Pas à pas"_fr, "Page " + juce::String (pageDePas + 1) + " sur " + juce::String (pages));
            }
            return;

        case Bouton::accent:
            return;     // se tient, avec un pad ou un pas

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

    if (mode == Mode::note)
    {
        // Pads de la batterie (en bas à gauche) et pas à pas (en haut)
        const auto& kit = morceau.pistes[(size_t) pisteChoisie].kit;

        for (int pad = 0; pad < 16; ++pad)
            etat.pads[(size_t) ((pad / 4) * 8 + pad % 4)] = kit.pads[(size_t) pad].son.isEmpty() ? palette::noir
                                                           : palette::indexDePiste (pisteChoisie, padsAppuyes[(size_t) pad] ? 3 : pad == padChoisi ? 2 : 1);

        const auto cible = caseDuPasAPas();
        const auto* clip = cible ? morceau.clip (cible->first, cible->second) : nullptr;
        const auto longueur = longueurDUnPas();
        const int total = nombreDePas();
        const auto position = moteur.positionDansLeClip (pisteChoisie);
        const int pasEnCours = position ? (int) (*position / longueur) : -1;
        const int note = Kit::premiereNote + padChoisi;

        for (int i = 0; i < 32; ++i)
        {
            const int pas = pageDePas * 32 + i;
            const int ligne = 7 - i / 8, colonne = i % 8;
            int index = palette::noir;

            if (pas < total)
            {
                index = palette::indexDePiste (pisteChoisie, 0);

                if (clip != nullptr)
                    for (auto& n : clip->notes)
                        if (n.hauteur == note && n.debut > pas * longueur - 1.0e-6 && n.debut < (pas + 1) * longueur - 1.0e-6)
                            index = palette::indexDePiste (pisteChoisie, n.velocite >= 120 ? 3 : 2);

                if (pas == pasEnCours)
                    index = palette::blanc;
            }

            etat.pads[(size_t) (ligne * 8 + colonne)] = index;
        }
    }
    else
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
    blanc (Bouton::session, mode == Mode::session);
    blanc (Bouton::note, mode == Mode::note);
    blanc (Bouton::accent, tenu (Bouton::accent));

    if (morceau.pistes[(size_t) pisteChoisie].estUneBatterie())
    {
        blanc (Bouton::device, page == Page::device || page == Page::generateur || page == Page::commun);
        blanc (Bouton::clip, page == Page::variation);
        blanc (Bouton::convert, generateurVise() != nullptr);
        blanc (Bouton::browse, page == Page::browse);
    }

    if (mode == Mode::note)
    {
        const int pages = (nombreDePas() + 31) / 32;

        if (pageDePas > 0)
            blanc (Bouton::pageGauche, false);

        if (pageDePas + 1 < pages)
            blanc (Bouton::pageDroite, false);
    }

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
