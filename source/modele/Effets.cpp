#include "Effets.h"

namespace groovebox
{

namespace
{
    const juce::StringArray clesDesEffets { "filtre", "saturation", "lofi", "compresseur", "echo", "chorus", "distorsion", "punch", "grave" };
}

juce::String nomDeLEffet (TypeEffet t)
{
    switch (t)
    {
        case TypeEffet::filtre:         return "Filtre";
        case TypeEffet::saturation:     return "Saturation";
        case TypeEffet::lofi:           return "Lo-fi";
        case TypeEffet::compresseur:    return "Compresseur";
        case TypeEffet::echo:           return "Écho court"_fr;
        case TypeEffet::chorus:         return "Chorus";
        case TypeEffet::distorsion:     return "Distorsion";
        case TypeEffet::punch:          return "Punch";
        case TypeEffet::grave:          return "Grave";
    }

    return {};
}

juce::StringArray Effet::nomsDesReglages (TypeEffet t)
{
    switch (t)
    {
        case TypeEffet::filtre:         return { "Filtre", "Résonance"_fr };
        case TypeEffet::saturation:     return { "Quantité"_fr, "Couleur", "Mélange"_fr };
        case TypeEffet::lofi:           return { "Bits", "Échantillon."_fr, "Mélange"_fr };
        case TypeEffet::compresseur:    return { "Quantité"_fr };
        case TypeEffet::echo:           return { "Temps", "Retour", "Mélange"_fr };
        case TypeEffet::chorus:         return { "Profondeur", "Vitesse", "Mélange"_fr };
        case TypeEffet::distorsion:     return { "Gain", "Dureté"_fr, "Tonalité"_fr, "Mélange"_fr };
        case TypeEffet::punch:          return { "Attaque", "Maintien", "Puissance" };
        case TypeEffet::grave:          return { "Quantité"_fr, "Fréquence"_fr, "Saturation" };
    }

    return {};
}

Effet Effet::parDefaut (TypeEffet t)
{
    Effet e;
    e.type = t;

    switch (t)
    {
        case TypeEffet::filtre:         e.reglages = { 0.5f, 0.2f, 0.0f, 0.0f }; break;
        case TypeEffet::saturation:     e.reglages = { 0.3f, 0.5f, 1.0f, 0.0f }; break;
        case TypeEffet::lofi:           e.reglages = { 0.4f, 0.3f, 1.0f, 0.0f }; break;
        case TypeEffet::compresseur:    e.reglages = { 0.4f, 0.0f, 0.0f, 0.0f }; break;
        case TypeEffet::echo:           e.reglages = { 0.3f, 0.3f, 0.3f, 0.0f }; break;
        case TypeEffet::chorus:         e.reglages = { 0.5f, 0.3f, 0.5f, 0.0f }; break;
        case TypeEffet::distorsion:     e.reglages = { 0.5f, 0.4f, 0.6f, 1.0f }; break;
        case TypeEffet::punch:          e.reglages = { 0.75f, 0.5f, 0.4f, 0.0f }; break;
        case TypeEffet::grave:          e.reglages = { 0.5f, 0.4f, 0.4f, 0.0f }; break;
    }

    return e;
}

juce::var Effet::versJson() const
{
    auto o = new juce::DynamicObject();
    o->setProperty ("type", clesDesEffets[(int) type]);
    o->setProperty ("reglages", juce::Array<juce::var> { reglages[0], reglages[1], reglages[2], reglages[3] });
    o->setProperty ("actif", actif);
    return juce::var (o);
}

std::optional<Effet> Effet::depuisJson (const juce::var& v)
{
    const auto i = clesDesEffets.indexOf (v.getProperty ("type", {}).toString());

    if (i < 0)
        return std::nullopt;

    Effet e = parDefaut ((TypeEffet) i);

    if (auto r = v.getProperty ("reglages", {}).getArray(); r != nullptr && r->size() == 4)
        for (int k = 0; k < 4; ++k)
            e.reglages[(size_t) k] = juce::jlimit (0.0f, 1.0f, (float) (*r)[k]);

    e.actif = v.getProperty ("actif", true);
    return e;
}

juce::StringArray DelayDub::nomsDesDivisions()
{
    return { "Croche pointée"_fr, "Noire", "Croche", "Triolet de noires", "Triolet de croches", "Blanche" };
}

} // namespace groovebox
