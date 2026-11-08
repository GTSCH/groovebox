#include "FabriqueDeSons.h"
#include "BatteriePlugin.h"

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

//==============================================================================
namespace
{
    bool ecrireWav (const juce::File& fichier, const juce::AudioBuffer<float>& son)
    {
        fichier.getParentDirectory().createDirectory();
        std::unique_ptr<juce::OutputStream> flux (fichier.createOutputStream());

        if (flux == nullptr)
            return false;

        juce::WavAudioFormat wav;

        if (auto ecrivain = std::unique_ptr<juce::AudioFormatWriter> (wav.createWriterFor (flux.get(), 48000.0, (unsigned int) son.getNumChannels(), 24, {}, 0)))
        {
            flux.release();
            return ecrivain->writeFromAudioSampleBuffer (son, 0, son.getNumSamples());
        }

        return false;
    }

    void normaliser (juce::AudioBuffer<float>& son, float crete = 0.85f)
    {
        if (const auto m = son.getMagnitude (0, son.getNumSamples()); m > 0.0f)
            son.applyGain (crete / m);

        // 5 ms de fondu aux deux bouts : pas de clic
        const int rampe = juce::jmin (240, son.getNumSamples() / 4);
        son.applyGainRamp (0, rampe, 0.0f, 1.0f);
        son.applyGainRamp (son.getNumSamples() - rampe, rampe, 1.0f, 0.0f);
    }

    /** Une boucle de batterie : un motif par pad, une lettre par pas (x fort, o moyen, . rien). */
    juce::AudioBuffer<float> boucle (const juce::String& kit, double tempo, int pasParMesure, double noiresParMesure, int mesures,
                                     const std::vector<std::pair<int, juce::String>>& motifs, double swing = 0.0)
    {
        const double fe = 48000.0;
        const double secondesParPas = noiresParMesure * 60.0 / tempo / pasParMesure;
        const int total = juce::roundToInt (mesures * pasParMesure * secondesParPas * fe);
        juce::AudioBuffer<float> son (2, total);
        son.clear();

        for (auto& [pad, motif] : motifs)
        {
            const auto coup = fabriquer (kit, pad, fe);
            const float placement = 0.35f * BatteriePlugin::placementDuPad (pad);
            const float gauche = std::cos ((placement + 1.0f) * juce::MathConstants<float>::pi * 0.25f);
            const float droite = std::sin ((placement + 1.0f) * juce::MathConstants<float>::pi * 0.25f);

            for (int pas = 0; pas < mesures * pasParMesure; ++pas)
            {
                const auto lettre = motif[pas % motif.length()];

                if (lettre != 'x' && lettre != 'o')
                    continue;

                const double decalage = (pas % 2 == 1) ? swing * secondesParPas : 0.0;
                const int debut = juce::roundToInt ((pas * secondesParPas + decalage) * fe);
                const float gain = lettre == 'x' ? 1.0f : 0.5f;
                // Le bout qui dépasse la fin revient au début : la boucle s'enchaîne avec elle-même
                for (int k = 0; k < coup.getNumSamples(); ++k)
                {
                    const int i = (debut + k) % total;
                    const float x = coup.getSample (0, k) * gain;
                    son.addSample (0, i, x * gauche);
                    son.addSample (1, i, x * droite);
                }

            }
        }

        if (const auto m = son.getMagnitude (0, son.getNumSamples()); m > 0.0f)
            son.applyGain (0.85f / m);

        return son;
    }

    juce::AudioBuffer<float> texture (const juce::String& nom)
    {
        const double fe = 48000.0, doDuMilieu = 261.6256;
        Bruit bruit;
        auto seconde = [fe] (double s) { return juce::roundToInt (s * fe); };

        if (nom == "Drone en do")
        {
            juce::AudioBuffer<float> son (2, seconde (6.0));
            for (int i = 0; i < son.getNumSamples(); ++i)
            {
                const double t = i / fe;
                float g = 0.0f, d = 0.0f;

                // Trois dents de scie un peu désaccordées, à bande limitée, et une octave en dessous
                for (int h = 1; h <= 24; ++h)
                {
                    const float a = 1.0f / (float) h;
                    g += a * (float) std::sin (juce::MathConstants<double>::twoPi * doDuMilieu * h * t * 0.997);
                    d += a * (float) std::sin (juce::MathConstants<double>::twoPi * doDuMilieu * h * t * 1.003);
                    const float c = a * (float) std::sin (juce::MathConstants<double>::twoPi * doDuMilieu * 0.5 * h * t);
                    g += 0.6f * c; d += 0.6f * c;
                }

                son.setSample (0, i, g);
                son.setSample (1, i, d);
            }

            // Passe-bas qui s'ouvre et se referme
            Filtre bg (Filtre::Mode::bas, 900.0, fe), bd (Filtre::Mode::bas, 900.0, fe);

            for (int i = 0; i < son.getNumSamples(); ++i)
            {
                if (i % 64 == 0)
                {
                    const double t = i / fe;
                    const double ouverture = 400.0 + 1800.0 * (0.5 - 0.5 * std::cos (juce::MathConstants<double>::twoPi * t / 6.0));
                    bg.g = bd.g = std::tan (juce::MathConstants<double>::pi * ouverture / fe);
                }

                son.setSample (0, i, bg.traiter (son.getSample (0, i)));
                son.setSample (1, i, bd.traiter (son.getSample (1, i)));
            }

            normaliser (son, 0.7f);
            son.applyGainRamp (0, seconde (0.3), 0.0f, 1.0f);
            return son;
        }

        if (nom == "Cloche en do")
        {
            juce::AudioBuffer<float> son (1, seconde (4.0));

            for (int i = 0; i < son.getNumSamples(); ++i)
            {
                const double t = i / fe;
                const double indice = 3.0 * env (t, 0.8);
                const double modulante = std::sin (juce::MathConstants<double>::twoPi * doDuMilieu * 3.5 * t);
                son.setSample (0, i, (float) (std::sin (juce::MathConstants<double>::twoPi * doDuMilieu * t + indice * modulante) * env (t, 1.2)));
            }

            normaliser (son);
            return son;
        }

        if (nom == "Corde en do")
        {
            // Corde pincée (Karplus-Strong)
            juce::AudioBuffer<float> son (1, seconde (3.0));
            const int periode = juce::roundToInt (fe / doDuMilieu);
            std::vector<float> ligne ((size_t) periode);

            for (auto& x : ligne)
                x = bruit();

            float precedent = 0.0f;

            for (int i = 0; i < son.getNumSamples(); ++i)
            {
                auto& x = ligne[(size_t) (i % periode)];
                const float y = 0.996f * 0.5f * (x + precedent);
                precedent = x;
                x = y;
                son.setSample (0, i, y);
            }

            normaliser (son);
            return son;
        }

        // Souffle : un bruit filtré qui respire
        juce::AudioBuffer<float> son (2, seconde (6.0));
        Filtre g (Filtre::Mode::bande, 700.0, fe, 1.5), d (Filtre::Mode::bande, 900.0, fe, 1.5);

        for (int i = 0; i < son.getNumSamples(); ++i)
        {
            const double t = i / fe;
            const auto souffle = (float) (0.3 + 0.7 * (0.5 - 0.5 * std::cos (juce::MathConstants<double>::twoPi * t / 3.0)));
            son.setSample (0, i, g.traiter (bruit()) * souffle);
            son.setSample (1, i, d.traiter (bruit()) * souffle);
        }

        normaliser (son, 0.6f);
        return son;
    }

    juce::AudioBuffer<float> oneShot (const juce::String& nom)
    {
        const double fe = 48000.0;
        auto seconde = [fe] (double s) { return juce::roundToInt (s * fe); };
        double phase = 0.0;
        auto avancer = [&phase, fe] (double f) { phase += f / fe; return std::sin (juce::MathConstants<double>::twoPi * phase); };

        if (nom == "Sirène dub"_fr)
        {
            juce::AudioBuffer<float> son (1, seconde (2.5));

            for (int i = 0; i < son.getNumSamples(); ++i)
            {
                const double t = i / fe;
                const double f = 600.0 * std::pow (2.0, 0.8 * std::sin (juce::MathConstants<double>::twoPi * 3.0 * t));
                const double carre = avancer (f) > 0.0 ? 1.0 : -1.0;
                son.setSample (0, i, (float) (carre * 0.5 * juce::jmin (1.0, (2.5 - t) / 0.3)));
            }

            Filtre f (Filtre::Mode::bas, 3000.0, fe);

            for (int i = 0; i < son.getNumSamples(); ++i)
                son.setSample (0, i, f.traiter (son.getSample (0, i)));

            normaliser (son, 0.7f);
            return son;
        }

        if (nom == "Laser")
        {
            juce::AudioBuffer<float> son (1, seconde (0.45));

            for (int i = 0; i < son.getNumSamples(); ++i)
            {
                const double t = i / fe;
                son.setSample (0, i, (float) (avancer (100.0 + 2400.0 * std::exp (-t / 0.07)) * env (t, 0.15)));
            }

            normaliser (son);
            return son;
        }

        if (nom == "Gong")
        {
            juce::AudioBuffer<float> son (2, seconde (5.0));
            static constexpr double partiels[] = { 1.0, 1.47, 2.09, 2.56, 3.23, 4.11 };

            for (int i = 0; i < son.getNumSamples(); ++i)
            {
                const double t = i / fe;
                double g = 0.0, d = 0.0;
                int k = 0;

                for (auto p : partiels)
                {
                    const double x = std::sin (juce::MathConstants<double>::twoPi * 110.0 * p * t * (1.0 + 0.002 * std::sin (t * 3.0))) * env (t, 2.5 / p) / (1.0 + k);
                    (k % 2 == 0 ? g : d) += x;
                    (k % 2 == 0 ? d : g) += 0.6 * x;
                    ++k;
                }

                son.setSample (0, i, (float) g);
                son.setSample (1, i, (float) d);
            }

            normaliser (son);
            return son;
        }

        if (nom == "Bulle")
        {
            juce::AudioBuffer<float> son (1, seconde (0.18));

            for (int i = 0; i < son.getNumSamples(); ++i)
            {
                const double t = i / fe;
                son.setSample (0, i, (float) (avancer (300.0 * std::exp (t / 0.06)) * env (t, 0.05)));
            }

            normaliser (son);
            return son;
        }

        // Ressort : une note qui vibre de plus en plus lentement
        juce::AudioBuffer<float> son (1, seconde (0.9));

        for (int i = 0; i < son.getNumSamples(); ++i)
        {
            const double t = i / fe;
            const double f = 180.0 * (1.0 + 0.25 * std::sin (juce::MathConstants<double>::twoPi * (18.0 - 12.0 * t) * t) * env (t, 0.6));
            son.setSample (0, i, (float) (avancer (f) * env (t, 0.35)));
        }

        normaliser (son);
        return son;
    }

    /** Fait dire une phrase par la synthèse vocale du Mac (commande say), en AIFF 48 kHz. */
    bool dire (const juce::String& voix, const juce::String& texte, const juce::File& fichier)
    {
        const juce::File say ("/usr/bin/say");

        if (! say.existsAsFile())
            return false;

        fichier.getParentDirectory().createDirectory();
        juce::ChildProcess processus;

        if (! processus.start (juce::StringArray { say.getFullPathName(), "-v", voix, "-o", fichier.getFullPathName(), "--data-format=BEI16@48000", texte }))
            return false;

        processus.waitForProcessToFinish (20000);
        return fichier.existsAsFile() && fichier.getSize() > 1000;
    }
}

const std::vector<BoucleDeDepart>& bouclesDeDepart()
{
    static const std::vector<BoucleDeDepart> boucles {
        { "Breakbeat 100", 100.0, 4, 4, 2 },
        { "Techno 130", 130.0, 4, 4, 2 },
        { "Dub 75", 75.0, 4, 4, 2 },
        { "Jungle 170", 170.0, 4, 4, 2 },
        { "Shuffle 90 en 6-8", 90.0, 6, 8, 2 },
    };
    return boucles;
}

int assurerSonsDuSampler (const juce::File& dossierSons, bool avecVoix)
{
    int ecrits = 0;
    auto manque = [&] (const juce::String& chemin) { return ! dossierSons.getChildFile (chemin).existsAsFile(); };

    // Pads : 0 grosse caisse, 1 rimshot, 2 caisse claire, 3 clap, 6 charleston fermé, 8 shaker, 10 charleston ouvert, 13 crash, 15 ride
    for (auto& b : bouclesDeDepart())
    {
        const auto chemin = "Boucles/" + b.nom + ".wav";

        if (! manque (chemin))
            continue;

        juce::AudioBuffer<float> son;

        if (b.nom == "Breakbeat 100")
            son = boucle ("Analogique", b.tempo, 16, 4.0, b.mesures, {
                { 0, "x.........x.....x.x.......o....." },
                { 2, "....x..o.o..x..o....x..o.o..x.o." },
                { 6, "x.x.x.x.x.x.x.x.x.x.x.x.x.x.xox." },
                { 10, "..............x...............x." } }, 0.06);
        else if (b.nom == "Techno 130")
            son = boucle ("Hard techno", b.tempo, 16, 4.0, b.mesures, {
                { 0, "x...x...x...x..." },
                { 3, "....x.......x..." },
                { 10, "..x...x...x...x." },
                { 15, "oooooooooooooooo" } });
        else if (b.nom == "Dub 75")
            son = boucle ("Dub", b.tempo, 16, 4.0, b.mesures, {
                { 0, "........x..............o.x......" },
                { 1, "........x......................x" },
                { 6, "x.o.x.o.x.o.x.o.x.o.x.o.x.o.x.oo" },
                { 10, "......x...............x........." } }, 0.12);
        else if (b.nom == "Jungle 170")
            son = boucle ("Minimal", b.tempo, 16, 4.0, b.mesures, {
                { 0, "x.........x.......x.x.....x....." },
                { 2, "....x..o.x.ox..o....x..o.x.ox.x." },
                { 15, "x.o.x.o.x.o.x.o.x.o.x.o.x.o.x.o." } });
        else
            son = boucle ("Analogique", b.tempo, 12, 3.0, b.mesures, {
                { 0, "x.....x..o.." },
                { 2, "......x....." },
                { 6, "x.ox.ox.ox.o" },
                { 8, "..o..o..o..o" } });

        ecrits += ecrireWav (dossierSons.getChildFile (chemin), son) ? 1 : 0;
    }

    for (auto& nom : { juce::String ("Drone en do"), juce::String ("Cloche en do"), juce::String ("Corde en do"), juce::String ("Souffle") })
        if (const auto chemin = "Textures/" + nom + ".wav"; manque (chemin))
            ecrits += ecrireWav (dossierSons.getChildFile (chemin), texture (nom)) ? 1 : 0;

    for (auto& nom : { juce::String ("Sirène dub"_fr), juce::String ("Laser"), juce::String ("Gong"), juce::String ("Bulle"), juce::String ("Ressort") })
        if (const auto chemin = "One-shots/" + nom + ".wav"; manque (chemin))
            ecrits += ecrireWav (dossierSons.getChildFile (chemin), oneShot (nom)) ? 1 : 0;

    dossierSons.getChildFile ("Mes sons").createDirectory();

    if (avecVoix)
    {
        const std::vector<std::tuple<juce::String, juce::String, juce::String>> phrases {
            { "Thomas", "Un, deux, trois, quatre", "Un deux trois quatre" },
            { "Thomas", "Allez !", "Allez" },
            { "Thomas", "Encore une fois", "Encore une fois" },
            { "Thomas", "Ouais", "Ouais" },
            { "Amélie"_fr, "Groove box", "Groovebox" },
            { "Amélie"_fr, "Tout le monde, les mains en l'air !", "Les mains en l'air" },
        };

        for (auto& [voix, texte, nom] : phrases)
            if (const auto chemin = "Voix/" + nom + ".aiff"; manque (chemin))
                ecrits += dire (voix, texte, dossierSons.getChildFile (chemin)) ? 1 : 0;
    }

    return ecrits;
}

} // namespace groovebox::fabriqueDeSons
