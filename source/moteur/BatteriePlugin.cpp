#include "BatteriePlugin.h"

namespace groovebox
{

const char* BatteriePlugin::xmlTypeName = "groovebox_batterie";

BatteriePlugin::BatteriePlugin (te::PluginCreationInfo info)  : te::Plugin (info) {}

BatteriePlugin::~BatteriePlugin()
{
    notifyListenersOfDeletion();
}

float BatteriePlugin::placementDuPad (int pad)
{
    // Grosse caisse, caisses claires et clap au centre ; charlestons à droite ; toms étalés
    static constexpr std::array<float, Kit::nombreDePads> placements {
        0.0f, -0.3f, 0.0f, 0.1f, -0.1f, -0.6f, 0.45f, -0.2f, 0.6f, 0.3f, 0.45f, -0.5f, 0.35f, -0.4f, 0.5f, 0.4f };
    return placements[(size_t) juce::jlimit (0, Kit::nombreDePads - 1, pad)];
}

void BatteriePlugin::initialise (const te::PluginInitialisationInfo& info)
{
    frequenceEchantillonnage = info.sampleRate;

    for (auto& pad : voix)
        for (auto& v : pad)
            v.active = false;
}

void BatteriePlugin::appliquerKit (const Kit& kit, BanqueDeSons& banque)
{
    compression = kit.compression;

    for (int i = 0; i < Kit::nombreDePads; ++i)
    {
        const auto& p = kit.pads[(size_t) i];
        auto& r = reglages[(size_t) i];
        r.son = banque.charger (p.son);
        r.vitesse = std::pow (2.0f, p.hauteur / 12.0f);

        // Décroissance 1 : le son entier ; en dessous, de 2 s à 30 ms, sur une échelle logarithmique
        r.dureeDeVie = p.decroissance >= 0.999f ? 0.0f : 0.03f * std::pow (2.0f / 0.03f, p.decroissance);
        r.saturation = p.saturation;
        r.couleur = p.couleur;
        r.gain = juce::Decibels::decibelsToGain (p.volumeDb);
        r.panoramique = placementDuPad (i) * kit.largeur;
        r.etouffement = p.etouffement;
    }
}

void BatteriePlugin::frapper (int pad, float velocite)
{
    auto& r = reglages[(size_t) pad];
    const auto* son = r.son.load();

    if (son == nullptr)
        return;

    const auto pasDeFondu = (float) (1.0 / (0.003 * frequenceEchantillonnage));

    // Groupe d'étouffement : les autres pads du groupe s'effacent en 3 ms
    if (const int groupe = r.etouffement.load(); groupe > 0)
        for (int autre = 0; autre < Kit::nombreDePads; ++autre)
            if (autre != pad && reglages[(size_t) autre].etouffement.load() == groupe)
                for (auto& v : voix[(size_t) autre])
                    if (v.active)
                        v.pasDeFondu = pasDeFondu;

    // La voix qui jouait s'efface ; la nouvelle prend sa place
    auto& paire = voix[(size_t) pad];

    if (paire[0].active)
    {
        paire[1] = paire[0];
        paire[1].pasDeFondu = pasDeFondu;
    }

    auto& v = paire[0];
    v = Voix {};
    v.active = true;
    v.son = son;
    v.pas = r.vitesse.load() * son->frequence / frequenceEchantillonnage;
    v.gain = std::pow (velocite, 1.4f) * r.gain.load();
    v.dureeDeVie = r.dureeDeVie.load();
}

void BatteriePlugin::rendre (juce::AudioBuffer<float>& sortie, int debut, int nombre)
{
    const auto fe = (float) frequenceEchantillonnage;
    const auto pasDAge = 1.0 / frequenceEchantillonnage;
    const auto canaux = sortie.getNumChannels();
    const auto quantite = compression.load();

    // Compresseur du kit : seuil de -6 à -30 dB, rapport 4, remontée du niveau
    const auto seuil = juce::Decibels::decibelsToGain (-6.0f - 24.0f * quantite);
    const auto remontee = juce::Decibels::decibelsToGain (8.0f * quantite);
    const auto attaque = std::exp (-1.0f / (0.005f * fe)), relache = std::exp (-1.0f / (0.08f * fe));

    for (int i = debut; i < debut + nombre; ++i)
    {
        float gauche = 0.0f, droite = 0.0f;

        for (int pad = 0; pad < Kit::nombreDePads; ++pad)
        {
            const auto& r = reglages[(size_t) pad];

            for (auto& v : voix[(size_t) pad])
            {
                if (! v.active)
                    continue;

                const auto& audio = v.son->audio;
                const int index = (int) v.position;

                if (index + 1 >= audio.getNumSamples())
                {
                    v.active = false;
                    continue;
                }

                // Lecture avec interpolation linéaire, mélange en mono
                const auto fraction = (float) (v.position - index);
                float x = 0.0f;

                for (int c = 0; c < audio.getNumChannels(); ++c)
                {
                    const auto* d = audio.getReadPointer (c);
                    x += d[index] + fraction * (d[index + 1] - d[index]);
                }

                x /= (float) audio.getNumChannels();

                // Décroissance raccourcie : enveloppe exponentielle
                if (v.dureeDeVie > 0.0)
                {
                    x *= (float) std::exp (-v.age / (v.dureeDeVie * 0.35));

                    if (v.age > v.dureeDeVie)
                        v.pasDeFondu = juce::jmax (v.pasDeFondu, 1.0f / (0.01f * fe));
                }

                // Couleur : vers le sombre, un passe-bas ; vers le brillant, on retire du grave
                const auto couleur = r.couleur.load();

                if (couleur < 0.0f)
                {
                    const auto coupure = 18000.0f * std::pow (300.0f / 18000.0f, -couleur);
                    const auto a = 1.0f - std::exp (-juce::MathConstants<float>::twoPi * coupure / fe);
                    // Deux étages : 12 dB par octave, un vrai son étouffé
                    v.bas += a * (x - v.bas);
                    v.bas2 += a * (v.bas - v.bas2);
                    x = v.bas2;
                }
                else if (couleur > 0.0f)
                {
                    const auto coupure = 20.0f * std::pow (1500.0f / 20.0f, couleur);
                    const auto a = 1.0f - std::exp (-juce::MathConstants<float>::twoPi * coupure / fe);
                    v.haut += a * (x - v.haut);
                    x -= v.haut;
                }

                // Saturation
                if (const auto s = r.saturation.load(); s > 0.0f)
                {
                    const auto entrainement = 1.0f + 9.0f * s;
                    x = std::tanh (x * entrainement) / std::tanh (entrainement);
                }

                x *= v.gain * v.fondu;

                if (v.pasDeFondu > 0.0f)
                {
                    v.fondu -= v.pasDeFondu;

                    if (v.fondu <= 0.0f)
                        v.active = false;
                }

                // Panoramique à puissance constante
                const auto angle = (r.panoramique.load() + 1.0f) * juce::MathConstants<float>::pi * 0.25f;
                gauche += x * std::cos (angle);
                droite += x * std::sin (angle);

                v.position += v.pas;
                v.age += pasDAge;
            }
        }

        if (quantite > 0.0f)
        {
            const auto niveau = juce::jmax (std::abs (gauche), std::abs (droite));
            enveloppeDuCompresseur = niveau > enveloppeDuCompresseur ? attaque * enveloppeDuCompresseur + (1.0f - attaque) * niveau
                                                                       : relache * enveloppeDuCompresseur + (1.0f - relache) * niveau;
            float g = 1.0f;

            if (enveloppeDuCompresseur > seuil)
                g = std::pow (enveloppeDuCompresseur / seuil, 1.0f / 4.0f - 1.0f);

            gauche *= g * remontee;
            droite *= g * remontee;
        }

        // Niveau de sortie du kit : de la marge pour la grosse caisse et les coups simultanés
        constexpr float niveauDeSortie = 0.5f;

        if (canaux > 1)
        {
            sortie.addSample (0, i, gauche * niveauDeSortie);
            sortie.addSample (1, i, droite * niveauDeSortie);
        }
        else if (canaux == 1)
        {
            sortie.addSample (0, i, 0.5f * (gauche + droite) * niveauDeSortie);
        }
    }
}

void BatteriePlugin::applyToBuffer (const te::PluginRenderContext& fc)
{
    if (fc.destBuffer == nullptr)
        return;

    auto& sortie = *fc.destBuffer;
    const int debut = fc.bufferStartSample;
    const int n = fc.bufferNumSamples;
    int position = 0;

    if (fc.bufferForMidiMessages != nullptr)
    {
        for (auto& m : *fc.bufferForMidiMessages)
        {
            if (! m.isNoteOn())
                continue;

            const int pad = m.getNoteNumber() - Kit::premiereNote;

            if (pad < 0 || pad >= Kit::nombreDePads)
                continue;

            const int instant = juce::jlimit (0, n, juce::roundToInt (m.getTimeStamp() * frequenceEchantillonnage));

            if (instant > position)
            {
                rendre (sortie, debut + position, instant - position);
                position = instant;
            }

            frapper (pad, m.getFloatVelocity());
        }
    }

    if (position < n)
        rendre (sortie, debut + position, n - position);
}

} // namespace groovebox
