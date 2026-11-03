#include "Pilote.h"
#include "moteur/SonsDeSynthe.h"

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

    juce::String formaterDuree (double secondes)
    {
        const int s = (int) secondes;
        return juce::String (s / 60) + ":" + juce::String (s % 60).paddedLeft ('0', 2);
    }

    juce::String nomDeLaDuree (int mesures)
    {
        return juce::String (mesures) + (mesures == 1 ? " mesure" : " mesures");
    }

    juce::String decrire (const Enchainement& e)
    {
        return e.type == TypeEnchainement::coupe ? nomDeLEnchainement (e.type) : nomDeLEnchainement (e.type) + ", " + nomDeLaDuree (e.mesures);
    }
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

    // Page Mélodie d'une piste de synthé : le générateur de basses et de mélodies
    auto mel = [this] () -> GenerateurMelodique { const auto* g = melodieVisee(); return g != nullptr ? *g : GenerateurMelodique::parDefaut (StyleMelodique::basseDub); };
    auto continu = [this] (int encodeur, std::function<float& (GenerateurMelodique&)> champ)
    {
        return [this, encodeur, champ] (int pas, bool fin) { modifierMelodie (encodeur, [=] (GenerateurMelodique& g) { auto& v = champ (g); v = juce::jlimit (0.0f, 1.0f, v + (float) pas * (fin ? 0.002f : 0.01f)); }); };
    };

    pageMelodie[0] = { "Densité"_fr, [mel, pourcent] { return pourcent (mel().densite); }, [mel] { return mel().densite; }, continu (0, [] (GenerateurMelodique& g) -> float& { return g.densite; }), actif };
    pageMelodie[1] = { "Registre", [mel] { return "Octave " + juce::String (mel().registre); }, [mel] { return (float) mel().registre / 6.0f; },
                       [this] (int pas, bool) { if (auto c = crantsDiscrets (1, pas)) modifierMelodie (1, [=] (GenerateurMelodique& g) { g.registre = juce::jlimit (0, 6, g.registre + c); }); }, actif };
    pageMelodie[2] = { "Mouvement", [mel, pourcent] { return pourcent (mel().mouvement); }, [mel] { return mel().mouvement; }, continu (2, [] (GenerateurMelodique& g) -> float& { return g.mouvement; }), actif };
    pageMelodie[3] = { "Longueur", [mel, pourcent] { return pourcent (mel().longueur); }, [mel] { return mel().longueur; }, continu (3, [] (GenerateurMelodique& g) -> float& { return g.longueur; }), actif };
    pageMelodie[4] = { "Accents", [mel, pourcent] { return pourcent (mel().accents); }, [mel] { return mel().accents; }, continu (4, [] (GenerateurMelodique& g) -> float& { return g.accents; }), actif };
    pageMelodie[5] = { "Répétition"_fr, [mel, pourcent] { return pourcent (mel().repetition); }, [mel] { return mel().repetition; }, continu (5, [] (GenerateurMelodique& g) -> float& { return g.repetition; }), actif };
    pageMelodie[6] = { "Glissés"_fr, [mel, pourcent] { return pourcent (mel().glisses); }, [mel] { return mel().glisses; }, continu (6, [] (GenerateurMelodique& g) -> float& { return g.glisses; }), actif };
    pageMelodie[7] = { "Style", [mel] { return nomDuStyle (mel().style); }, [mel] { return (float) mel().style / 6.0f; },
                       [this] (int pas, bool) { if (auto c = crantsDiscrets (7, pas)) modifierMelodie (7, [=] (GenerateurMelodique& g) {
                           // Changer de style reprend ses réglages de départ, en gardant la graine
                           const auto graine = g.graine;
                           g = GenerateurMelodique::parDefaut ((StyleMelodique) juce::jlimit (0, 6, (int) g.style + c));
                           g.graine = graine; }); }, premier };

    // Page Commun d'une piste de synthé : swing de la piste, graine de la mélodie, longueur du clip
    pageCommunSynthe[0] = pageCommun[0];
    pageCommunSynthe[1] = { "Graine", [mel] { return juce::String ((juce::int64) mel().graine); }, [] { return 0.0f; },
                            [this] (int pas, bool) { if (auto c = crantsDiscrets (1, pas)) modifierMelodie (1, [=] (GenerateurMelodique& g) { g.graine = (juce::uint32) juce::jmax (1, (int) g.graine + c); }); }, actif };
    pageCommunSynthe[2] = pageCommun[4];

    // Pages Mix : envois vers le delay et la réverbération, puis réglages des effets de départ et de la sortie
    for (int i = 0; i < nombreDePistes; ++i)
    {
        pageEnvoisDelay[(size_t) i] = { {}, [this, i] { return formaterDb (moteur.getMorceau().pistes[(size_t) i].envoiDelayDb); },
                                        [this, i] { return (moteur.getMorceau().pistes[(size_t) i].envoiDelayDb + 60.0f) / 66.0f; },
                                        [this, i] (int pas, bool fin) { moteur.modifier ([=] (Morceau& m) { auto& e = m.pistes[(size_t) i].envoiDelayDb; e = juce::jlimit (-60.0f, 6.0f, e + (float) pas * (fin ? 0.05f : 0.3f)); }, "envoi delay " + juce::String (i)); },
                                        palette::couleurDePiste (i) };
        pageEnvoisReverb[(size_t) i] = { {}, [this, i] { return formaterDb (moteur.getMorceau().pistes[(size_t) i].envoiReverbDb); },
                                         [this, i] { return (moteur.getMorceau().pistes[(size_t) i].envoiReverbDb + 60.0f) / 66.0f; },
                                         [this, i] (int pas, bool fin) { moteur.modifier ([=] (Morceau& m) { auto& e = m.pistes[(size_t) i].envoiReverbDb; e = juce::jlimit (-60.0f, 6.0f, e + (float) pas * (fin ? 0.05f : 0.3f)); }, "envoi reverb " + juce::String (i)); },
                                         palette::couleurDePiste (i) };
    }

    auto reglageDepart = [this, pourcent] (juce::String nom, std::function<float& (Morceau&)> champ, float maximum, int encodeur)
    {
        return Reglage { nom, [this, champ, pourcent, maximum] { auto m = moteur.getMorceau(); return pourcent (champ (m) / maximum); },
                         [this, champ, maximum] { auto m = moteur.getMorceau(); return champ (m) / maximum; },
                         [this, champ, maximum, encodeur] (int pas, bool fin) { moteur.modifier ([=] (Morceau& m) { auto& v = champ (m); v = juce::jlimit (0.0f, maximum, v + (float) pas * (fin ? 0.002f : 0.01f) * maximum); }, "depart " + juce::String (encodeur)); },
                         premier };
    };

    pageDeparts[0] = { "Delay temps", [this] { return DelayDub::nomsDesDivisions()[moteur.getMorceau().delay.division]; },
                       [this] { return (float) moteur.getMorceau().delay.division / 5.0f; },
                       [this] (int pas, bool) { if (auto c = crantsDiscrets (0, pas)) moteur.modifier ([=] (Morceau& m) { m.delay.division = juce::jlimit (0, 5, m.delay.division + c); }, "depart 0"); }, premier };
    pageDeparts[1] = reglageDepart ("Delay retour", [] (Morceau& m) -> float& { return m.delay.retour; }, 1.2f, 1);
    pageDeparts[1].valeur = [this] { return juce::String (juce::roundToInt (moteur.getMorceau().delay.retour * 100.0f)) + " %"; };
    pageDeparts[2] = reglageDepart ("Delay filtre", [] (Morceau& m) -> float& { return m.delay.filtre; }, 1.0f, 2);
    pageDeparts[3] = reglageDepart ("Delay grain", [] (Morceau& m) -> float& { return m.delay.saturation; }, 1.0f, 3);
    pageDeparts[4] = reglageDepart ("Rév. taille"_fr, [] (Morceau& m) -> float& { return m.reverb.taille; }, 1.0f, 4);
    pageDeparts[5] = reglageDepart ("Rév. durée"_fr, [] (Morceau& m) -> float& { return m.reverb.duree; }, 1.0f, 5);
    pageDeparts[6] = reglageDepart ("Rév. couleur"_fr, [] (Morceau& m) -> float& { return m.reverb.couleur; }, 1.0f, 6);
    pageDeparts[7] = reglageDepart ("Compression", [] (Morceau& m) -> float& { return m.compressionDeSortie; }, 1.0f, 7);

    // Page Enchaînement (bouton User) : l'enchaînement du morceau, pour les clips et les scènes sans enchaînement retenu
    auto changerEnchainement = [this] (int encodeur, std::function<void (Enchainement&)> f)
    {
        moteur.modifier ([f] (Morceau& m) { f (m.enchainement); }, "enchainement " + juce::String (encodeur));
    };

    pageEnchainement[0] = { "Type", [this] { return nomDeLEnchainement (moteur.getMorceau().enchainement.type); },
                            [this] { return (float) moteur.getMorceau().enchainement.type / (float) (nombreDeTypesDEnchainement - 1); },
                            [this, changerEnchainement] (int pas, bool) { if (auto c = crantsDiscrets (0, pas)) changerEnchainement (0, [c] (Enchainement& e) {
                                e.type = (TypeEnchainement) juce::jlimit (0, nombreDeTypesDEnchainement - 1, (int) e.type + c); }); }, premier };

    pageEnchainement[1] = { "Durée"_fr, [this] { return nomDeLaDuree (moteur.getMorceau().enchainement.mesures); },
                            [this] {
                                const auto& d = Enchainement::dureesPossibles;
                                return (float) (std::find (d.begin(), d.end(), moteur.getMorceau().enchainement.mesures) - d.begin()) / (float) (d.size() - 1); },
                            [this, changerEnchainement] (int pas, bool) { if (auto c = crantsDiscrets (1, pas)) changerEnchainement (1, [c] (Enchainement& e) {
                                const auto& d = Enchainement::dureesPossibles;
                                const int i = (int) (std::find (d.begin(), d.end(), e.mesures) - d.begin());
                                e.mesures = d[(size_t) juce::jlimit (0, (int) d.size() - 1, i + c)]; }); }, premier };

    pageEnchainement[2] = { "Batterie nette", [this] { return moteur.getMorceau().enchainement.batterieNette ? juce::String ("Oui") : juce::String ("Non"); },
                            [this] { return moteur.getMorceau().enchainement.batterieNette ? 1.0f : 0.0f; },
                            [this, changerEnchainement] (int pas, bool) { if (auto c = crantsDiscrets (2, pas)) changerEnchainement (2, [c] (Enchainement& e) { e.batterieNette = c > 0; }); }, premier };

    pageEnchainement[3] = { "Intensité"_fr, [this, pourcent] { return pourcent (moteur.getMorceau().enchainement.intensite); },
                            [this] { return moteur.getMorceau().enchainement.intensite; },
                            [changerEnchainement] (int pas, bool fin) { changerEnchainement (3, [=] (Enchainement& e) {
                                e.intensite = juce::jlimit (0.0f, 1.0f, e.intensite + (float) pas * (fin ? 0.002f : 0.01f)); }); }, premier };

    pageEnchainement[4] = { "Montée"_fr, [this] { return moteur.getMorceau().enchainement.montee ? juce::String ("Oui") : juce::String ("Non"); },
                            [this] { return moteur.getMorceau().enchainement.montee ? 1.0f : 0.0f; },
                            [this, changerEnchainement] (int pas, bool) { if (auto c = crantsDiscrets (4, pas)) changerEnchainement (4, [c] (Enchainement& e) { e.montee = c > 0; }); }, premier };

    // Page d'un effet d'insertion : ses réglages (1 à 4), et l'encodeur 8 pour le sauter
    for (int k = 0; k < 4; ++k)
        pageEffet[(size_t) k] = { {}, [this, k, pourcent] {
                                      const auto& e = moteur.getMorceau().pistes[(size_t) pisteChoisie].effets[(size_t) emplacementEffet];
                                      if (! e || k >= Effet::nomsDesReglages (e->type).size()) return juce::String();
                                      if (e->type == TypeEffet::filtre && k == 0)
                                      {
                                          const auto f = e->reglages[0];
                                          return std::abs (f - 0.5f) < 0.01f ? juce::String ("Neutre") : (f < 0.5f ? "Passe-bas " : "Passe-haut ") + juce::String (juce::roundToInt (std::abs (f - 0.5f) * 200.0f));
                                      }
                                      return pourcent (e->reglages[(size_t) k]); },
                                  [this, k] { const auto& e = moteur.getMorceau().pistes[(size_t) pisteChoisie].effets[(size_t) emplacementEffet]; return e ? e->reglages[(size_t) k] : 0.0f; },
                                  [this, k] (int pas, bool fin) { modifierEffet (k, [=] (Effet& e) { if (k < Effet::nomsDesReglages (e.type).size()) e.reglages[(size_t) k] = juce::jlimit (0.0f, 1.0f, e.reglages[(size_t) k] + (float) pas * (fin ? 0.001f : 0.005f)); }); },
                                  actif };

    pageEffet[7] = { "Actif", [this] { const auto& e = moteur.getMorceau().pistes[(size_t) pisteChoisie].effets[(size_t) emplacementEffet]; return e && e->actif ? juce::String ("Oui") : juce::String ("Sauté"_fr); },
                     [this] { const auto& e = moteur.getMorceau().pistes[(size_t) pisteChoisie].effets[(size_t) emplacementEffet]; return e && e->actif ? 1.0f : 0.0f; },
                     [this] (int pas, bool) { if (auto c = crantsDiscrets (7, pas)) modifierEffet (7, [=] (Effet& e) { e.actif = c > 0; }); }, premier };

    // Page Add Device : choisir l'effet et l'emplacement, puis rappuyer sur Add Device
    pageAjout[0] = { "Effet", [this] { return nomDeLEffet ((TypeEffet) typeAAjouter); }, [this] { return (float) typeAAjouter / (float) (nombreDeTypesDEffet - 1); },
                     [this] (int pas, bool) { if (auto c = crantsDiscrets (0, pas)) typeAAjouter = juce::jlimit (0, nombreDeTypesDEffet - 1, typeAAjouter + c); }, premier };
    pageAjout[1] = { "Emplacement", [this] {
                         const auto& e = moteur.getMorceau().pistes[(size_t) pisteChoisie].effets[(size_t) emplacementAAjouter];
                         return juce::String (emplacementAAjouter + 1) + (e ? " (" + nomDeLEffet (e->type) + ")" : juce::String (" (libre)")); },
                     [this] { return (float) emplacementAAjouter / 2.0f; },
                     [this] (int pas, bool) { if (auto c = crantsDiscrets (1, pas)) emplacementAAjouter = juce::jlimit (0, 2, emplacementAAjouter + c); }, premier };

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

    // Page Gamme (bouton Scale) : fondamentale et mode du morceau
    pageGamme[0] = { "Fondamentale", [this] { return Gamme::nomDeLaNote (moteur.getMorceau().gamme.fondamentale); },
                     [this] { return (float) moteur.getMorceau().gamme.fondamentale / 11.0f; },
                     [this] (int pas, bool) { if (auto c = crantsDiscrets (0, pas)) moteur.modifier ([=] (Morceau& m) { m.gamme.fondamentale = ((m.gamme.fondamentale + c) % 12 + 12) % 12; }, "gamme 0"); }, premier };
    pageGamme[1] = { "Mode", [this] { return Gamme::nomsDesModes()[(int) moteur.getMorceau().gamme.mode]; },
                     [this] { return (float) moteur.getMorceau().gamme.mode / 6.0f; },
                     [this] (int pas, bool) { if (auto c = crantsDiscrets (1, pas)) moteur.modifier ([=] (Morceau& m) { m.gamme.mode = (Gamme::Mode) juce::jlimit (0, 6, (int) m.gamme.mode + c); }, "gamme 1"); }, premier };

    notesTenues.fill (-1);

    // Page Macros d'une piste de synthé : les 8 macros du son de Surge (noms mis à jour à chaque rafraîchissement)
    for (int i = 0; i < 8; ++i)
        pageMacros[(size_t) i] = { {}, [this, i, pourcent] { return pourcent (moteur.getMorceau().pistes[(size_t) pisteChoisie].macros[(size_t) i]); },
                                   [this, i] { return moteur.getMorceau().pistes[(size_t) pisteChoisie].macros[(size_t) i]; },
                                   [this, i] (int pas, bool fin) { moteur.modifier ([=, p = pisteChoisie] (Morceau& m) { auto& v = m.pistes[(size_t) p].macros[(size_t) i];
                                                                       v = juce::jlimit (0.0f, 1.0f, v + (float) pas * (fin ? 0.001f : 0.005f)); }, "macro " + juce::String (i)); },
                                   palette::couleurDePiste (0) };

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

void Pilote::modifierEffet (int encodeur, std::function<void (Effet&)> f)
{
    moteur.modifier ([=, p = pisteChoisie, k = emplacementEffet] (Morceau& m)
    {
        if (auto& e = m.pistes[(size_t) p].effets[(size_t) k])
            f (*e);
    }, "effet " + juce::String (emplacementEffet) + " " + juce::String (encodeur));
}

void Pilote::pageSuivanteDeDevice()
{
    // Les pages de la piste, puis une page par effet d'insertion
    const auto& p = moteur.getMorceau().pistes[(size_t) pisteChoisie];
    std::vector<std::pair<Page, int>> pages;

    if (p.estUneBatterie())
        pages = { { Page::device, 0 }, { Page::generateur, 0 }, { Page::commun, 0 } };
    else
        pages = { { Page::macros, 0 }, { Page::melodie, 0 }, { Page::communSynthe, 0 } };

    for (int k = 0; k < 3; ++k)
        if (p.effets[(size_t) k])
            pages.push_back ({ Page::effet, k });

    size_t i = 0;

    while (i < pages.size() && ! (pages[i].first == page && (page != Page::effet || pages[i].second == emplacementEffet)))
        ++i;

    const auto suivante = i < pages.size() ? pages[(i + 1) % pages.size()] : pages.front();
    page = suivante.first;
    emplacementEffet = suivante.second;
}

const Generateur* Pilote::generateurVise() const
{
    const auto c = caseDuPasAPas();
    const auto* clip = c ? moteur.getMorceau().clip (c->first, c->second) : nullptr;
    return clip != nullptr && clip->generateur ? &*clip->generateur : nullptr;
}

const GenerateurMelodique* Pilote::melodieVisee() const
{
    const auto c = caseDuPasAPas();
    const auto* clip = c ? moteur.getMorceau().clip (c->first, c->second) : nullptr;
    return clip != nullptr && clip->melodie ? &*clip->melodie : nullptr;
}

void Pilote::modifierMelodie (int encodeur, std::function<void (GenerateurMelodique&)> f)
{
    if (pisteChoisieEstUneBatterie())
        return;

    const bool devientGenere = melodieVisee() == nullptr;
    const auto& piste = moteur.getMorceau().pistes[(size_t) pisteChoisie];
    const auto* son = sonsDeSynthe::trouver (piste.sonDeSynthe);
    const auto style = melodies::styleDuSon (son != nullptr ? son->style : juce::String(), piste.son == SonProvisoire::basse);

    modifierClipVise (100 + encodeur, [=] (Clip& c)
    {
        // Un clip écrit à la main devient généré (Undo pour revenir) ; le style de départ suit le son de la piste
        if (! c.melodie)
            c.melodie = GenerateurMelodique::parDefaut (style);

        f (*c.melodie);
        c.nom = nomDuStyle (c.melodie->style);
    });

    if (devientGenere)
        annoncer ("Mélodie"_fr, "Le clip est maintenant généré (Undo pour revenir)"_fr);
}

void Pilote::modifierClipVise (int encodeur, std::function<void (Clip&)> f)
{
    auto cible = caseDuPasAPas();
    const int scene = cible ? cible->second : (caseChoisie && caseChoisie->first == pisteChoisie ? caseChoisie->second : 0);

    moteur.modifier ([=, p = pisteChoisie] (Morceau& m)
    {
        m.assurerScenes (scene + 1);
        auto& c = m.scenes[(size_t) scene].clips[(size_t) p];

        if (! c)
            c = Clip { "Rythme", 1, {} };

        f (*c);
        c->regenerer (m.mesure, m.gamme);
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

    if (c.melodie)
    {
        // Mélodie générée : nouvelle graine, densité décalée ; elle reste réglable
        c.melodie->graine = graine;
        c.melodie->densite = juce::jlimit (0.0f, 1.0f, c.melodie->densite + m.variation.densite * 0.2f);
        c.regenerer (m.mesure, m.gamme);
    }
    else if (c.generateur)
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
        c.regenerer (m.mesure, m.gamme);
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

    if (! c || moteur.getMorceau().clip (c->first, c->second) == nullptr || ! moteur.getMorceau().clip (c->first, c->second)->estGenere())
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

    if (! pisteChoisieEstUneBatterie())
        return sonsDeSynthe::sonsDuStyle (categorie);

    if (categorie == "Kits")
        return kitsDeDepart::noms();

    return moteur.getBanque().sons (categorie);
}

void Pilote::preecouterLeSynthe()
{
    // La fondamentale de la gamme, une demi-seconde
    const int note = juce::jlimit (0, 127, moteur.getMorceau().gamme.note (0, octaves[(size_t) pisteChoisie]));
    moteur.jouerNote (pisteChoisie, note, 100);
    juce::Timer::callAfterDelay (500, [vie = std::weak_ptr<bool> (vivant), this, piste = pisteChoisie, note]
    {
        if (! vie.expired())
            moteur.relacherNote (piste, note);
    });
}

void Pilote::choisirDansLeNavigateur()
{
    const auto sons = sonsDeLaCategorie();

    if (! pisteChoisieEstUneBatterie())
    {
        if (sons.isEmpty())
            return;

        if (auto son = sonsDeSynthe::trouver (sons[sonChoisi]))
            moteur.modifier ([=, p = pisteChoisie] (Morceau& m)
            {
                m.pistes[(size_t) p].sonDeSynthe = son->nom;

                for (int k = 0; k < 8; ++k)
                    m.pistes[(size_t) p].macros[(size_t) k] = son->macros[(size_t) k].depart;
            }, "navigateur");

        preecouterLeSynthe();
        return;
    }


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

int Pilote::noteDuPad (int ligne, int colonne) const
{
    // Comme le Push d'Ableton « en gamme » : un degré par colonne, une quarte par rangée
    const auto& gamme = moteur.getMorceau().gamme;
    const int parRangee = gamme.nombreDeDegres() >= 7 ? 3 : 2;
    return juce::jlimit (0, 127, gamme.note (ligne * parRangee + colonne, octaves[(size_t) pisteChoisie]));
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
            if (mode == Mode::note && ! pisteChoisieEstUneBatterie())
            {
                // Jeu en gamme : chaque pad garde la note qu'il a lancée, même si l'octave change entre-temps
                auto& tenue = notesTenues[(size_t) e.numero];

                if (e.appuye)
                {
                    tenue = noteDuPad (e.ligne(), e.colonne());
                    moteur.jouerNote (pisteChoisie, tenue, tenu (Bouton::accent) ? 127 : e.valeur);
                }
                else if (tenue >= 0)
                {
                    moteur.relacherNote (pisteChoisie, tenue);
                    tenue = -1;
                }
            }
            else if (mode == Mode::note)
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
            if (e.numero >= premierBoutonDuHaut && e.numero < premierBoutonDuHaut + 8)
            {
                // Envoi momentané vers le delay dub, tant que le bouton est tenu
                const int piste = e.numero - premierBoutonDuHaut;
                moteur.envoiMomentane (piste, e.appuye);

                if (e.appuye)
                    annoncer ("Envoi au delay"_fr, moteur.getMorceau().pistes[(size_t) piste].nom);
            }
            else if (e.appuye)
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
            if (! e.appuye && ignorerLaBande)
            {
                ignorerLaBande = false;
                moteur.filtreGlobal (0.5f);
            }
            break;

        case Evenement::Type::bandeDeplacee:
        {
            const auto position = (float) e.valeur / 16383.0f;

            // Enchaînement à la main : la bande mélange, de la scène en cours (en bas) à la nouvelle (en haut)
            if (moteur.enchainementALaMain())
            {
                const int scene = moteur.sceneALaMain();
                moteur.melangerALaMain (position);

                if (! moteur.enchainementALaMain())
                {
                    ignorerLaBande = true;
                    const auto& scenes = moteur.getMorceau().scenes;
                    annoncer (position > 0.5f && scene >= 0 && scene < (int) scenes.size() ? scenes[(size_t) scene].nom : juce::String ("Mélange annulé"_fr),
                              position > 0.5f ? juce::String ("Prend la main") : juce::String ("Retour à la scène en cours"_fr));
                }
                else
                {
                    annoncer ("Mélange"_fr, juce::String (juce::roundToInt (position * 100.0f)) + " %");
                }
                break;
            }

            if (ignorerLaBande)
                break;

            // Filtre global : en bas passe-bas, au milieu neutre, en haut passe-haut
            moteur.filtreGlobal (std::abs (position - 0.5f) < 0.02f ? 0.5f : position);

            if (std::abs (position - 0.5f) >= 0.02f)
                annoncer ("Filtre global", position < 0.5f ? "Passe-bas " + juce::String (juce::roundToInt ((0.5f - position) * 200.0f))
                                                           : "Passe-haut " + juce::String (juce::roundToInt ((position - 0.5f) * 200.0f)));
            break;
        }
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

    // User tenu : retenir l'enchaînement de la page pour entrer dans cette scène (ou le retirer s'il y est déjà)
    if (tenu (Bouton::user))
    {
        moteur.modifier ([=] (Morceau& m) {
            auto& entree = m.scenes[(size_t) scene].entree;
            if (entree && *entree == m.enchainement) entree.reset(); else entree = m.enchainement; });

        const auto& entree = moteur.getMorceau().scenes[(size_t) scene].entree;
        annoncer (morceau.scenes[(size_t) scene].nom, entree ? "Entrée : "_fr + decrire (*entree) : juce::String ("Entrée : celle de la page User"_fr));
        return;
    }

    // Shift + scène : enchaînement à la main, la bande tactile devient le curseur de mélange
    if (shift() && moteur.enLecture())
    {
        moteur.preparerScene (scene, page == Page::enchainement ? std::optional (morceau.enchainement) : std::nullopt);

        if (moteur.enchainementALaMain())
            annoncer ("À la main : "_fr + morceau.scenes[(size_t) scene].nom, "Monte la bande tactile"_fr);
        else
            annoncer (morceau.scenes[(size_t) scene].nom, "Rien à mélanger"_fr);

        return;
    }

    const bool etaitALaMain = moteur.enchainementALaMain();

    // Page Enchaînement ouverte : on lance avec ce qu'elle montre ; sinon, avec l'enchaînement de la scène
    const auto e = page == Page::enchainement ? morceau.enchainement : morceau.scenes[(size_t) scene].entree.value_or (morceau.enchainement);
    const bool enchaine = moteur.enLecture() && e.type != TypeEnchainement::coupe;
    if (etaitALaMain && scene == moteur.sceneALaMain())
    {
        moteur.lancerScene (scene, e);
        annoncer (morceau.scenes[(size_t) scene].nom, "Le mélange finit tout seul"_fr);
        return;
    }

    moteur.lancerScene (scene, e);
    annoncer ("Scène "_fr + juce::String (scene + 1), morceau.scenes[(size_t) scene].nom + (enchaine ? " (" + decrire (e) + ")" : juce::String()));
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
                mode = Mode::note;
                page = Page::device;
                annoncer ("Note", moteur.getMorceau().gamme.nom() + ", octave " + juce::String (octaves[(size_t) pisteChoisie]));
            }
            return;

        case Bouton::octaveHaut:
        case Bouton::octaveBas:
            if (mode == Mode::note && ! pisteChoisieEstUneBatterie())
            {
                auto& o = octaves[(size_t) pisteChoisie];
                o = juce::jlimit (0, 7, o + ((Bouton) bouton == Bouton::octaveHaut ? 1 : -1));
                annoncer ("Octave", juce::String (o));
            }
            return;

        case Bouton::scale:
            page = Page::gamme;
            annoncer ("Scale", moteur.getMorceau().gamme.nom());
            return;

        case Bouton::session:
            mode = Mode::session;
            annoncer ("Session", "Pistes et scènes"_fr);
            return;

        case Bouton::device:
            if (tenu (Bouton::deleteBouton) && page == Page::effet)
            {
                const auto nom = nomDeLEffet (morceau.pistes[(size_t) pisteChoisie].effets[(size_t) emplacementEffet]->type);
                moteur.modifier ([p = pisteChoisie, k = emplacementEffet] (Morceau& m) { m.pistes[(size_t) p].effets[(size_t) k].reset(); });
                pageSuivanteDeDevice();
                annoncer ("Effet retiré"_fr, nom);
                return;
            }

            pageSuivanteDeDevice();

            if (page == Page::effet)
            {
                annoncer ("Effet "_fr + juce::String (emplacementEffet + 1), nomDeLEffet (moteur.getMorceau().pistes[(size_t) pisteChoisie].effets[(size_t) emplacementEffet]->type));
                return;
            }

            if (! pisteChoisieEstUneBatterie())
            {
                annoncer ("Device", page == Page::macros ? morceau.pistes[(size_t) pisteChoisie].sonDeSynthe + (moteur.joueAvecSurge (pisteChoisie) ? juce::String() : juce::String (" (Surge XT absent : son provisoire)"))
                                  : page == Page::melodie ? "Mélodie "_fr + (melodieVisee() ? nomDuStyle (melodieVisee()->style) : juce::String ("(le clip deviendra généré)"_fr))
                                                          : juce::String ("Swing, graine, longueur"));
                return;
            }

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

            if (pisteChoisieEstUneBatterie())
            {
                categoriesDuNavigateur = moteur.getBanque().categories();
                categoriesDuNavigateur.insert (0, "Kits");
            }
            else
            {
                categoriesDuNavigateur = sonsDeSynthe::styles();
            }

            categorieChoisie = juce::jlimit (0, juce::jmax (0, categoriesDuNavigateur.size() - 1), categorieChoisie);
            sonChoisi = 0;
            page = Page::browse;
            annoncer ("Browse", pisteChoisieEstUneBatterie() ? "Encodeur 1 : catégorie ; encodeur 2 : son"_fr : juce::String ("Encodeur 1 : style ; encodeur 2 : son"));
            return;

        case Bouton::pageGauche:
        case Bouton::pageDroite:
            if (page == Page::mix || page == Page::envoisDelay || page == Page::envoisReverb || page == Page::departs)
            {
                static constexpr Page pagesDuMix[] = { Page::mix, Page::envoisDelay, Page::envoisReverb, Page::departs };
                int i = 0;
                while (pagesDuMix[i] != page) ++i;
                i = juce::jlimit (0, 3, i + ((Bouton) bouton == Bouton::pageDroite ? 1 : -1));
                page = pagesDuMix[i];
                static const juce::StringArray noms { "Volumes", "Envois au delay dub", "Envois à la réverbération"_fr, "Delay, réverbération, sortie"_fr };
                annoncer ("Mix", noms[i]);
                return;
            }

            if (mode == Mode::note)
            {
                const int pages = juce::jmax (1, (nombreDePas() + 31) / 32);
                pageDePas = juce::jlimit (0, pages - 1, pageDePas + ((Bouton) bouton == Bouton::pageDroite ? 1 : -1));
                annoncer ("Pas à pas"_fr, "Page " + juce::String (pageDePas + 1) + " sur " + juce::String (pages));
            }
            return;

        case Bouton::accent:
            return;     // se tient, avec un pad ou un pas

        case Bouton::addDevice:
            if (page == Page::ajoutEffet)
            {
                moteur.modifier ([p = pisteChoisie, k = emplacementAAjouter, t = typeAAjouter] (Morceau& m) { m.pistes[(size_t) p].effets[(size_t) k] = Effet::parDefaut ((TypeEffet) t); });
                page = Page::effet;
                emplacementEffet = emplacementAAjouter;
                annoncer ("Effet ajouté"_fr, nomDeLEffet ((TypeEffet) typeAAjouter) + ", emplacement " + juce::String (emplacementAAjouter + 1));
            }
            else
            {
                // On propose le premier emplacement libre
                emplacementAAjouter = 0;

                while (emplacementAAjouter < 2 && morceau.pistes[(size_t) pisteChoisie].effets[(size_t) emplacementAAjouter])
                    ++emplacementAAjouter;

                page = Page::ajoutEffet;
                annoncer ("Add Device", "Encodeur 1 : effet ; 2 : emplacement ; rappuie pour ajouter"_fr);
            }
            return;

        case Bouton::mix:
            page = Page::mix;
            annoncer ("Mix", "Volumes des pistes sur les encodeurs");
            return;

        case Bouton::setup:
            page = Page::clic;
            annoncer ("Setup", "Réglages du clic sur les encodeurs"_fr);
            return;

        case Bouton::record:
            if (moteur.priseEnCours())
            {
                const auto nom = moteur.priseCourante()->nom;
                const auto duree = moteur.dureeDeLaPrise();
                moteur.arreterPrise();
                annoncer (nom + " enregistrée"_fr, formaterDuree (duree));
            }
            else if (moteur.demarrerPrise())
            {
                annoncer ("Record", moteur.priseCourante()->nom + " en cours"_fr);
            }
            else
            {
                annoncer ("Record", "Prise impossible : voir le journal"_fr);
            }
            return;

        case Bouton::user:
            page = Page::enchainement;
            annoncer ("Enchaînement"_fr, "Scène : lancer ainsi ; User tenu + scène : retenir"_fr);
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

    if (mode == Mode::note && ! morceau.pistes[(size_t) pisteChoisie].estUneBatterie())
    {
        // Jeu en gamme : fondamentales en couleur pleine, autres notes en couleur faible, pad tenu en blanc
        for (int i = 0; i < 64; ++i)
        {
            const int note = noteDuPad (i / 8, i % 8);
            etat.pads[(size_t) i] = notesTenues[(size_t) i] >= 0 ? palette::blanc
                                  : palette::indexDePiste (pisteChoisie, morceau.gamme.estFondamentale (note) ? 3 : 1);
        }
    }
    else if (mode == Mode::note)
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

    // Boutons du haut : envoi momentané, allumés fort tant qu'on les tient
    for (int piste = 0; piste < nombreDePistes; ++piste)
        etat.boutons[(size_t) (premierBoutonDuHaut + piste)] = palette::indexDePiste (piste, moteur.envoiMomentaneTenu (piste) ? 3 : 0);

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
    blanc (Bouton::scale, page == Page::gamme);
    blanc (Bouton::addDevice, page == Page::ajoutEffet);
    blanc (Bouton::user, page == Page::enchainement || moteur.enchainementEnCours().has_value());
    etat.bandeSansRetour = moteur.enchainementALaMain();

    // Record : rouge qui clignote pendant une prise
    etat.boutons[(size_t) Bouton::record] = moteur.priseEnCours() ? (std::fmod (juce::Time::getMillisecondCounterHiRes(), 1000.0) < 600.0 ? palette::rouge : palette::grisFonce)
                                                                   : palette::grisFonce;

    if (page == Page::mix || page == Page::envoisDelay || page == Page::envoisReverb || page == Page::departs)
    {
        if (page != Page::mix) blanc (Bouton::pageGauche, false);
        if (page != Page::departs) blanc (Bouton::pageDroite, false);
    }

    if (mode == Mode::note && ! morceau.pistes[(size_t) pisteChoisie].estUneBatterie())
    {
        if (octaves[(size_t) pisteChoisie] < 7) blanc (Bouton::octaveHaut, false);
        if (octaves[(size_t) pisteChoisie] > 0) blanc (Bouton::octaveBas, false);
    }

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
        position = "Mesure " + juce::String (p.mesure) + (moteur.priseEnCours() ? juce::String() : "   temps " + juce::String (p.temps + 1));   // pendant une prise, la place va à sa durée

    g.setColour (p.enLecture ? actif : discret);
    g.setFont (juce::FontOptions (28.0f, juce::Font::bold));
    g.drawText (position, juce::Rectangle<float> (455.0f, 4.0f, 340.0f, 40.0f), juce::Justification::centredLeft);

    const auto zoneClic = juce::Rectangle<float> (largeur - 150.0f, 9.0f, 136.0f, 30.0f);
    g.setColour (morceau.clic.actif ? premier : panneau);
    g.fillRoundedRectangle (zoneClic, 6.0f);
    g.setColour (morceau.clic.actif ? fond : discret);
    g.setFont (juce::FontOptions (22.0f, juce::Font::bold));
    g.drawText (morceau.clic.actif ? juce::String ("Clic") : juce::String ("Clic coupé"_fr), zoneClic, juce::Justification::centred);

    // Prise en cours : un point rouge et la durée
    if (moteur.priseEnCours())
    {
        const auto zone = juce::Rectangle<float> (largeur - 290.0f, 9.0f, 128.0f, 30.0f);
        g.setColour (juce::Colour (0xffe5484d));
        g.fillRoundedRectangle (zone, 6.0f);
        g.setColour (texte);
        g.fillEllipse (zone.getX() + 10.0f, zone.getCentreY() - 7.0f, 14.0f, 14.0f);
        g.setFont (juce::FontOptions (22.0f, juce::Font::bold));
        g.drawText (formaterDuree (moteur.dureeDeLaPrise()), zone.withTrimmedLeft (30.0f), juce::Justification::centred);
    }

    g.setColour (panneau);
    g.fillRect (0.0f, 50.0f, largeur, 2.0f);

    // Enchaînement en cours : la ligne sous l'en-tête avance avec lui
    const auto enchainement = moteur.enchainementEnCours();

    if (enchainement && ! enchainement->enAttente)
    {
        g.setColour (premier);
        g.fillRect (0.0f, 49.0f, largeur * enchainement->avancee, 4.0f);
    }

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

        if (page == Page::mix || page == Page::envoisDelay || page == Page::envoisReverb)
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

    // Page Enchaînement : à droite, l'enchaînement en cours et les deux scènes
    if (page == Page::enchainement)
    {
        const auto zone = juce::Rectangle<float> (4.0f * colonne + 8.0f, 60.0f, 4.0f * colonne - 16.0f, 92.0f);
        auto nomDeScene = [&morceau] (int s) { return s >= 0 && s < (int) morceau.scenes.size() ? morceau.scenes[(size_t) s].nom : juce::String ("Silence"); };

        g.setColour (discret);
        g.setFont (juce::FontOptions (21.0f));

        if (! enchainement)
        {
            g.drawFittedText ("Aucun enchaînement en cours"_fr, zone.withHeight (28.0f).toNearestInt(), juce::Justification::centred, 1, 0.7f);
            return;
        }

        const auto titre = enchainement->aLaMain ? "À la main : bande tactile"_fr
                         : enchainement->enAttente ? "Au prochain calage"_fr
                         : enchainement->annulation ? "Retour en cours"_fr
                                                    : nomDeLEnchainement (enchainement->type) + " en cours";
        g.drawFittedText (titre, zone.withHeight (28.0f).toNearestInt(), juce::Justification::centred, 1, 0.7f);

        g.setColour (texte);
        g.setFont (juce::FontOptions (30.0f, juce::Font::bold));
        g.drawFittedText (nomDeScene (enchainement->sceneDe) + "  vers  " + nomDeScene (enchainement->sceneVers),
                          juce::Rectangle<float> (zone.getX(), 90.0f, zone.getWidth(), 36.0f).toNearestInt(), juce::Justification::centred, 1, 0.5f);

        const auto barre = juce::Rectangle<float> (zone.getX() + 10.0f, 136.0f, zone.getWidth() - 20.0f, 10.0f);
        g.setColour (panneau);
        g.fillRoundedRectangle (barre, 4.0f);
        g.setColour (premier);
        g.fillRoundedRectangle (barre.withWidth (barre.getWidth() * enchainement->avancee), 4.0f);
    }
}

void Pilote::rafraichir()
{
    if (page == Page::effet)
    {
        const auto& e = moteur.getMorceau().pistes[(size_t) pisteChoisie].effets[(size_t) emplacementEffet];
        const auto noms = e ? Effet::nomsDesReglages (e->type) : juce::StringArray();

        for (int k = 0; k < 4; ++k)
            pageEffet[(size_t) k].nom = noms[k];
    }

    if (page == Page::macros)
    {
        const auto& p = moteur.getMorceau().pistes[(size_t) pisteChoisie];
        const auto* son = sonsDeSynthe::trouver (p.sonDeSynthe);

        for (int i = 0; i < 8; ++i)
        {
            pageMacros[(size_t) i].nom = son != nullptr ? son->macros[(size_t) i].nom : juce::String ("Macro ") + juce::String (i + 1);
            pageMacros[(size_t) i].couleur = palette::couleurDePiste (pisteChoisie);
        }
    }

    dessinerEcran (ecran);
    surface.publier (calculerEtat(), ecran);
}

} // namespace groovebox
