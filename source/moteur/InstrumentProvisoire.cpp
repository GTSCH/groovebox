#include "InstrumentProvisoire.h"
#include "modele/Demo.h"

namespace groovebox
{

namespace
{
    using namespace notesBatterie;

    enum class Onde { scie, carre, sinus };

    struct Reglage
    {
        Onde onde;
        bool doubleDesaccorde;
        double attaque, declin, maintien, relache;  // secondes, sauf maintien (0 à 1)
        float coupure, profondeurFiltre;            // Hz
        float gain;
    };

    // Un réglage par son de synthé, rangés comme SonProvisoire
    const std::array<Reglage, 8> reglages {{
        { Onde::scie,  false, 0.001, 0.10, 0.0, 0.05,  800.0f, 3000.0f, 0.0f  },   // batterie (inutilisé)
        { Onde::scie,  false, 0.003, 0.25, 0.4, 0.08,  450.0f, 1600.0f, 0.42f },   // basse
        { Onde::scie,  true,  0.50,  1.00, 0.8, 1.20, 1600.0f,  600.0f, 0.13f },   // nappe
        { Onde::carre, false, 0.01,  0.30, 0.6, 0.20, 2400.0f, 1500.0f, 0.16f },   // lead
        { Onde::scie,  false, 0.002, 0.15, 0.0, 0.10, 1200.0f, 3500.0f, 0.20f },   // arpège
        { Onde::scie,  false, 0.001, 0.10, 0.0, 0.05,  800.0f, 3000.0f, 0.0f  },   // percussions (inutilisé)
        { Onde::scie,  true,  0.002, 0.20, 0.0, 0.15, 2200.0f, 2500.0f, 0.12f },   // stab
        { Onde::sinus, true,  2.00,  2.00, 0.7, 3.00, 4000.0f,    0.0f, 0.16f }    // texture
    }};

    float bruitBlanc (juce::uint32& etat)
    {
        etat ^= etat << 13;
        etat ^= etat >> 17;
        etat ^= etat << 5;
        return (float) etat / 2147483648.0f - 1.0f;
    }

    double frequenceDeLaNote (int note)
    {
        return 440.0 * std::pow (2.0, (note - 69) / 12.0);
    }

    bool estUneBatterie (int son)
    {
        return son == (int) SonProvisoire::batterie || son == (int) SonProvisoire::percussions;
    }

    double dureeDuCoup (int note)
    {
        switch (note)
        {
            case grosseCaisse:      return 0.45;
            case caisseClaire:      return 0.30;
            case clap:              return 0.30;
            case charleston:        return 0.08;
            case charlestonOuvert:  return 0.45;
            case rimshot:           return 0.06;
            case tom:               return 0.40;
            default:                return 0.20;
        }
    }
}

const char* InstrumentProvisoire::xmlTypeName = "groovebox_instrument_provisoire";

InstrumentProvisoire::InstrumentProvisoire (te::PluginCreationInfo info)  : te::Plugin (info) {}

InstrumentProvisoire::~InstrumentProvisoire()
{
    notifyListenersOfDeletion();
}

void InstrumentProvisoire::initialise (const te::PluginInitialisationInfo& info)
{
    frequenceEchantillonnage = info.sampleRate;

    for (auto& v : voix)
        v.active = false;
}

void InstrumentProvisoire::noteOn (int note, float velocite)
{
    const bool batterie = estUneBatterie (son.load());

    // Groupe d'étouffement : le charleston fermé coupe l'ouvert
    if (batterie && note == charleston)
        for (auto& v : voix)
            if (v.active && v.note == charlestonOuvert)
                v.active = false;

    // Une voix libre, sinon la plus ancienne
    Voix* choisie = &voix[0];

    for (auto& v : voix)
    {
        if (! v.active)
        {
            choisie = &v;
            break;
        }

        if (v.age > choisie->age)
            choisie = &v;
    }

    auto& v = *choisie;
    v = Voix {};
    v.active = true;
    v.note = note;
    v.velocite = velocite;
    v.frequence = frequenceDeLaNote (note);
    v.bruit = 0x9E3779B9u ^ (juce::uint32) (note * 7919);
}

void InstrumentProvisoire::noteOff (int note)
{
    if (estUneBatterie (son.load()))
        return;     // Les coups de batterie vont jusqu'au bout

    for (auto& v : voix)
        if (v.active && ! v.relachee && v.note == note)
        {
            v.relachee = true;
            v.ageAuRelache = v.age;
            v.niveauAuRelache = v.enveloppe;
        }
}

float InstrumentProvisoire::echantillonBatterie (Voix& v)
{
    const auto t = v.age;
    const auto fe = frequenceEchantillonnage;
    float x = 0.0f;

    auto ton = [&v, fe] (double f)
    {
        v.phase += juce::MathConstants<double>::twoPi * f / fe;
        return (float) std::sin (v.phase);
    };

    switch (v.note)
    {
        case grosseCaisse:
            x = ton (45.0 + 110.0 * std::exp (-t / 0.03)) * (float) std::exp (-t / 0.12) * 1.1f;
            break;

        case caisseClaire:
            x = 0.55f * bruitBlanc (v.bruit) * (float) std::exp (-t / 0.06) + 0.45f * ton (190.0) * (float) std::exp (-t / 0.04);
            break;

        case clap:
        {
            // Trois petites rafales, puis une queue
            const auto rafale = std::fmod (t, 0.011) < 0.004 && t < 0.03 ? 1.0 : 0.0;
            x = bruitBlanc (v.bruit) * (float) (t < 0.03 ? rafale : std::exp (-(t - 0.03) / 0.07)) * 0.7f;
            break;
        }

        case charleston:
        case charlestonOuvert:
        {
            // Bruit passé en passe-haut simple : on retire la partie lente
            const auto b = bruitBlanc (v.bruit);
            v.v1 += 0.35f * (b - v.v1);
            x = (b - v.v1) * (float) std::exp (-t / (v.note == charleston ? 0.018 : 0.12)) * 0.45f;
            break;
        }

        case rimshot:
            x = (0.6f * ton (820.0) + 0.4f * bruitBlanc (v.bruit)) * (float) std::exp (-t / 0.012) * 0.6f;
            break;

        case tom:
            x = ton (90.0 + 40.0 * std::exp (-t / 0.05)) * (float) std::exp (-t / 0.12) * 0.8f;
            break;

        default:
            x = ton (v.frequence) * (float) std::exp (-t / 0.05) * 0.4f;
            break;
    }

    if (t > dureeDuCoup (v.note))
        v.active = false;

    return x * (0.35f + 0.65f * v.velocite);
}

float InstrumentProvisoire::echantillonSynthe (Voix& v, int s)
{
    const auto& r = reglages[(size_t) s];
    const auto fe = frequenceEchantillonnage;
    const auto t = v.age;

    // Enveloppe ADSR
    float env;

    if (! v.relachee)
    {
        if (t < r.attaque)                  env = (float) (t / r.attaque);
        else if (t < r.attaque + r.declin)  env = (float) (1.0 - (1.0 - r.maintien) * (t - r.attaque) / r.declin);
        else                                env = (float) r.maintien;
    }
    else
    {
        const auto depuis = t - v.ageAuRelache;
        env = v.niveauAuRelache * (float) juce::jmax (0.0, 1.0 - depuis / r.relache);

        if (depuis >= r.relache)
            v.active = false;
    }

    if (! v.relachee && r.maintien <= 0.0 && t > r.attaque + r.declin)
        v.active = false;

    v.enveloppe = env;

    // Oscillateurs
    auto onde = [&r] (double phase)
    {
        switch (r.onde)
        {
            case Onde::scie:  return (float) (2.0 * phase - 1.0);
            case Onde::carre: return phase < 0.5 ? 0.7f : -0.7f;
            case Onde::sinus: return (float) std::sin (juce::MathConstants<double>::twoPi * phase);
        }

        return 0.0f;
    };

    v.phase += v.frequence / fe;
    v.phase -= std::floor (v.phase);
    float x = onde (v.phase);

    if (r.doubleDesaccorde)
    {
        v.phase2 += v.frequence * (r.onde == Onde::sinus ? 2.0 : 1.004) / fe;
        v.phase2 -= std::floor (v.phase2);
        x = 0.5f * (x + onde (v.phase2));
    }

    // Filtre passe-bas à variable d'état, ouvert par l'enveloppe
    const auto coupure = juce::jlimit (40.0f, (float) fe * 0.45f, r.coupure + r.profondeurFiltre * env);
    const auto g = (float) std::tan (juce::MathConstants<double>::pi * coupure / fe);
    const float k = 1.2f;
    const auto a1 = 1.0f / (1.0f + g * (g + k));
    const auto v3 = x - v.v2;
    const auto n1 = a1 * v.v1 + g * a1 * v3;
    const auto n2 = v.v2 + g * n1;
    v.v1 = 2.0f * n1 - v.v1;
    v.v2 = 2.0f * n2 - v.v2;

    return n2 * env * r.gain * (0.4f + 0.6f * v.velocite);
}

void InstrumentProvisoire::rendre (juce::AudioBuffer<float>& sortie, int debut, int nombre)
{
    const int s = son.load();
    const bool batterie = estUneBatterie (s);
    const auto pas = 1.0 / frequenceEchantillonnage;

    for (int i = debut; i < debut + nombre; ++i)
    {
        float valeur = 0.0f;

        for (auto& v : voix)
        {
            if (! v.active)
                continue;

            valeur += batterie ? echantillonBatterie (v) : echantillonSynthe (v, s);
            v.age += pas;
        }

        for (int canal = 0; canal < sortie.getNumChannels(); ++canal)
            sortie.addSample (canal, i, valeur);
    }
}

void InstrumentProvisoire::applyToBuffer (const te::PluginRenderContext& fc)
{
    if (fc.destBuffer == nullptr)
        return;

    auto& sortie = *fc.destBuffer;
    const int debut = fc.bufferStartSample;
    const int n = fc.bufferNumSamples;
    int position = 0;

    if (fc.bufferForMidiMessages != nullptr)
    {
        if (fc.bufferForMidiMessages->isAllNotesOff)
            for (auto& v : voix)
                if (v.active)
                    noteOff (v.note);

        for (auto& m : *fc.bufferForMidiMessages)
        {
            const int instant = juce::jlimit (0, n, juce::roundToInt (m.getTimeStamp() * frequenceEchantillonnage));

            if (instant > position)
            {
                rendre (sortie, debut + position, instant - position);
                position = instant;
            }

            if (m.isNoteOn())
                noteOn (m.getNoteNumber(), m.getFloatVelocity());
            else if (m.isNoteOff())
                noteOff (m.getNoteNumber());
            else if (m.isAllNotesOff() || m.isAllSoundOff())
                for (auto& v : voix)
                    if (v.active)
                        noteOff (v.note);
        }
    }

    if (position < n)
        rendre (sortie, debut + position, n - position);
}

} // namespace groovebox
