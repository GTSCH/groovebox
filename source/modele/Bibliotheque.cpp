#include "Bibliotheque.h"

namespace groovebox
{

Bibliotheque::Bibliotheque (juce::File r)  : racine (std::move (r))
{
    racine.createDirectory();
}

juce::File Bibliotheque::racineParDefaut()
{
    // Sur le Mac, hors du OneDrive : Musique/groovebox/Morceaux
    return juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("groovebox").getChildFile ("Morceaux");
}

juce::String Bibliotheque::nettoyer (const juce::String& nom)
{
    auto propre = juce::File::createLegalFileName (nom.trim()).trimCharactersAtStart (".");
    return propre.isEmpty() ? juce::String ("Sans titre") : propre;
}

juce::File Bibliotheque::dossierDe (const juce::String& nom) const
{
    return racine.getChildFile (nettoyer (nom));
}

bool Bibliotheque::existe (const juce::String& nom) const
{
    return dossierDe (nom).getChildFile ("morceau.json").existsAsFile();
}

juce::StringArray Bibliotheque::noms() const
{
    juce::StringArray liste;

    for (auto& d : racine.findChildFiles (juce::File::findDirectories, false))
        if (d.getChildFile ("morceau.json").existsAsFile())
            liste.add (d.getFileName());

    liste.sortNatural();
    return liste;
}

juce::String Bibliotheque::nomLibre (const juce::String& base) const
{
    for (int i = 1;; ++i)
    {
        const auto nom = base + " " + juce::String (i);

        if (! dossierDe (nom).exists())
            return nom;
    }
}

std::optional<Morceau> Bibliotheque::ouvrir (const juce::String& nom) const
{
    auto m = Morceau::charger (dossierDe (nom).getChildFile ("morceau.json"));

    if (m)
        m->nom = dossierDe (nom).getFileName();

    return m;
}

bool Bibliotheque::enregistrer (const Morceau& m)
{
    auto dossier = dossierDe (m.nom);

    if (! dossier.createDirectory())
        return false;

    if (! m.enregistrer (dossier.getChildFile ("morceau.json")))
        return false;

    // Copie de secours : une toutes les 5 minutes, les 20 dernières
    const auto maintenant = juce::Time::getMillisecondCounterHiRes() / 1000.0;
    auto& derniere = derniereCopie[dossier.getFileName()];

    if (derniere == 0.0 || maintenant - derniere >= intervalleDesCopies)
    {
        derniere = maintenant;
        auto copies = dossier.getChildFile ("sauvegardes");
        copies.createDirectory();
        m.enregistrer (copies.getChildFile (juce::Time::getCurrentTime().formatted ("%Y-%m-%d_%Hh%M.%S") + ".json"));

        auto anciennes = copies.findChildFiles (juce::File::findFiles, false, "*.json");
        std::sort (anciennes.begin(), anciennes.end(), [] (auto& a, auto& b) { return a.getFileName() < b.getFileName(); });

        for (int i = 0; i < anciennes.size() - nombreDeCopies; ++i)
            anciennes.getReference (i).deleteFile();
    }

    return true;
}

bool Bibliotheque::renommer (const juce::String& ancien, const juce::String& nouveau)
{
    if (nettoyer (ancien) == nettoyer (nouveau))
        return true;

    if (dossierDe (nouveau).exists())
        return false;

    return dossierDe (ancien).moveFileTo (dossierDe (nouveau));
}

juce::String Bibliotheque::dernierOuvert() const
{
    return racine.getChildFile ("dernier-morceau.txt").loadFileAsString().trim();
}

void Bibliotheque::retenirDernier (const juce::String& nom)
{
    racine.getChildFile ("dernier-morceau.txt").replaceWithText (nettoyer (nom));
}

bool Bibliotheque::marquerOuverture()
{
    auto marqueur = racine.getChildFile (".groovebox-ouvert");
    const bool plantage = marqueur.existsAsFile();
    marqueur.replaceWithText (juce::Time::getCurrentTime().toISO8601 (true));
    return plantage;
}

void Bibliotheque::marquerFermeture()
{
    racine.getChildFile (".groovebox-ouvert").deleteFile();
}

juce::StringArray Bibliotheque::copiesDeSecours (const juce::String& nom) const
{
    juce::StringArray liste;

    for (auto& f : dossierDe (nom).getChildFile ("sauvegardes").findChildFiles (juce::File::findFiles, false, "*.json"))
        liste.add (f.getFileName());

    liste.sort (false);
    return liste;
}

} // namespace groovebox
