#include "Prise.h"

namespace groovebox
{

namespace
{
    const juce::StringArray nomsDesGenres { "scene", "arret", "preparation", "melange", "filtreGlobal", "envoiMomentane", "note", "reglages" };

    bool memeValeur (const juce::var& a, const juce::var& b)
    {
        if (a.isObject() || b.isObject() || a.isArray() || b.isArray())
            return false;

        if (a.isVoid() || b.isVoid())
            return a.isVoid() && b.isVoid();

        if (a.isString() || b.isString())
            return a.isString() && b.isString() && a.toString() == b.toString();

        return a.equalsWithSameType (b) || (double) a == (double) b;
    }

    void comparer (const juce::var& a, const juce::var& b, const juce::String& chemin, juce::Array<juce::var>& liste)
    {
        auto noter = [&] { liste.add (juce::Array<juce::var> { chemin, b }); };

        if (auto oa = a.getDynamicObject(); oa != nullptr && ! a.isArray())
        {
            auto ob = b.getDynamicObject();

            if (ob == nullptr || b.isArray())
                return noter();

            for (auto& p : ob->getProperties())
                comparer (oa->getProperty (p.name), p.value, chemin + "/" + p.name.toString(), liste);

            for (auto& p : oa->getProperties())
                if (! ob->hasProperty (p.name))
                    liste.add (juce::Array<juce::var> { chemin + "/" + p.name.toString(), juce::var() });

            return;
        }

        if (auto ta = a.getArray(), tb = b.getArray(); ta != nullptr && tb != nullptr)
        {
            if (ta->size() != tb->size())
                return noter();

            for (int i = 0; i < ta->size(); ++i)
                comparer ((*ta)[i], (*tb)[i], chemin + "/" + juce::String (i), liste);

            return;
        }

        if (! memeValeur (a, b))
            noter();
    }

    juce::var copieProfonde (const juce::var& v)
    {
        if (auto t = v.getArray())
        {
            juce::Array<juce::var> copie;

            for (auto& e : *t)
                copie.add (copieProfonde (e));

            return copie;
        }

        if (auto o = v.getDynamicObject())
        {
            auto copie = new juce::DynamicObject();

            for (auto& p : o->getProperties())
                copie->setProperty (p.name, copieProfonde (p.value));

            return juce::var (copie);
        }

        return v;
    }

    void ecrire (juce::var& racine, const juce::StringArray& morceaux, int k, const juce::var& valeur)
    {
        if (k >= morceaux.size())
        {
            racine = valeur;
            return;
        }

        const auto& cle = morceaux[k];

        if (auto t = racine.getArray())
        {
            const int i = cle.getIntValue();

            if (i >= 0 && i < t->size())
                ecrire (t->getReference (i), morceaux, k + 1, valeur);

            return;
        }

        if (! racine.isObject())
            racine = juce::var (new juce::DynamicObject());

        auto* o = racine.getDynamicObject();

        if (k == morceaux.size() - 1 && valeur.isVoid())
        {
            o->removeProperty (cle);
            return;
        }

        auto enfant = o->getProperty (cle);
        ecrire (enfant, morceaux, k + 1, valeur);
        o->setProperty (cle, enfant);
    }
}

juce::Array<juce::var> ecarts::entre (const juce::var& avant, const juce::var& apres)
{
    juce::Array<juce::var> liste;
    comparer (avant, apres, {}, liste);
    return liste;
}

juce::var ecarts::copie (const juce::var& v)
{
    return copieProfonde (v);
}

juce::var ecarts::appliquer (const juce::var& json, const juce::var& liste)
{
    auto resultat = copieProfonde (json);

    if (auto t = liste.getArray())
        for (auto& e : *t)
            if (auto paire = e.getArray(); paire != nullptr && paire->size() == 2)
                ecrire (resultat, juce::StringArray::fromTokens ((*paire)[0].toString(), "/", {}), 1, (*paire)[1]);

    return resultat;
}

//==============================================================================
juce::var EvenementDePrise::versJson() const
{
    auto o = new juce::DynamicObject();
    o->setProperty ("genre", nomsDesGenres[(int) genre]);
    o->setProperty ("temps", temps);

    if (cale)               o->setProperty ("cale", *cale);
    if (piste >= 0)         o->setProperty ("piste", piste);
    if (scene >= 0)         o->setProperty ("scene", scene);
    if (enchainement)       o->setProperty ("enchainement", enchainement->versJson());
    if (valeur != 0.0f)     o->setProperty ("valeur", valeur);
    if (velocite != 0)      o->setProperty ("velocite", velocite);
    if (! changements.isVoid()) o->setProperty ("changements", changements);

    return juce::var (o);
}

std::optional<EvenementDePrise> EvenementDePrise::depuisJson (const juce::var& v)
{
    const int g = nomsDesGenres.indexOf (v.getProperty ("genre", {}).toString());

    if (g < 0)
        return std::nullopt;

    EvenementDePrise e;
    e.genre = (Genre) g;
    e.temps = v.getProperty ("temps", 0.0);

    if (const auto c = v.getProperty ("cale", {}); ! c.isVoid())
        e.cale = (double) c;

    e.piste = v.getProperty ("piste", -1);
    e.scene = v.getProperty ("scene", -1);
    e.enchainement = Enchainement::depuisJson (v.getProperty ("enchainement", {}));
    e.valeur = (float) v.getProperty ("valeur", 0.0f);
    e.velocite = v.getProperty ("velocite", 0);
    e.changements = v.getProperty ("changements", {});
    return e;
}

//==============================================================================
double Prise::dureeEnSecondes() const
{
    const auto parSeconde = morceauDeDepart.tempo / 60.0 * morceauDeDepart.mesure.denominateur / 4.0;
    return juce::jmax (0.0, fin - debut) / parSeconde;
}

double Prise::secondesDansLeSon (double temps) const
{
    const auto parSeconde = [this] (double tempo) { return tempo / 60.0 * morceauDeDepart.mesure.denominateur / 4.0; };
    double tempo = morceauDeDepart.tempo;
    double position = debut;
    double secondes = secondesDuDebut >= 0.0 ? secondesDuDebut : debut / parSeconde (tempo);

    // Le tempo change là où il a été tourné (le moteur le prend 50 ms plus tard : négligeable ici)
    for (auto& e : evenements)
    {
        if (e.genre != EvenementDePrise::Genre::reglages || e.temps >= temps)
            continue;

        if (auto t = e.changements.getArray())
            for (auto& c : *t)
                if (auto paire = c.getArray(); paire != nullptr && paire->size() == 2 && (*paire)[0].toString() == "/tempo")
                {
                    const auto jusqua = juce::jmax (position, e.temps);
                    secondes += (jusqua - position) / parSeconde (tempo);
                    position = jusqua;
                    tempo = (double) (*paire)[1];
                }
    }

    secondes += (temps - position) / parSeconde (tempo);
    return secondes - debutDuSon;
}

juce::var Prise::versJson() const
{
    auto o = new juce::DynamicObject();
    o->setProperty ("format", "groovebox-prise");
    o->setProperty ("version", versionDuFormat);
    o->setProperty ("nom", nom);
    o->setProperty ("date", date.toISO8601 (true));
    o->setProperty ("debut", debut);
    o->setProperty ("fin", fin);
    o->setProperty ("debutDuSon", debutDuSon);
    o->setProperty ("secondesDuDebut", secondesDuDebut);
    o->setProperty ("avecMicros", avecMicros);

    juce::Array<juce::var> scenes;

    for (auto s : scenesDeDepart)
        scenes.add (s);

    o->setProperty ("scenesDeDepart", scenes);
    o->setProperty ("morceauDeDepart", morceauDeDepart.versJson());

    juce::Array<juce::var> liste;

    for (auto& e : evenements)
        liste.add (e.versJson());

    o->setProperty ("evenements", liste);
    return juce::var (o);
}

std::optional<Prise> Prise::depuisJson (const juce::var& v)
{
    if (v.getProperty ("format", {}).toString() != "groovebox-prise")
        return std::nullopt;

    Prise p;
    p.nom = v.getProperty ("nom", {}).toString();
    p.date = juce::Time::fromISO8601 (v.getProperty ("date", {}).toString());
    p.debut = v.getProperty ("debut", 0.0);
    p.fin = v.getProperty ("fin", 0.0);
    p.debutDuSon = v.getProperty ("debutDuSon", 0.0);
    p.secondesDuDebut = v.getProperty ("secondesDuDebut", -1.0);
    p.avecMicros = v.getProperty ("avecMicros", false);

    if (auto t = v.getProperty ("scenesDeDepart", {}).getArray())
        for (int i = 0; i < std::min (nombreDePistes, t->size()); ++i)
            p.scenesDeDepart[(size_t) i] = (*t)[i];

    p.morceauDeDepart = Morceau::depuisJson (v.getProperty ("morceauDeDepart", {}));

    if (auto t = v.getProperty ("evenements", {}).getArray())
        for (auto& e : *t)
            if (auto ev = EvenementDePrise::depuisJson (e))
                p.evenements.push_back (*ev);

    return p;
}

bool Prise::enregistrer (const juce::File& fichier) const
{
    juce::TemporaryFile temporaire (fichier);

    if (! temporaire.getFile().replaceWithText (juce::JSON::toString (versJson())))
        return false;

    return temporaire.overwriteTargetFileWithTemporary();
}

std::optional<Prise> Prise::charger (const juce::File& fichier)
{
    if (! fichier.existsAsFile())
        return std::nullopt;

    return depuisJson (juce::JSON::parse (fichier));
}

} // namespace groovebox
