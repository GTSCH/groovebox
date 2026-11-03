#include "Mesure.h"

namespace groovebox
{

std::vector<std::vector<int>> Mesure::groupementsProposes (int num, int den)
{
    if (den >= 8)
    {
        switch (num)
        {
            case 5:  return { { 2, 3 }, { 3, 2 } };
            case 6:  return { { 3, 3 }, { 2, 2, 2 } };
            case 7:  return { { 2, 2, 3 }, { 3, 2, 2 }, { 2, 3, 2 } };
            case 8:  return { { 3, 3, 2 }, { 2, 2, 2, 2 }, { 2, 3, 3 } };
            case 9:  return { { 3, 3, 3 }, { 2, 2, 2, 3 }, { 3, 2, 2, 2 } };
            case 10: return { { 3, 3, 2, 2 }, { 2, 2, 3, 3 }, { 3, 2, 3, 2 } };
            case 11: return { { 3, 3, 3, 2 }, { 2, 2, 3, 2, 2 }, { 3, 3, 2, 3 } };
            case 12: return { { 3, 3, 3, 3 }, { 2, 2, 2, 2, 2, 2 } };
            default: break;
        }
    }
    else
    {
        switch (num)
        {
            case 4:  return { { 4 }, { 2, 2 } };
            case 5:  return { { 3, 2 }, { 2, 3 }, { 5 } };
            case 6:  return { { 3, 3 }, { 2, 2, 2 }, { 6 } };
            case 7:  return { { 4, 3 }, { 3, 4 }, { 2, 2, 3 } };
            default: break;
        }
    }

    return { { num } };
}

Mesure Mesure::standard (int num, int den)
{
    return { num, den, groupementsProposes (num, den).front() };
}

std::vector<Mesure> Mesure::mesuresProposees()
{
    return { standard (4, 4), standard (3, 4), standard (5, 4), standard (6, 8),
             standard (7, 8), standard (9, 8), standard (12, 8) };
}

std::optional<std::vector<int>> Mesure::lireGroupement (const juce::String& texte)
{
    auto morceaux = juce::StringArray::fromTokens (texte.removeCharacters (" "), "+", "");
    std::vector<int> groupement;

    for (auto& m : morceaux)
    {
        if (m.isEmpty() || ! m.containsOnly ("0123456789"))
            return std::nullopt;

        groupement.push_back (m.getIntValue());
    }

    if (groupement.empty())
        return std::nullopt;

    return groupement;
}

bool Mesure::estValide() const
{
    if (numerateur < 1 || numerateur > 32)
        return false;

    if (denominateur != 2 && denominateur != 4 && denominateur != 8 && denominateur != 16)
        return false;

    int total = 0;

    for (auto g : groupement)
    {
        if (g < 1)
            return false;

        total += g;
    }

    return total == numerateur;
}

juce::String Mesure::nom() const
{
    return juce::String (numerateur) + "/" + juce::String (denominateur);
}

juce::String Mesure::nomGroupement (const std::vector<int>& groupement)
{
    juce::StringArray morceaux;

    for (auto g : groupement)
        morceaux.add (juce::String (g));

    return morceaux.joinIntoString ("+");
}

juce::String Mesure::nomGroupement() const
{
    return nomGroupement (groupement);
}

int Mesure::grilleParDefaut() const
{
    return denominateur >= 8 ? denominateur : 16;
}

std::vector<TypeDeTemps> Mesure::typesDesTemps() const
{
    std::vector<TypeDeTemps> types ((size_t) numerateur, TypeDeTemps::autre);
    int debut = 0;

    for (auto g : groupement)
    {
        if (debut < numerateur)
            types[(size_t) debut] = TypeDeTemps::groupe;

        debut += g;
    }

    if (! types.empty())
        types.front() = TypeDeTemps::premier;

    return types;
}

std::vector<CoupDeClic> Mesure::motifDeClic (bool unClicParGroupe, bool subdivisions) const
{
    std::vector<CoupDeClic> coups;
    const auto types = typesDesTemps();

    for (int i = 0; i < numerateur; ++i)
    {
        const auto type = types[(size_t) i];

        if (unClicParGroupe)
        {
            // Un clic au début de chaque groupe ; les subdivisions sont alors les autres temps du groupe
            if (type != TypeDeTemps::autre)
                coups.push_back ({ (double) i, type });
            else if (subdivisions)
                coups.push_back ({ (double) i, TypeDeTemps::subdivision });
        }
        else
        {
            coups.push_back ({ (double) i, type });

            if (subdivisions)
                coups.push_back ({ i + 0.5, TypeDeTemps::subdivision });
        }
    }

    return coups;
}

} // namespace groovebox
