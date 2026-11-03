#include "Enchainements.h"

namespace groovebox
{

namespace
{
    const juce::StringArray nomsDesTypes { "coupe", "fondu", "echangeDesBasses", "filtre", "queueDEcho" };
    constexpr float demiPi = juce::MathConstants<float>::halfPi;

    float borner (float x)      { return juce::jlimit (0.0f, 1.0f, x); }
}

juce::String nomDeLEnchainement (TypeEnchainement t)
{
    switch (t)
    {
        case TypeEnchainement::coupe:               return "Coupe";
        case TypeEnchainement::fondu:               return "Fondu";
        case TypeEnchainement::echangeDesBasses:    return "Échange des basses"_fr;
        case TypeEnchainement::filtre:              return "Filtre";
        case TypeEnchainement::queueDEcho:          return "Queue d'écho"_fr;
    }

    return "Coupe";
}

juce::var Enchainement::versJson() const
{
    auto o = new juce::DynamicObject();
    o->setProperty ("type", nomsDesTypes[(int) type]);
    o->setProperty ("mesures", mesures);
    o->setProperty ("batterieNette", batterieNette);
    o->setProperty ("intensite", intensite);
    o->setProperty ("montee", montee);
    return juce::var (o);
}

std::optional<Enchainement> Enchainement::depuisJson (const juce::var& v)
{
    if (! v.isObject())
        return std::nullopt;

    Enchainement e;

    if (const int t = nomsDesTypes.indexOf (v.getProperty ("type", {}).toString()); t >= 0)
        e.type = (TypeEnchainement) t;

    const int m = (int) v.getProperty ("mesures", e.mesures);

    if (std::find (dureesPossibles.begin(), dureesPossibles.end(), m) != dureesPossibles.end())
        e.mesures = m;

    e.batterieNette = v.getProperty ("batterieNette", e.batterieNette);
    e.intensite = borner ((float) v.getProperty ("intensite", e.intensite));
    e.montee = v.getProperty ("montee", e.montee);
    return e;
}

FormeDeVoix formeDeLaVoix (TypeEnchainement type, bool entrante, float p, float intensite)
{
    p = borner (p);
    intensite = borner (intensite);
    FormeDeVoix f;

    switch (type)
    {
        case TypeEnchainement::coupe:
            f.gain = entrante ? 1.0f : (p > 0.0f ? 0.0f : 1.0f);
            break;

        case TypeEnchainement::fondu:
            // À puissance constante : le volume perçu ne creuse pas au milieu
            f.gain = entrante ? std::sin (p * demiPi) : std::cos (p * demiPi);
            break;

        case TypeEnchainement::echangeDesBasses:
            // Le nouveau monte sans ses graves ; à mi-parcours les graves changent de main ; puis l'ancien descend
            if (entrante)
            {
                f.gain = std::sin (borner (2.0f * p) * demiPi);
                f.sansGraves = p < 0.5f ? 1.0f : 0.0f;
            }
            else
            {
                f.gain = std::cos (borner (2.0f * p - 1.0f) * demiPi);
                f.sansGraves = p < 0.5f ? 0.0f : 1.0f;
            }
            break;

        case TypeEnchainement::filtre:
            // L'ancien se ferme par le haut (passe-bas qui descend) ; le nouveau s'ouvre par le bas (passe-haut qui descend)
            if (entrante)
            {
                f.passeHaut = (0.3f + 0.6f * intensite) * (1.0f - p);
                f.gain = std::sin (borner (p / 0.25f) * demiPi);
            }
            else
            {
                f.passeBas = 1.0f - (0.35f + 0.6f * intensite) * p;
                f.gain = std::cos (borner ((p - 0.75f) / 0.25f) * demiPi);
            }
            break;

        case TypeEnchainement::queueDEcho:
            // À la bascule, le son direct de l'ancien se coupe ; ce qui part dans le delay s'éteint peu à peu
            if (entrante)
            {
                f.gain = 1.0f;
            }
            else
            {
                f.gain = p > 0.0f ? 0.0f : 1.0f;
                f.envoi = (0.4f + 0.6f * intensite) * (1.0f - p);
            }
            break;
    }

    return f;
}

} // namespace groovebox
