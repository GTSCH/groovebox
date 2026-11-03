#include "FabriqueDeSons.h"

namespace groovebox::fabriqueDeSons
{

namespace
{
    /** Caractère d'un kit : tout le reste en découle. */
    struct Caractere
    {
        double longueur;        // facteur sur les durées
        double grave;           // fréquence de base de la grosse caisse
        double attaque;         // force du clic d'attaque
        float saturation;       // 0 à 1
        double brillance;       // coupure des bruits et des métaux
    };

    Caractere caractereDe (const juce::String& kit)
    {
        if (kit == "Minimal")       return { 0.55, 52.0, 0.9, 0.0f, 9000.0 };
        if (kit == "Dub")           return { 1.30, 44.0, 0.3, 0.05f, 4500.0 };
        if (kit == "Hard techno")   return { 0.90, 50.0, 1.0, 0.75f, 11000.0 };
        return { 1.0, 50.0, 0.6, 0.0f, 8000.0 };   // Analogique
    }

    /** Filtre à variable d'état : passe-bas, passe-haut ou passe-bande. */
    struct Filtre
    {
        enum class Mode { bas, haut, bande };

        Filtre (Mode m, double coupure, double fe, double q = 0.8)
            : mode (m), g (std::tan (juce::MathConstants<double>::pi * juce::jmin (coupure, fe * 0.45) / fe)), k (1.0 / q) {}

        float traiter (float x)
        {
            const auto a1 = 1.0 / (1.0 + g * (g + k));
            const auto v3 = x - v2;
            const auto v1n = a1 * v1 + g * a1 * v3;
            const auto v2n = v2 + g * v1n;
            v1 = 2.0 * v1n - v1;
            v2 = 2.0 * v2n - v2;

            switch (mode)
            {
                case Mode::bas:     return (float) v2n;
                case Mode::bande:   return (float) v1n;
                case Mode::haut:    return (float) (x - k * v1n - v2n);
            }

            return x;
        }

        Mode mode;
        double g, k, v1 = 0.0, v2 = 0.0;
    };

    struct Bruit
    {
        juce::uint32 etat = 22222;

        float operator()()
        {
            etat ^= etat << 13;
            etat ^= etat >> 17;
            etat ^= etat << 5;
            return (float) etat / 2147483648.0f - 1.0f;
        }
    };

    double env (double t, double duree)      { return std::exp (-t / juce::jmax (1.0e-4, duree)); }

    /** Six ondes carrées désaccordées : le métal des charlestons et cymbales des boîtes à rythmes classiques. */
    float metal (double t, double accord)
    {
        static constexpr double frequences[] = { 205.3, 304.4, 369.6, 522.7, 540.0, 800.0 };
        float s = 0.0f;

        for (auto f : frequences)
            s += std::fmod (t * f * accord, 1.0) < 0.5 ? 1.0f : -1.0f;

        return s / 6.0f;
    }
}

juce::AudioBuffer<float> fabriquer (const juce::String& kit, int pad, double fe)
{
    const auto c = caractereDe (kit);
    const auto L = c.longueur;
    const auto note = Kit::premiereNote + pad;

    // Durée du son, selon le pad
    double duree = 0.4;

    switch (note)
    {
        case 36: duree = 0.9 * L; break;
        case 49: duree = 2.2 * L; break;
        case 51: duree = 1.8 * L; break;
        case 46: duree = 0.8 * L; break;
        case 41: case 43: case 45: duree = 0.7 * L; break;
        default: duree = 0.5 * L; break;
    }

    const int n = (int) (duree * fe);
    juce::AudioBuffer<float> son (1, n);
    auto* d = son.getWritePointer (0);
    Bruit bruit;
    bruit.etat ^= (juce::uint32) (note * 2654435761u);
    double phase = 0.0;

    Filtre passeHaut (Filtre::Mode::haut, c.brillance * 0.8, fe, 0.7);
    Filtre bande (Filtre::Mode::bande, 1800.0, fe, 1.2);
    Filtre doux (Filtre::Mode::bas, c.brillance, fe, 0.7);
    Filtre bandeCloche (Filtre::Mode::bande, 2600.0, fe, 3.0);

    for (int i = 0; i < n; ++i)
    {
        const double t = i / fe;
        float x = 0.0f;

        auto sinus = [&] (double f)
        {
            phase += juce::MathConstants<double>::twoPi * f / fe;
            return (float) std::sin (phase);
        };

        switch (note)
        {
            case 36:    // Grosse caisse : sinus qui descend, clic d'attaque
            {
                const double f = c.grave + c.grave * 2.2 * env (t, 0.035 * L);
                x = sinus (f) * (float) env (t, 0.28 * L);
                x += (float) (c.attaque * 0.5 * env (t, 0.004)) * bruit();
                break;
            }

            case 37:    // Rimshot
                x = (float) ((0.6 * std::sin (juce::MathConstants<double>::twoPi * 1700.0 * t)
                              + 0.4 * std::sin (juce::MathConstants<double>::twoPi * 480.0 * t)) * env (t, 0.012 * L))
                    + bande.traiter (bruit()) * (float) env (t, 0.01);
                break;

            case 38:    // Caisse claire : deux tons et du bruit
            case 40:
            {
                const double base = note == 38 ? 180.0 : 230.0;
                const auto corps = 0.5 * std::sin (juce::MathConstants<double>::twoPi * base * t)
                                 + 0.3 * std::sin (juce::MathConstants<double>::twoPi * base * 1.8 * t);
                x = (float) (corps * env (t, 0.06 * L)) + 0.8f * doux.traiter (bruit()) * (float) env (t, (note == 38 ? 0.14 : 0.09) * L);
                break;
            }

            case 39:    // Clap : quatre rafales, puis la queue
            {
                const bool rafale = t < 0.032 && std::fmod (t, 0.0105) < 0.0035;
                const auto amplitude = t < 0.032 ? (rafale ? 1.0 : 0.15) : env (t - 0.032, 0.11 * L);
                x = bande.traiter (bruit()) * (float) amplitude * 2.0f;
                break;
            }

            case 41: case 43: case 45:      // Toms
            {
                const double base = note == 41 ? 95.0 : note == 43 ? 135.0 : 185.0;
                x = sinus (base * (1.0 + 0.5 * env (t, 0.05))) * (float) env (t, 0.22 * L);
                break;
            }

            case 42: case 46:               // Charlestons : métal passé en passe-haut
                x = passeHaut.traiter (0.6f * metal (t, 1.0) + 0.4f * bruit()) * (float) env (t, (note == 42 ? 0.035 : 0.28) * L);
                break;

            case 44:                        // Shaker
                x = passeHaut.traiter (bruit()) * (float) (juce::jmin (1.0, t / 0.012) * env (t, 0.06 * L));
                break;

            case 47:                        // Conga
                x = sinus (320.0 * (1.0 + 0.25 * env (t, 0.02))) * (float) env (t, 0.15 * L);
                break;

            case 48:                        // Cloche : deux carrés à 540 et 800 Hz
            {
                const auto carres = (std::fmod (t * 540.0, 1.0) < 0.5 ? 1.0f : -1.0f) + (std::fmod (t * 800.0, 1.0) < 0.5 ? 1.0f : -1.0f);
                x = bandeCloche.traiter (carres * 0.5f) * (float) (0.6 * env (t, 0.02) + 0.4 * env (t, 0.25 * L)) * 2.5f;
                break;
            }

            case 49:                        // Crash
                x = passeHaut.traiter (0.5f * metal (t, 1.3) + 0.5f * bruit()) * (float) env (t, 0.7 * L);
                break;

            case 50:                        // Perc : petit coup en modulation de fréquence
            {
                const auto mod = std::sin (juce::MathConstants<double>::twoPi * 870.0 * t) * 3.0 * env (t, 0.03);
                x = (float) (std::sin (juce::MathConstants<double>::twoPi * 610.0 * t + mod) * env (t, 0.09 * L));
                break;
            }

            case 51:                        // Ride
                x = passeHaut.traiter (0.7f * metal (t, 1.6) + 0.3f * bruit()) * (float) (0.4 * env (t, 0.01) + 0.6 * env (t, 0.6 * L));
                break;

            default:
                break;
        }

        // Caractère du kit : saturation
        if (c.saturation > 0.0f)
        {
            const auto entrainement = 1.0f + 6.0f * c.saturation;
            x = std::tanh (x * entrainement) / std::tanh (entrainement);
        }

        d[i] = x;
    }

    // Petite rampe de sortie (pas de clic en fin de fichier), puis crête à -1 dB
    const int rampe = juce::jmin (n, (int) (0.005 * fe));

    for (int i = 0; i < rampe; ++i)
        d[n - 1 - i] *= (float) i / (float) rampe;

    if (const auto crete = son.getMagnitude (0, 0, n); crete > 0.0f)
        son.applyGain (juce::Decibels::decibelsToGain (-1.0f) / crete);

    return son;
}

int assurerKitsDeDepart (const juce::File& dossierSons)
{
    int ecrits = 0;
    juce::WavAudioFormat wav;

    for (auto& nom : kitsDeDepart::noms())
    {
        const auto kit = kitsDeDepart::kit (nom);

        for (int pad = 0; pad < Kit::nombreDePads; ++pad)
        {
            auto fichier = dossierSons.getChildFile (kit.pads[(size_t) pad].son);

            if (fichier.existsAsFile())
                continue;

            fichier.getParentDirectory().createDirectory();
            const auto son = fabriquer (nom, pad);

            std::unique_ptr<juce::OutputStream> flux (fichier.createOutputStream());

            if (flux == nullptr)
                continue;

            if (auto ecrivain = std::unique_ptr<juce::AudioFormatWriter> (wav.createWriterFor (flux.get(), 48000.0, 1, 24, {}, 0)))
            {
                flux.release();     // l'écrivain possède maintenant le flux
                ecrivain->writeFromAudioSampleBuffer (son, 0, son.getNumSamples());
                ++ecrits;
            }
        }
    }

    return ecrits;
}

} // namespace groovebox::fabriqueDeSons
