#include "VoixPlugin.h"

namespace groovebox
{

const char* VoixPlugin::xmlTypeName = "groovebox_voix";

namespace
{
    constexpr int tailleDesTranches = 32;

    /** Filtre à variable d'état : un pas, renvoie (passe-bas, passe-haut). */
    std::pair<float, float> etat (float x, float g, float k, float& s1, float& s2)
    {
        const float a1 = 1.0f / (1.0f + g * (g + k));
        const float v3 = x - s2;
        const float v1 = a1 * s1 + g * a1 * v3;
        const float v2 = s2 + g * v1;
        s1 = 2.0f * v1 - s1;
        s2 = 2.0f * v2 - s2;
        return { v2, x - k * v1 - v2 };
    }

    float coefficient (double coupure, double fe)
    {
        return (float) std::tan (juce::MathConstants<double>::pi * juce::jlimit (10.0, fe * 0.45, coupure) / fe);
    }
}

float TrajetDeVoix::avancee (double instant) const
{
    if (fin <= debut)
        return instant >= debut ? pFin : pDebut;

    const auto x = juce::jlimit (0.0, 1.0, (instant - debut) / (fin - debut));
    return pDebut + (pFin - pDebut) * (float) x;
}

double VoixPlugin::coupureDuPasseBas (float position)
{
    return 60.0 * std::pow (20000.0 / 60.0, (double) juce::jlimit (0.0f, 1.0f, position));
}

double VoixPlugin::coupureDuPasseHaut (float position)
{
    return 20.0 * std::pow (2500.0 / 20.0, (double) juce::jlimit (0.0f, 1.0f, position));
}

void VoixPlugin::initialise (const te::PluginInitialisationInfo& info)
{
    frequence = info.sampleRate;
}

void VoixPlugin::programmer (const TrajetDeVoix& t)
{
    const juce::SpinLock::ScopedLockType l (verrou);
    programme.avant = programme.actuel;
    programme.actuel = t;
}

void VoixPlugin::reposer (float gain)
{
    const juce::SpinLock::ScopedLockType l (verrou);
    programme.actuel.reset();
    programme.avant.reset();
    programme.repos = gain;
}

FormeDeVoix VoixPlugin::formeA (const Programme& p, double instant) const
{
    if (p.actuel && (instant >= p.actuel->debut || ! p.avant))
        return p.actuel->forme (instant);

    if (p.avant)
        return p.avant->forme (instant);

    FormeDeVoix f;
    f.gain = p.repos;
    return f;
}

void VoixPlugin::viderLesFiltres()
{
    for (auto* f : { &bas1, &bas2, &haut1, &haut2, &gravesA1, &gravesA2, &gravesB1, &gravesB2 })
        f->fill (0.0f);
}

void VoixPlugin::applyToBuffer (const te::PluginRenderContext& fc)
{
    if (fc.destBuffer == nullptr)
        return;

    {
        const juce::SpinLock::ScopedTryLockType l (verrou);

        if (l.isLocked())
            programmeAudio = programme;
    }

    const auto& pr = programmeAudio;
    auto& b = *fc.destBuffer;
    const int canaux = juce::jmin (2, b.getNumChannels());
    const int n = fc.bufferNumSamples;
    const bool auRepos = ! pr.actuel && ! pr.avant;

    if (canaux == 0)
        return;

    // Au repos : rien à calculer
    if (auRepos && melangeDesFiltres == 0.0f && gainLisse == pr.repos)
    {
        if (pr.repos == 0.0f)
            b.clear (fc.bufferStartSample, n);
        else if (pr.repos != 1.0f)
            b.applyGain (fc.bufferStartSample, n, pr.repos);

        return;
    }

    const double t0 = fc.editTime.getStart().inSeconds();
    const float aGain = (float) (1.0 - std::exp (-1.0 / (0.003 * frequence)));
    const float aGraves = (float) (1.0 - std::exp (-1.0 / (0.008 * frequence)));
    const float pasDuMelange = (float) (1.0 / (0.01 * frequence));
    const float gGraves = coefficient (coupureDesGraves, frequence);
    float* x[2] = { b.getWritePointer (0, fc.bufferStartSample), b.getWritePointer (canaux > 1 ? 1 : 0, fc.bufferStartSample) };

    for (int debut = 0; debut < n; debut += tailleDesTranches)
    {
        const int m = juce::jmin (tailleDesTranches, n - debut);
        FormeDeVoix f;

        if (auRepos)
            f.gain = pr.repos;
        else
            f = formeA (pr, t0 + (debut + 0.5 * m) / frequence);

        const float cibleDuMelange = f.estNeutre() ? 0.0f : 1.0f;
        basLisse += (f.passeBas - basLisse) * 0.25f;
        hautLisse += (f.passeHaut - hautLisse) * 0.25f;
        const float gb = coefficient (coupureDuPasseBas (basLisse), frequence);
        const float gh = coefficient (coupureDuPasseHaut (hautLisse), frequence);

        for (int i = debut; i < debut + m; ++i)
        {
            gainLisse += (f.gain - gainLisse) * aGain;
            gravesLisses += (f.sansGraves - gravesLisses) * aGraves;
            melangeDesFiltres = melangeDesFiltres < cibleDuMelange ? juce::jmin (cibleDuMelange, melangeDesFiltres + pasDuMelange)
                                                                   : juce::jmax (cibleDuMelange, melangeDesFiltres - pasDuMelange);

            for (size_t c = 0; c < (size_t) canaux; ++c)
            {
                const float entree = x[c][i];
                float y = entree;

                // Pendant un trajet, les filtres tournent même quand on ne les entend pas : ils n'entrent jamais à vide
                if (melangeDesFiltres > 0.0f || ! auRepos)
                {
                    y = etat (y, gb, 1.2f, bas1[c], bas2[c]).first;
                    y = etat (y, gh, 1.2f, haut1[c], haut2[c]).second;

                    // Graves coupés : passe-haut raide (deux étages), mélangé peu à peu pour basculer sans clic
                    const float sansGraves = etat (etat (y, gGraves, 1.414f, gravesA1[c], gravesA2[c]).second,
                                                   gGraves, 1.414f, gravesB1[c], gravesB2[c]).second;
                    y += (sansGraves - y) * gravesLisses;
                    y = entree + (y - entree) * melangeDesFiltres;
                }

                x[c][i] = y * gainLisse;
            }
        }

        if (melangeDesFiltres == 0.0f && auRepos)
            viderLesFiltres();
    }

    if (std::abs (gainLisse - pr.repos) < 1.0e-5f && auRepos)
        gainLisse = pr.repos;
}

} // namespace groovebox
