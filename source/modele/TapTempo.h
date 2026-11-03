#pragma once

#include <JuceHeader.h>

namespace groovebox
{

/** Tempo tapé : moyenne des intervalles entre les 4 dernières frappes. Une pause de plus de 2 s recommence le comptage. */
class TapTempo
{
public:
    /** Enregistre une frappe (en secondes) ; renvoie le tempo dès la deuxième frappe. */
    std::optional<double> taper (double secondes)
    {
        if (! frappes.empty() && secondes - frappes.back() > 2.0)
            frappes.clear();

        frappes.push_back (secondes);

        if (frappes.size() > 4)
            frappes.erase (frappes.begin());

        if (frappes.size() < 2)
            return std::nullopt;

        const auto intervalle = (frappes.back() - frappes.front()) / (double) (frappes.size() - 1);
        return 60.0 / intervalle;
    }

private:
    std::vector<double> frappes;
};

} // namespace groovebox
