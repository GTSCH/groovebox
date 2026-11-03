#include "Deroule.h"

namespace groovebox
{

namespace
{
    using Genre = EvenementDePrise::Genre;

    juce::var valeurDuChemin (const juce::var& json, const juce::String& chemin)
    {
        auto v = json;

        for (auto& cle : juce::StringArray::fromTokens (chemin, "/", {}))
        {
            if (cle.isEmpty())
                continue;

            if (auto t = v.getArray())
                v = cle.getIntValue() >= 0 && cle.getIntValue() < t->size() ? (*t)[cle.getIntValue()] : juce::var();
            else
                v = v.getProperty (cle, {});
        }

        return v;
    }

    bool estUnNombre (const juce::var& v)       { return v.isDouble() || v.isInt() || v.isInt64() || v.isBool(); }

    /** Les changements notés sont partagés entre copies d'une section : avant de les toucher, chacune a les siens. */
    void detacher (Section& s)
    {
        for (auto& e : s.evenements)
            e.changements = ecarts::copie (e.changements);
    }

    /** Ce qui joue sur chaque piste, après un lancement ou un arrêt. */
    void appliquerLancement (const EvenementDePrise& e, const Morceau& m, std::array<int, nombreDePistes>& scenes)
    {
        if (e.genre == Genre::scene || e.genre == Genre::preparation)
        {
            if (e.piste >= 0)
                scenes[(size_t) e.piste] = m.clip (e.piste, e.scene) != nullptr ? e.scene : -1;
            else
                for (int p = 0; p < nombreDePistes; ++p)
                    if (m.clip (p, e.scene) != nullptr)
                        scenes[(size_t) p] = e.scene;     // une case vide laisse la piste continuer
        }
        else if (e.genre == Genre::arret)
        {
            if (e.piste >= 0)
                scenes[(size_t) e.piste] = -1;
            else
                scenes.fill (-1);
        }
    }

    bool ouvreUneSection (const EvenementDePrise& e)
    {
        return (e.genre == Genre::scene && e.piste < 0) || e.genre == Genre::preparation;
    }

    bool estUnLancement (const EvenementDePrise& e)
    {
        return e.genre == Genre::scene || e.genre == Genre::preparation || e.genre == Genre::arret;
    }

    /** État (JSON) et scènes au bout d'un morceau de section : les réglages et les lancements qui ont eu lieu avant ce temps. */
    void avancer (juce::var& etat, std::array<int, nombreDePistes>& scenes, const std::vector<EvenementDePrise>& evenements, double jusqua)
    {
        // Le morceau n'est relu que quand un lancement en a besoin (une longue prise compte des milliers de crans d'encodeur)
        std::optional<Morceau> m;

        for (auto& e : evenements)
        {
            if (e.genre == Genre::reglages && e.temps < jusqua)
            {
                etat = ecarts::appliquer (etat, e.changements);
                m.reset();
            }
            else if (estUnLancement (e) && e.cale.value_or (e.temps) < jusqua)
            {
                if (! m)
                    m = Morceau::depuisJson (etat);

                appliquerLancement (e, *m, scenes);
            }
        }
    }

    juce::String nomDeScene (const juce::var& etat, int scene)
    {
        if (scene < 0)
            return "Départ"_fr;

        if (auto t = etat.getProperty ("scenes", {}).getArray(); t != nullptr && scene < t->size())
            return (*t)[scene].getProperty ("nom", {}).toString();

        return "Scène "_fr + juce::String (scene + 1);
    }
}

//==============================================================================
int Section::tempsParMesure() const
{
    return juce::jmax (1, (int) etat.getProperty ("mesure", {}).getProperty ("numerateur", 4));
}

juce::var Section::versJson() const
{
    auto o = new juce::DynamicObject();
    o->setProperty ("nom", nom);
    o->setProperty ("scene", scene);

    if (entree)
        o->setProperty ("entree", entree->versJson());

    o->setProperty ("aLaMain", aLaMain);
    o->setProperty ("ouverte", ouverte);
    o->setProperty ("longueur", longueur);
    o->setProperty ("avance", avance);
    o->setProperty ("etat", etat);

    juce::Array<juce::var> scenes;

    for (auto s : scenesDeDepart)
        scenes.add (s);

    o->setProperty ("scenesDeDepart", scenes);

    juce::Array<juce::var> liste;

    for (auto& e : evenements)
        liste.add (e.versJson());

    o->setProperty ("evenements", liste);
    o->setProperty ("prise", prise);
    o->setProperty ("debutDansLaPrise", debutDansLaPrise);
    return juce::var (o);
}

Section Section::depuisJson (const juce::var& v)
{
    Section s;
    s.nom = v.getProperty ("nom", {}).toString();
    s.scene = v.getProperty ("scene", -1);
    s.entree = Enchainement::depuisJson (v.getProperty ("entree", {}));
    s.aLaMain = v.getProperty ("aLaMain", false);
    s.ouverte = v.getProperty ("ouverte", false);
    s.longueur = v.getProperty ("longueur", 0.0);
    s.avance = v.getProperty ("avance", 0.0);
    s.etat = v.getProperty ("etat", {});

    if (auto t = v.getProperty ("scenesDeDepart", {}).getArray())
        for (int i = 0; i < std::min (nombreDePistes, t->size()); ++i)
            s.scenesDeDepart[(size_t) i] = (*t)[i];

    if (auto t = v.getProperty ("evenements", {}).getArray())
        for (auto& e : *t)
            if (auto ev = EvenementDePrise::depuisJson (e))
                s.evenements.push_back (*ev);

    s.prise = v.getProperty ("prise", {}).toString();
    s.debutDansLaPrise = v.getProperty ("debutDansLaPrise", 0.0);
    return s;
}

//==============================================================================
double Deroule::longueur() const
{
    double l = 0.0;

    for (auto& s : sections)
        l += s.longueur;

    return l;
}

double Deroule::debutDeLaSection (int i) const
{
    double t = 0.0;

    for (int k = 0; k < juce::jmin (i, (int) sections.size()); ++k)
        t += sections[(size_t) k].longueur;

    return t;
}

juce::var Deroule::versJson() const
{
    auto o = new juce::DynamicObject();
    o->setProperty ("format", "groovebox-deroule");
    o->setProperty ("version", versionDuFormat);
    o->setProperty ("nom", nom);

    juce::Array<juce::var> liste;

    for (auto& s : sections)
        liste.add (s.versJson());

    o->setProperty ("sections", liste);
    return juce::var (o);
}

std::optional<Deroule> Deroule::depuisJson (const juce::var& v)
{
    if (v.getProperty ("format", {}).toString() != "groovebox-deroule")
        return std::nullopt;

    Deroule d;
    d.nom = v.getProperty ("nom", {}).toString();

    if (auto t = v.getProperty ("sections", {}).getArray())
        for (auto& s : *t)
            d.sections.push_back (Section::depuisJson (s));

    return d;
}

bool Deroule::enregistrer (const juce::File& fichier) const
{
    fichier.getParentDirectory().createDirectory();
    juce::TemporaryFile temporaire (fichier);

    if (! temporaire.getFile().replaceWithText (juce::JSON::toString (versJson())))
        return false;

    return temporaire.overwriteTargetFileWithTemporary();
}

std::optional<Deroule> Deroule::charger (const juce::File& fichier)
{
    if (! fichier.existsAsFile())
        return std::nullopt;

    return depuisJson (juce::JSON::parse (fichier));
}

//==============================================================================
Deroule retouche::decouper (const Prise& prise, const juce::String& dossierDeLaPrise)
{
    // Les limites : chaque lancement de scène prend effet à son calage ; à calage égal, le dernier geste l'emporte
    struct Limite { double temps; const EvenementDePrise* lancement; };
    std::vector<Limite> limites { { prise.debut, nullptr } };

    for (auto& e : prise.evenements)
    {
        if (! ouvreUneSection (e))
            continue;

        const double t = juce::jlimit (prise.debut, prise.fin, e.cale.value_or (e.temps));

        if (std::abs (t - limites.back().temps) < 1.0e-6)
            limites.back().lancement = &e;
        else if (t > limites.back().temps)
            limites.push_back ({ t, &e });
    }

    Deroule d;
    d.nom = prise.nom;
    auto etat = prise.morceauDeDepart.versJson();
    auto scenes = prise.scenesDeDepart;
    double dejaAvance = prise.debut;

    for (size_t i = 0; i < limites.size(); ++i)
    {
        const double debut = limites[i].temps;
        const double fin = i + 1 < limites.size() ? limites[i + 1].temps : prise.fin;

        // L'état au début de la section : ce qui s'est passé avant, y compris le lancement qui l'ouvre
        std::vector<EvenementDePrise> avant;

        for (auto& e : prise.evenements)
        {
            const bool lancementDejaPasse = estUnLancement (e) && e.cale.value_or (e.temps) >= dejaAvance && e.cale.value_or (e.temps) <= debut;
            const bool reglageDejaPasse = e.genre == Genre::reglages && e.temps >= dejaAvance && e.temps < debut;

            if (lancementDejaPasse || reglageDejaPasse)
                avant.push_back (e);
        }

        avancer (etat, scenes, avant, debut + 1.0e-9);
        dejaAvance = debut;

        if (fin - debut < 1.0e-6)
            continue;

        Section s;
        s.etat = etat;
        s.scenesDeDepart = scenes;
        s.longueur = fin - debut;
        s.prise = dossierDeLaPrise;
        s.debutDansLaPrise = debut;

        if (auto* l = limites[i].lancement)
        {
            s.scene = l->scene;
            s.entree = l->enchainement;
            s.aLaMain = l->genre == Genre::preparation;
            s.avance = juce::jmax (0.0, debut - l->temps);
        }
        else
        {
            s.scene = -1;
        }

        s.nom = nomDeScene (etat, s.scene);

        // Les gestes de la section, sauf les lancements qui ouvrent une section
        for (auto& e : prise.evenements)
        {
            if (ouvreUneSection (e) || e.temps < debut || e.temps >= fin)
                continue;

            auto copie = e;
            copie.temps -= debut;

            if (copie.cale)
                *copie.cale -= debut;

            s.evenements.push_back (copie);
        }

        d.sections.push_back (std::move (s));
    }

    return d;
}

bool retouche::couper (Deroule& d, int i, double temps)
{
    if (i < 0 || i >= (int) d.sections.size())
        return false;

    auto& s = d.sections[(size_t) i];

    if (temps <= 1.0e-6 || temps >= s.longueur - 1.0e-6)
        return false;

    Section seconde;
    seconde.etat = s.etat;
    seconde.scenesDeDepart = s.scenesDeDepart;
    avancer (seconde.etat, seconde.scenesDeDepart, s.evenements, temps);

    seconde.nom = s.nom + " (suite)";
    seconde.scene = s.scene;
    seconde.longueur = s.longueur - temps;
    seconde.prise = s.prise;
    seconde.debutDansLaPrise = s.debutDansLaPrise + temps;

    std::vector<EvenementDePrise> premiers;

    for (auto& e : s.evenements)
    {
        if (e.temps < temps)
        {
            premiers.push_back (e);
            continue;
        }

        auto copie = e;
        copie.temps -= temps;

        if (copie.cale)
            *copie.cale -= temps;

        seconde.evenements.push_back (copie);
    }

    s.evenements = std::move (premiers);
    s.longueur = temps;
    d.sections.insert (d.sections.begin() + i + 1, std::move (seconde));
    return true;
}

bool retouche::supprimer (Deroule& d, int i)
{
    if (i < 0 || i >= (int) d.sections.size())
        return false;

    d.sections.erase (d.sections.begin() + i);
    return true;
}

bool retouche::deplacer (Deroule& d, int i, int sens)
{
    const int j = i + (sens < 0 ? -1 : 1);

    if (i < 0 || i >= (int) d.sections.size() || j < 0 || j >= (int) d.sections.size())
        return false;

    std::swap (d.sections[(size_t) i], d.sections[(size_t) j]);
    return true;
}

bool retouche::repeter (Deroule& d, int i)
{
    if (i < 0 || i >= (int) d.sections.size())
        return false;

    auto copie = d.sections[(size_t) i];
    detacher (copie);

    // La répétition repart de l'état du début de la section, comme si on la rejouait
    d.sections.insert (d.sections.begin() + i + 1, std::move (copie));
    return true;
}

//==============================================================================
int retouche::pisteDuChemin (const juce::String& chemin)
{
    if (! chemin.startsWith ("/pistes/"))
        return -1;

    return chemin.fromFirstOccurrenceOf ("/pistes/", false, false).upToFirstOccurrenceOf ("/", false, false).getIntValue();
}

juce::StringArray retouche::courbes (const Section& s, int piste)
{
    juce::StringArray chemins;

    for (auto& e : s.evenements)
        if (e.genre == Genre::reglages)
            if (auto t = e.changements.getArray())
                for (auto& c : *t)
                    if (auto paire = c.getArray(); paire != nullptr && paire->size() == 2 && estUnNombre ((*paire)[1]))
                    {
                        const auto chemin = (*paire)[0].toString();

                        // Les notes des clips ne sont pas des courbes
                        if (chemin.startsWith ("/scenes/"))
                            continue;

                        if (pisteDuChemin (chemin) == piste)
                            chemins.addIfNotAlreadyThere (chemin);
                    }

    return chemins;
}

std::vector<std::pair<double, double>> retouche::points (const Section& s, const juce::String& chemin)
{
    std::vector<std::pair<double, double>> p;

    if (const auto depart = valeurDuChemin (s.etat, chemin); estUnNombre (depart))
        p.push_back ({ 0.0, (double) depart });

    for (auto& e : s.evenements)
        if (e.genre == Genre::reglages)
            if (auto t = e.changements.getArray())
                for (auto& c : *t)
                    if (auto paire = c.getArray(); paire != nullptr && paire->size() == 2 && (*paire)[0].toString() == chemin && estUnNombre ((*paire)[1]))
                        p.push_back ({ e.temps, (double) (*paire)[1] });

    return p;
}

void retouche::lisser (Section& s, const juce::String& chemin)
{
    const auto avant = points (s, chemin);

    if (avant.size() < 3)
        return;

    detacher (s);

    // Moyenne sur une fenêtre d'un temps, pondérée en triangle ; la dernière valeur est gardée
    std::vector<double> lisses;

    for (size_t i = 0; i < avant.size(); ++i)
    {
        double somme = 0.0, poids = 0.0;

        for (auto& [t, v] : avant)
            if (const double d = std::abs (t - avant[i].first); d < 1.0)
            {
                somme += v * (1.0 - d);
                poids += 1.0 - d;
            }

        lisses.push_back (i + 1 == avant.size() ? avant[i].second : somme / poids);
    }

    // On réécrit les valeurs dans les gestes, dans le même ordre (le point 0 est la valeur de départ)
    size_t k = 1;

    for (auto& e : s.evenements)
        if (e.genre == Genre::reglages)
            if (auto t = e.changements.getArray())
                for (auto& c : *t)
                    if (auto paire = c.getArray(); paire != nullptr && paire->size() == 2 && (*paire)[0].toString() == chemin && k < lisses.size())
                    {
                        const bool entier = (*paire)[1].isInt() || (*paire)[1].isInt64();
                        paire->set (1, entier ? juce::var (juce::roundToInt (lisses[k])) : juce::var (lisses[k]));
                        ++k;
                    }
}

void retouche::effacer (Section& s, const juce::String& chemin)
{
    detacher (s);

    for (auto& e : s.evenements)
        if (e.genre == Genre::reglages)
            if (auto t = e.changements.getArray())
                for (int i = t->size(); --i >= 0;)
                    if (auto paire = (*t)[i].getArray(); paire != nullptr && paire->size() == 2 && (*paire)[0].toString() == chemin)
                        t->remove (i);

    s.evenements.erase (std::remove_if (s.evenements.begin(), s.evenements.end(), [] (const EvenementDePrise& e)
    {
        return e.genre == Genre::reglages && (e.changements.getArray() == nullptr || e.changements.getArray()->isEmpty());
    }), s.evenements.end());
}

juce::String retouche::nomDuReglage (const juce::String& chemin)
{
    auto morceaux = juce::StringArray::fromTokens (chemin, "/", {});
    morceaux.removeEmptyStrings();

    if (morceaux.isEmpty())
        return chemin;

    // Réglages du morceau entier
    if (morceaux[0] != "pistes" || morceaux.size() < 3)
    {
        static const std::map<juce::String, juce::String> noms {
            { "tempo", "Tempo" }, { "swing", "Swing" }, { "volumeGeneralDb", "Volume général"_fr }, { "musiqueDansCasqueDb", "Musique au casque" },
            { "compressionDeSortie", "Compression" } };

        if (morceaux[0] == "delay" && morceaux.size() > 1)
            return "Delay " + morceaux[1];

        if (morceaux[0] == "reverb" && morceaux.size() > 1)
            return "Réverbération "_fr + morceaux[1];

        if (auto n = noms.find (morceaux[0]); n != noms.end())
            return n->second;

        return morceaux.joinIntoString (" ");
    }

    const auto& champ = morceaux[2];

    if (champ == "macros" && morceaux.size() > 3)
        return "Macro " + juce::String (morceaux[3].getIntValue() + 1);

    if (champ == "effets" && morceaux.size() > 4)
        return "Effet " + juce::String (morceaux[3].getIntValue() + 1) + ", réglage "_fr + juce::String (morceaux[5].getIntValue() + 1);

    static const std::map<juce::String, juce::String> noms {
        { "volumeDb", "Volume" }, { "panoramique", "Panoramique" }, { "mute", "Coupée"_fr }, { "solo", "Solo" },
        { "envoiDelayDb", "Envoi au delay" }, { "envoiReverbDb", "Envoi à la réverbération"_fr }, { "swing", "Swing" } };

    if (auto n = noms.find (champ); n != noms.end())
        return n->second;

    return morceaux.joinIntoString (" ", 2);
}

} // namespace groovebox
