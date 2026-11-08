#include "Morceau.h"

namespace groovebox
{

namespace
{
    juce::var clipVersJson (const Clip& c)
    {
        auto objet = new juce::DynamicObject();
        objet->setProperty ("nom", c.nom);
        objet->setProperty ("longueurMesures", c.longueurMesures);

        // Une note = [début, durée, hauteur, vélocité] : compact et lisible
        juce::Array<juce::var> notes;

        for (auto& n : c.notes)
            notes.add (juce::Array<juce::var> { n.debut, n.duree, n.hauteur, n.velocite });

        objet->setProperty ("notes", notes);

        if (c.generateur)
            objet->setProperty ("generateur", c.generateur->versJson());

        if (c.melodie)
            objet->setProperty ("melodie", c.melodie->versJson());

        return juce::var (objet);
    }

    std::optional<Clip> clipDepuisJson (const juce::var& v)
    {
        if (! v.isObject())
            return std::nullopt;

        Clip c;
        c.nom = v.getProperty ("nom", {}).toString();
        c.longueurMesures = (int) v.getProperty ("longueurMesures", 1);

        if (std::find (Clip::longueursPossibles.begin(), Clip::longueursPossibles.end(), c.longueurMesures) == Clip::longueursPossibles.end())
            c.longueurMesures = 1;

        if (auto notes = v.getProperty ("notes", {}).getArray())
            for (auto& n : *notes)
                if (auto a = n.getArray(); a != nullptr && a->size() == 4)
                    c.notes.push_back ({ (double) (*a)[0], (double) (*a)[1],
                                         juce::jlimit (0, 127, (int) (*a)[2]), juce::jlimit (1, 127, (int) (*a)[3]) });

        if (const auto g = v.getProperty ("generateur", {}); g.isObject())
            c.generateur = Generateur::depuisJson (g);

        if (const auto g = v.getProperty ("melodie", {}); g.isObject())
            c.melodie = GenerateurMelodique::depuisJson (g);

        return c;
    }

    const juce::StringArray nomsDesSons { "batterie", "basse", "nappe", "lead", "arpege", "percussions", "stab", "texture", "sampler" };
    const juce::StringArray nomsDesCalages { "temps", "mesure", "deuxMesures", "quatreMesures" };
}


namespace
{
    juce::var mesureVersJson (const Mesure& m)
    {
        auto objet = new juce::DynamicObject();
        objet->setProperty ("numerateur", m.numerateur);
        objet->setProperty ("denominateur", m.denominateur);

        juce::Array<juce::var> groupement;

        for (auto g : m.groupement)
            groupement.add (g);

        objet->setProperty ("groupement", groupement);
        return juce::var (objet);
    }

    std::optional<Mesure> mesureDepuisJson (const juce::var& v)
    {
        if (! v.isObject())
            return std::nullopt;

        Mesure m { (int) v.getProperty ("numerateur", 4), (int) v.getProperty ("denominateur", 4), {} };

        if (auto groupement = v.getProperty ("groupement", {}).getArray())
            for (auto& g : *groupement)
                m.groupement.push_back ((int) g);

        if (! m.estValide())
            m = Mesure::standard (m.numerateur, m.denominateur);

        if (! m.estValide())
            return std::nullopt;

        return m;
    }
}

juce::var Morceau::versJson() const
{
    auto racine = new juce::DynamicObject();
    racine->setProperty ("format", "groovebox-morceau");
    racine->setProperty ("version", versionDuFormat);
    racine->setProperty ("nom", nom);
    racine->setProperty ("tempo", tempo);
    racine->setProperty ("mesure", mesureVersJson (mesure));

    auto c = new juce::DynamicObject();
    c->setProperty ("actif", clic.actif);
    c->setProperty ("volume", clic.volume);
    c->setProperty ("subdivisions", clic.subdivisions);
    c->setProperty ("unClicParGroupe", clic.unClicParGroupe);
    c->setProperty ("decompte", clic.decompte);
    c->setProperty ("ecouteSurMac", clic.ecouteSurMac);
    racine->setProperty ("clic", juce::var (c));

    racine->setProperty ("musiqueDansCasqueDb", musiqueDansCasqueDb);
    racine->setProperty ("volumeGeneralDb", volumeGeneralDb);
    racine->setProperty ("calage", nomsDesCalages[(int) calage]);
    racine->setProperty ("swing", swing);
    racine->setProperty ("gamme", gamme.versJson());

    auto d = new juce::DynamicObject();
    d->setProperty ("division", delay.division);
    d->setProperty ("retour", delay.retour);
    d->setProperty ("filtre", delay.filtre);
    d->setProperty ("saturation", delay.saturation);
    racine->setProperty ("delay", juce::var (d));

    auto r = new juce::DynamicObject();
    r->setProperty ("taille", reverb.taille);
    r->setProperty ("duree", reverb.duree);
    r->setProperty ("couleur", reverb.couleur);
    racine->setProperty ("reverb", juce::var (r));
    racine->setProperty ("compressionDeSortie", compressionDeSortie);
    racine->setProperty ("prochaineGraine", (juce::int64) prochaineGraine);

    auto var = new juce::DynamicObject();
    var->setProperty ("quantite", variation.quantite);
    var->setProperty ("densite", variation.densite);
    var->setProperty ("rotation", variation.rotation);
    var->setProperty ("inversion", variation.inversion);
    racine->setProperty ("variation", juce::var (var));
    racine->setProperty ("enchainement", enchainement.versJson());

    juce::Array<juce::var> listePistes;

    for (auto& p : pistes)
    {
        auto objet = new juce::DynamicObject();
        objet->setProperty ("nom", p.nom);
        objet->setProperty ("son", nomsDesSons[(int) p.son]);
        objet->setProperty ("volumeDb", p.volumeDb);
        objet->setProperty ("panoramique", p.panoramique);
        objet->setProperty ("mute", p.mute);
        objet->setProperty ("solo", p.solo);
        objet->setProperty ("swing", p.swing ? juce::var (*p.swing) : juce::var());
        objet->setProperty ("envoiDelayDb", p.envoiDelayDb);
        objet->setProperty ("envoiReverbDb", p.envoiReverbDb);

        juce::Array<juce::var> effets;

        for (auto& e : p.effets)
            effets.add (e ? e->versJson() : juce::var());

        objet->setProperty ("effets", effets);

        if (p.estUneBatterie())
            objet->setProperty ("kit", p.kit.versJson());
        else if (p.estUnSampler())
            objet->setProperty ("sampler", p.sampler.versJson());
        else
        {
            objet->setProperty ("sonDeSynthe", p.sonDeSynthe);
            objet->setProperty ("macros", juce::Array<juce::var> { p.macros[0], p.macros[1], p.macros[2], p.macros[3], p.macros[4], p.macros[5], p.macros[6], p.macros[7] });
        }

        listePistes.add (juce::var (objet));
    }

    racine->setProperty ("pistes", listePistes);

    juce::Array<juce::var> listeScenes;

    for (auto& s : scenes)
    {
        auto objet = new juce::DynamicObject();
        objet->setProperty ("nom", s.nom);
        objet->setProperty ("mesure", s.mesure ? mesureVersJson (*s.mesure) : juce::var());
        objet->setProperty ("tempo", s.tempo ? juce::var (*s.tempo) : juce::var());

        juce::Array<juce::var> clips;

        for (auto& c : s.clips)
            clips.add (c ? clipVersJson (*c) : juce::var());

        objet->setProperty ("clips", clips);

        if (s.entree)
            objet->setProperty ("entree", s.entree->versJson());

        listeScenes.add (juce::var (objet));
    }

    racine->setProperty ("scenes", listeScenes);
    return juce::var (racine);
}

bool Clip::doubler (int tempsParMesure)
{
    if (longueurMesures >= 16)
        return false;

    const auto decalage = (double) (longueurMesures * tempsParMesure);
    const auto originales = notes;

    for (auto n : originales)
    {
        n.debut += decalage;
        notes.push_back (n);
    }

    longueurMesures *= 2;
    return true;
}

juce::String nomDuSon (SonProvisoire s)
{
    switch (s)
    {
        case SonProvisoire::batterie:       return "Batterie";
        case SonProvisoire::basse:          return "Basse";
        case SonProvisoire::nappe:          return "Nappe";
        case SonProvisoire::lead:           return "Lead";
        case SonProvisoire::arpege:         return "Arpège"_fr;
        case SonProvisoire::percussions:    return "Percussions";
        case SonProvisoire::stab:           return "Stab";
        case SonProvisoire::texture:        return "Texture";
        case SonProvisoire::sampler:        return "Sampler";
    }

    return {};
}

juce::String nomDuCalage (Calage c)
{
    switch (c)
    {
        case Calage::temps:             return "Au temps";
        case Calage::mesure:            return "À la mesure"_fr;
        case Calage::deuxMesures:       return "Toutes les 2 mesures";
        case Calage::quatreMesures:     return "Toutes les 4 mesures";
    }

    return {};
}

void Clip::regenerer (const Mesure& m, const Gamme& g)
{
    if (generateur)
        notes = generateurs::generer (*generateur, m, longueurMesures);
    else if (melodie)
        notes = melodies::generer (*melodie, m, g, longueurMesures);
}

float Morceau::swingDeLaPiste (int piste) const
{
    const auto& p = pistes[(size_t) juce::jlimit (0, nombreDePistes - 1, piste)];
    return p.swing.value_or (swing);
}

void Morceau::regenererTout()
{
    for (auto& s : scenes)
        for (auto& c : s.clips)
            if (c && c->estGenere())
                c->regenerer (mesure, gamme);
}

bool Scene::estVide() const
{
    return std::none_of (clips.begin(), clips.end(), [] (auto& c) { return c.has_value(); });
}

std::array<Piste, nombreDePistes> Morceau::pistesDeDepart()
{
    std::array<Piste, nombreDePistes> pistes;
    const SonProvisoire sons[] = { SonProvisoire::batterie, SonProvisoire::basse, SonProvisoire::nappe, SonProvisoire::lead,
                                   SonProvisoire::arpege, SonProvisoire::percussions, SonProvisoire::stab, SonProvisoire::texture };

    for (int i = 0; i < nombreDePistes; ++i)
        pistes[(size_t) i] = { nomDuSon (sons[i]), sons[i] };

    pistes[0].kit = kitsDeDepart::kit ("Analogique");
    pistes[5].kit = kitsDeDepart::kit ("Minimal");
    return pistes;
}

const Clip* Morceau::clip (int piste, int scene) const
{
    if (piste < 0 || piste >= nombreDePistes || scene < 0 || scene >= (int) scenes.size())
        return nullptr;

    auto& c = scenes[(size_t) scene].clips[(size_t) piste];
    return c ? &*c : nullptr;
}

void Morceau::assurerScenes (int n)
{
    while ((int) scenes.size() < n)
        scenes.push_back ({ "Scène "_fr + juce::String ((int) scenes.size() + 1), std::nullopt, std::nullopt, {}, std::nullopt });
}

Morceau Morceau::depuisJson (const juce::var& v)
{
    Morceau m;
    m.nom = v.getProperty ("nom", m.nom).toString();
    m.tempo = juce::jlimit (tempoMin, tempoMax, (double) v.getProperty ("tempo", m.tempo));

    if (auto mesure = mesureDepuisJson (v.getProperty ("mesure", {})))
        m.mesure = *mesure;

    const auto c = v.getProperty ("clic", {});
    m.clic.actif = c.getProperty ("actif", m.clic.actif);
    m.clic.volume = juce::jlimit (0.0f, 1.0f, (float) c.getProperty ("volume", m.clic.volume));
    m.clic.subdivisions = c.getProperty ("subdivisions", m.clic.subdivisions);
    m.clic.unClicParGroupe = c.getProperty ("unClicParGroupe", m.clic.unClicParGroupe);
    m.clic.decompte = juce::jlimit (0, 2, (int) c.getProperty ("decompte", m.clic.decompte));
    m.clic.ecouteSurMac = c.getProperty ("ecouteSurMac", m.clic.ecouteSurMac);

    m.musiqueDansCasqueDb = juce::jlimit (-60.0f, 6.0f, (float) v.getProperty ("musiqueDansCasqueDb", m.musiqueDansCasqueDb));
    m.volumeGeneralDb = juce::jlimit (-60.0f, 6.0f, (float) v.getProperty ("volumeGeneralDb", m.volumeGeneralDb));

    if (const auto c = nomsDesCalages.indexOf (v.getProperty ("calage", {}).toString()); c >= 0)
        m.calage = (Calage) c;

    m.swing = juce::jlimit (0.0f, 0.75f, (float) v.getProperty ("swing", 0.0f));

    if (const auto g = v.getProperty ("gamme", {}); g.isObject())
        m.gamme = Gamme::depuisJson (g);

    if (const auto d = v.getProperty ("delay", {}); d.isObject())
    {
        m.delay.division = juce::jlimit (0, (int) DelayDub::divisions.size() - 1, (int) d.getProperty ("division", 0));
        m.delay.retour = juce::jlimit (0.0f, 1.2f, (float) d.getProperty ("retour", 0.45f));
        m.delay.filtre = juce::jlimit (0.0f, 1.0f, (float) d.getProperty ("filtre", 0.35f));
        m.delay.saturation = juce::jlimit (0.0f, 1.0f, (float) d.getProperty ("saturation", 0.2f));
    }

    if (const auto r = v.getProperty ("reverb", {}); r.isObject())
    {
        m.reverb.taille = juce::jlimit (0.0f, 1.0f, (float) r.getProperty ("taille", 0.5f));
        m.reverb.duree = juce::jlimit (0.0f, 1.0f, (float) r.getProperty ("duree", 0.5f));
        m.reverb.couleur = juce::jlimit (0.0f, 1.0f, (float) r.getProperty ("couleur", 0.5f));
    }

    m.compressionDeSortie = juce::jlimit (0.0f, 1.0f, (float) v.getProperty ("compressionDeSortie", 0.3f));
    m.prochaineGraine = (juce::uint32) (juce::int64) v.getProperty ("prochaineGraine", 2);

    if (const auto var = v.getProperty ("variation", {}); var.isObject())
    {
        m.variation.quantite = juce::jlimit (0.0f, 1.0f, (float) var.getProperty ("quantite", 0.25f));
        m.variation.densite = juce::jlimit (-1.0f, 1.0f, (float) var.getProperty ("densite", 0.0f));
        m.variation.rotation = (int) var.getProperty ("rotation", 0);
        m.variation.inversion = var.getProperty ("inversion", false);
    }

    if (auto e = Enchainement::depuisJson (v.getProperty ("enchainement", {})))
        m.enchainement = *e;

    if (auto liste = v.getProperty ("pistes", {}).getArray())
    {
        for (int i = 0; i < std::min (nombreDePistes, liste->size()); ++i)
        {
            const auto& p = (*liste)[i];
            auto& piste = m.pistes[(size_t) i];
            piste.nom = p.getProperty ("nom", piste.nom).toString();

            if (const auto son = nomsDesSons.indexOf (p.getProperty ("son", {}).toString()); son >= 0)
                piste.son = (SonProvisoire) son;

            piste.volumeDb = juce::jlimit (-60.0f, 6.0f, (float) p.getProperty ("volumeDb", 0.0f));
            piste.panoramique = juce::jlimit (-1.0f, 1.0f, (float) p.getProperty ("panoramique", 0.0f));
            piste.mute = p.getProperty ("mute", false);
            piste.solo = p.getProperty ("solo", false);

            piste.envoiDelayDb = juce::jlimit (-60.0f, 6.0f, (float) p.getProperty ("envoiDelayDb", -60.0f));
            piste.envoiReverbDb = juce::jlimit (-60.0f, 6.0f, (float) p.getProperty ("envoiReverbDb", -60.0f));

            if (auto effets = p.getProperty ("effets", {}).getArray())
                for (int k = 0; k < std::min (3, effets->size()); ++k)
                    piste.effets[(size_t) k] = Effet::depuisJson ((*effets)[k]);

            if (const auto sw = p.getProperty ("swing", {}); ! sw.isVoid())
                piste.swing = juce::jlimit (0.0f, 0.75f, (float) sw);

            if (const auto son = p.getProperty ("sonDeSynthe", {}).toString(); son.isNotEmpty())
                piste.sonDeSynthe = son;

            if (auto macros = p.getProperty ("macros", {}).getArray(); macros != nullptr && macros->size() == 8)
                for (int k = 0; k < 8; ++k)
                    piste.macros[(size_t) k] = juce::jlimit (0.0f, 1.0f, (float) (*macros)[k]);

            piste.sampler = ReglagesSampler::depuisJson (p.getProperty ("sampler", {}));

            if (const auto kit = p.getProperty ("kit", {}); kit.isObject())
                piste.kit = Kit::depuisJson (kit);
            else if (piste.estUneBatterie() && piste.kit.nom.isEmpty())
                piste.kit = kitsDeDepart::kit (i == 0 ? "Analogique" : "Minimal");
        }
    }

    if (auto liste = v.getProperty ("scenes", {}).getArray())
    {
        for (auto& s : *liste)
        {
            Scene scene;
            scene.nom = s.getProperty ("nom", {}).toString();
            scene.mesure = mesureDepuisJson (s.getProperty ("mesure", {}));

            if (const auto t = s.getProperty ("tempo", {}); ! t.isVoid())
                scene.tempo = (double) t;

            if (auto clips = s.getProperty ("clips", {}).getArray())
                for (int i = 0; i < std::min (nombreDePistes, clips->size()); ++i)
                    scene.clips[(size_t) i] = clipDepuisJson ((*clips)[i]);

            scene.entree = Enchainement::depuisJson (s.getProperty ("entree", {}));

            m.scenes.push_back (scene);
        }
    }

    return m;
}

bool Morceau::enregistrer (const juce::File& fichier) const
{
    // Écriture dans un fichier à côté, puis remplacement : un plantage ne laisse jamais un fichier à moitié écrit
    juce::TemporaryFile temporaire (fichier);

    if (! temporaire.getFile().replaceWithText (juce::JSON::toString (versJson())))
        return false;

    return temporaire.overwriteTargetFileWithTemporary();
}

std::optional<Morceau> Morceau::charger (const juce::File& fichier)
{
    if (! fichier.existsAsFile())
        return std::nullopt;

    const auto v = juce::JSON::parse (fichier);

    if (v.getProperty ("format", {}).toString() != "groovebox-morceau")
        return std::nullopt;

    return depuisJson (v);
}

} // namespace groovebox
