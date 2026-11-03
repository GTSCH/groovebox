#include "EffetsMaison.h"

namespace groovebox
{

const char* FiltrePlugin::xmlTypeName = "groovebox_filtre";
const char* SaturationPlugin::xmlTypeName = "groovebox_saturation";
const char* DelayDubPlugin::xmlTypeName = "groovebox_delay_dub";
const char* LimiteurPlugin::xmlTypeName = "groovebox_limiteur";
const char* DistorsionPlugin::xmlTypeName = "groovebox_distorsion";
const char* PunchPlugin::xmlTypeName = "groovebox_punch";
const char* GravePlugin::xmlTypeName = "groovebox_grave";

namespace
{
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

//==============================================================================
void EffetMaison::initialise (const te::PluginInitialisationInfo& info)
{
    frequence = info.sampleRate;
    preparer();
}

void EffetMaison::applyToBuffer (const te::PluginRenderContext& fc)
{
    if (fc.destBuffer == nullptr)
        return;

    auto& b = *fc.destBuffer;
    const int canaux = b.getNumChannels();

    if (canaux == 0)
        return;

    auto* gauche = b.getWritePointer (0, fc.bufferStartSample);
    auto* droite = b.getWritePointer (canaux > 1 ? 1 : 0, fc.bufferStartSample);
    const float cible = actif.load() ? 1.0f : 0.0f;
    const float pas = (float) (1.0 / (0.01 * frequence));

    for (int i = 0; i < fc.bufferNumSamples; ++i)
    {
        melangeDuPassage = melangeDuPassage < cible ? juce::jmin (cible, melangeDuPassage + pas) : juce::jmax (cible, melangeDuPassage - pas);
        float g = gauche[i], d = canaux > 1 ? droite[i] : gauche[i];
        const float g0 = g, d0 = d;

        if (melangeDuPassage > 0.0f)
            traiter (g, d);

        gauche[i] = g0 + (g - g0) * melangeDuPassage;

        if (canaux > 1)
            droite[i] = d0 + (d - d0) * melangeDuPassage;
    }
}

//==============================================================================
std::pair<double, double> FiltrePlugin::coupures (float p)
{
    // Sous 0,5 : le passe-bas descend de 20 kHz à 80 Hz ; au-dessus : le passe-haut monte de 20 Hz à 5 kHz
    const double bas = p < 0.5f ? 20000.0 * std::pow (80.0 / 20000.0, (0.5 - p) / 0.5) : 20000.0;
    const double haut = p > 0.5f ? 20.0 * std::pow (5000.0 / 20.0, (p - 0.5) / 0.5) : 20.0;
    return { bas, haut };
}

void FiltrePlugin::programmerMontee (double debut, double fin, float cible)
{
    const juce::SpinLock::ScopedLockType l (verrouDeLaMontee);
    montee = { debut, fin, cible, true };
}

void FiltrePlugin::annulerMontee()
{
    const juce::SpinLock::ScopedLockType l (verrouDeLaMontee);
    montee.active = false;
}

void FiltrePlugin::applyToBuffer (const te::PluginRenderContext& fc)
{
    {
        const juce::SpinLock::ScopedTryLockType l (verrouDeLaMontee);

        if (l.isLocked())
            monteeAudio = montee;
    }

    positionVisee = position.load();
    const double t = fc.editTime.getStart().inSeconds();

    if (monteeAudio.active && fc.isPlaying && t >= monteeAudio.debut && t < monteeAudio.fin && monteeAudio.fin > monteeAudio.debut)
    {
        const auto x = (float) ((t - monteeAudio.debut) / (monteeAudio.fin - monteeAudio.debut));
        positionVisee += (monteeAudio.cible - positionVisee) * x * x;
    }

    EffetMaison::applyToBuffer (fc);
}

void FiltrePlugin::traiter (float& g, float& d)
{
    const auto p = lisser (positionLissee, positionVisee);
    const auto r = lisser (resonanceLissee, resonance.load());
    const auto [coupureBasse, coupureHaute] = coupures (p);
    const float k = 2.0f - 1.85f * r;      // de Q 0,5 à Q 6,7
    const float gb = coefficient (coupureBasse, frequence), gh = coefficient (coupureHaute, frequence);
    float* x[2] = { &g, &d };

    for (size_t c = 0; c < 2; ++c)
    {
        auto y = *x[c];

        if (p < 0.5f)
            y = etat (y, gb, k, bas1[c], bas2[c]).first;
        else if (p > 0.5f)
            y = etat (y, gh, k, haut1[c], haut2[c]).second;

        *x[c] = y;
    }
}

//==============================================================================
void SaturationPlugin::traiter (float& g, float& d)
{
    const auto q = lisser (quantiteLissee, quantite.load());
    const auto c = lisser (couleurLissee, couleur.load());
    const auto m = lisser (melangeLisse, melange.load());
    const float entrainement = 1.0f + 19.0f * q * q;
    const float compensation = 1.0f / std::tanh (entrainement) * (0.6f + 0.4f / entrainement);
    const float aigus = (c - 0.5f) * 2.0f;     // -1 sombre, +1 brillant
    const float a = 1.0f - std::exp (-juce::MathConstants<float>::twoPi * 1200.0f / (float) frequence);
    float* x[2] = { &g, &d };

    for (size_t k = 0; k < 2; ++k)
    {
        const float sec = *x[k];
        memoire[k] += a * (sec - memoire[k]);
        const float grave = memoire[k], aigu = sec - grave;

        // La couleur penche le son avant la saturation : plus d'aigus saturés, ou plus de graves
        const float avant = grave * (1.0f - 0.5f * aigus) + aigu * (1.0f + 0.8f * aigus);
        const float sature = std::tanh (avant * entrainement) * compensation;
        *x[k] = sec + (sature - sec) * m;
    }
}

//==============================================================================
float DistorsionPlugin::formeDeLaDistorsion (float x, float durete)
{
    // De 0 à 0,5 : de la saturation douce à l'écrêtage dur ; de 0,5 à 1 : le repliement (le son se replie sur lui-même, très agressif)
    const float douce = std::tanh (x);
    const float dure = juce::jlimit (-1.0f, 1.0f, x);

    if (durete <= 0.5f)
    {
        const float a = durete * 2.0f;
        return douce + (dure - douce) * a;
    }

    const float a = (durete - 0.5f) * 2.0f;
    const float replie = std::sin (juce::MathConstants<float>::halfPi * juce::jlimit (-4.0f, 4.0f, x));
    return dure + (replie - dure) * a;
}

void DistorsionPlugin::traiter (float& g, float& d)
{
    const auto gn = lisser (gainLisse, gain.load());
    const auto du = lisser (dureteLissee, durete.load());
    const auto to = lisser (tonaliteLissee, tonalite.load());
    const auto me = lisser (melangeLisse, melange.load());
    const float entree = juce::Decibels::decibelsToGain (40.0f * gn);

    // Tonalité : un passe-bas après la distorsion, de 1,5 kHz (sombre, lourd) à 20 kHz (tranchant)
    const float coupure = 1500.0f * std::pow (20000.0f / 1500.0f, to);
    const float a = 1.0f - std::exp (-juce::MathConstants<float>::twoPi * coupure / (float) frequence);
    float* x[2] = { &g, &d };

    for (size_t c = 0; c < 2; ++c)
    {
        const float sec = *x[c];

        // Une pointe d'asymétrie : des harmoniques paires, plus de matière
        float y = formeDeLaDistorsion (sec * entree + 0.15f * gn, du);

        // Le décalage ajouté est retiré ensuite (bloqueur de continu)
        const float sansContinu = y - precedent[c] + 0.995f * bloqueur[c];
        precedent[c] = y;
        bloqueur[c] = sansContinu;
        y = sansContinu;

        passeBas[c] += a * (y - passeBas[c]);
        y = passeBas[c] * 0.7f;     // la sortie distordue est toujours au plafond : on la ramène vers le niveau d'entrée

        *x[c] = sec + (y - sec) * me;
    }
}

//==============================================================================
void PunchPlugin::traiter (float& g, float& d)
{
    // Deux suiveurs d'enveloppe : la différence entre le rapide et le lent signale l'attaque d'un coup
    const float niveau = juce::jmax (std::abs (g), std::abs (d));
    const float fe = (float) frequence;
    rapide += (niveau - rapide) * (niveau > rapide ? 1.0f - std::exp (-1.0f / (0.0005f * fe)) : 1.0f - std::exp (-1.0f / (0.02f * fe)));
    lente += (niveau - lente) * (1.0f - std::exp (-1.0f / (0.06f * fe)));

    const float attaqueVoulue = (attaque.load() - 0.5f) * 2.0f;      // -1 adoucit, +1 renforce
    const float maintienVoulu = (maintien.load() - 0.5f) * 2.0f;
    const float transitoire = juce::jlimit (0.0f, 1.0f, (rapide - lente) / (lente + 1.0e-4f));
    float gain = 1.0f + attaqueVoulue * 2.5f * transitoire;

    // Maintien : on remonte ou on baisse la queue du son (quand il n'y a plus d'attaque)
    gain *= 1.0f + maintienVoulu * 0.8f * (1.0f - transitoire);
    gain = juce::jlimit (0.1f, 4.0f, gain);

    // Puissance : on pousse dans un écrêtage doux, avec une remontée mesurée
    const float p = puissance.load();
    const float entrainement = 1.0f + 7.0f * p;
    const float compensation = (0.8f + 0.6f * p) / std::tanh (entrainement);
    g = std::tanh (g * gain * entrainement) * compensation / entrainement * (1.0f + 1.5f * p);
    d = std::tanh (d * gain * entrainement) * compensation / entrainement * (1.0f + 1.5f * p);
}

//==============================================================================
void GravePlugin::traiter (float& g, float& d)
{
    const float q = lisser (quantiteLissee, quantite.load());
    const float f = 40.0f * std::pow (4.0f, frequenceDuGrave.load());      // de 40 à 160 Hz
    const float gainDuGrave = juce::Decibels::decibelsToGain (12.0f * q);
    const float s = 1.0f + 9.0f * saturation.load();
    const float a = 1.0f - std::exp (-juce::MathConstants<float>::twoPi * f / (float) frequence);
    float* x[2] = { &g, &d };

    for (size_t c = 0; c < 2; ++c)
    {
        // Les graves, à deux étages (12 dB par octave), renforcés puis saturés ; le reste du son est intact
        bas1[c] += a * (*x[c] - bas1[c]);
        bas2[c] += a * (bas1[c] - bas2[c]);
        const float grave = bas2[c];
        // Plus de saturation : plus de matière et un grave plus dense (remontée en racine du gain d'attaque)
        const float travaille = std::tanh (grave * gainDuGrave * s) / std::sqrt (s);
        *x[c] = *x[c] - grave + travaille;
    }
}

//==============================================================================
void DelayDubPlugin::preparer()
{
    const auto taille = (size_t) (frequence * 4.5);     // jusqu'à 4 s (une blanche à 30 BPM)
    memoireG.assign (taille, 0.0f);
    memoireD.assign (taille, 0.0f);
    ecriture = 0;
    tempsLisse = (float) tempsDeLEcho (tempo.load(), noires.load());
}

void DelayDubPlugin::traiter (float& g, float& d)
{
    const int taille = (int) memoireG.size();

    if (taille == 0)
        return;

    // Le temps glisse vers sa nouvelle valeur (en 0,3 s) : changer de division fait monter ou descendre l'écho, comme sur bande
    const float cible = (float) juce::jlimit (0.01, 4.0, tempsDeLEcho (juce::jmax (30.0f, tempo.load()), noires.load()));
    tempsLisse += (cible - tempsLisse) * (float) (1.0 - std::exp (-1.0 / (0.3 * frequence)));
    const float r = lisser (retourLisse, juce::jlimit (0.0f, 1.2f, retour.load()));

    const float lecture = (float) ecriture - tempsLisse * (float) frequence;
    auto lire = [taille, lecture] (const std::vector<float>& m)
    {
        float p = lecture;

        while (p < 0.0f)
            p += (float) taille;

        const int i = (int) p;
        const float f = p - (float) i;
        return m[(size_t) (i % taille)] * (1.0f - f) + m[(size_t) ((i + 1) % taille)] * f;
    };

    const float echoG = lire (memoireG), echoD = lire (memoireD);

    // Dans la boucle : filtre (passe-bas ou passe-haut selon le réglage) et saturation douce qui borne tout
    const auto [coupureBasse, coupureHaute] = FiltrePlugin::coupures (filtre.load());
    const float ab = 1.0f - std::exp (-juce::MathConstants<float>::twoPi * (float) coupureBasse / (float) frequence);
    const float ah = 1.0f - std::exp (-juce::MathConstants<float>::twoPi * (float) coupureHaute / (float) frequence);
    const float s = 1.0f + 3.0f * saturation.load();
    auto boucle = [&] (float echo, size_t c)
    {
        bas[c] += ab * (echo - bas[c]);
        haut[c] += ah * (bas[c] - haut[c]);
        const float filtre = bas[c] - haut[c];
        return std::tanh (filtre * r * s) / s;      // borné à ±1/s : l'écho s'emballe sans exploser
    };

    // Ping-pong léger : chaque côté nourrit un peu l'autre
    const float retourG = boucle (0.85f * echoG + 0.15f * echoD, 0);
    const float retourD = boucle (0.85f * echoD + 0.15f * echoG, 1);
    memoireG[(size_t) ecriture] = g + retourG;
    memoireD[(size_t) ecriture] = d + retourD;
    ecriture = (ecriture + 1) % taille;

    g = echoG;
    d = echoD;
}

//==============================================================================
void LimiteurPlugin::preparer()
{
    anticipation = juce::jmax (1, (int) (0.0015 * frequence));   // 1,5 ms
    retardG.assign ((size_t) anticipation, 0.0f);
    retardD.assign ((size_t) anticipation, 0.0f);
    gains.assign ((size_t) anticipation, 1.0f);
    position = 0;
}

void LimiteurPlugin::traiter (float& g, float& d)
{
    // Compresseur léger : seuil de -6 à -18 dB, rapport 2, remontée modérée
    const float q = compression.load();

    if (q > 0.0f)
    {
        const float niveau = juce::jmax (std::abs (g), std::abs (d));
        const float attaque = std::exp (-1.0f / (0.01f * (float) frequence)), relache = std::exp (-1.0f / (0.15f * (float) frequence));
        enveloppe = niveau > enveloppe ? attaque * enveloppe + (1.0f - attaque) * niveau : relache * enveloppe + (1.0f - relache) * niveau;
        const float seuil = juce::Decibels::decibelsToGain (-6.0f - 12.0f * q);
        float gain = enveloppe > seuil ? std::sqrt (seuil / enveloppe) : 1.0f;
        gain *= juce::Decibels::decibelsToGain (3.0f * q);
        g *= gain;
        d *= gain;
    }

    // Limiteur : on regarde 1,5 ms à l'avance, on baisse avant la crête, on remonte doucement
    const float crete = juce::jmax (std::abs (g), std::abs (d));
    const float voulu = crete > plafond ? plafond / crete : 1.0f;
    const auto n = (size_t) anticipation;
    gains[(size_t) position] = voulu;
    float minimum = 1.0f;

    for (size_t k = 0; k < n; ++k)
        minimum = juce::jmin (minimum, gains[k]);

    gainDuLimiteur = minimum < gainDuLimiteur ? minimum : gainDuLimiteur + (minimum - gainDuLimiteur) * (1.0f - std::exp (-1.0f / (0.08f * (float) frequence)));

    const float sortieG = retardG[(size_t) position] * gainDuLimiteur;
    const float sortieD = retardD[(size_t) position] * gainDuLimiteur;
    retardG[(size_t) position] = g;
    retardD[(size_t) position] = d;
    position = (position + 1) % anticipation;

    // Dernière butée : jamais au-dessus du plafond, quoi qu'il arrive
    g = juce::jlimit (-plafond, plafond, sortieG);
    d = juce::jlimit (-plafond, plafond, sortieD);
}

} // namespace groovebox
