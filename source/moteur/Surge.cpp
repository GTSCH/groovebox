#include "Surge.h"

namespace groovebox::surge
{

juce::StringArray dossiersCherches()
{
    return { "/Library/Audio/Plug-Ins/VST3/Surge XT.vst3",
             juce::File::getSpecialLocation (juce::File::userHomeDirectory).getChildFile ("Library/Audio/Plug-Ins/VST3/Surge XT.vst3").getFullPathName() };
}

namespace
{
    bool autorise = true;
}

void autoriser (bool a)
{
    autorise = a;
}

std::optional<juce::PluginDescription> trouver (te::Engine& engine)
{
    static std::optional<juce::PluginDescription> trouve;
    static bool cherche = false;

    if (! autorise)
        return std::nullopt;

    if (cherche)
        return trouve;

    cherche = true;
    juce::VST3PluginFormat vst3;

    for (auto& chemin : dossiersCherches())
    {
        if (! juce::File (chemin).exists())
            continue;

        juce::OwnedArray<juce::PluginDescription> types;
        vst3.findAllTypesForFile (types, chemin);

        for (auto* t : types)
            if (t->isInstrument && t->name.containsIgnoreCase ("Surge XT") && ! t->name.containsIgnoreCase ("Effects"))
            {
                trouve = *t;
                engine.getPluginManager().knownPluginList.addType (*t);
                juce::Logger::writeToLog ("Surge XT trouvé : "_fr + chemin + ", version " + t->version);
                return trouve;
            }
    }

    juce::Logger::writeToLog ("Surge XT introuvable : son provisoire sur les pistes de synthé"_fr);
    return trouve;
}

te::Plugin::Ptr creer (te::Edit& edit)
{
    const auto description = trouver (edit.engine);

    if (! description)
        return {};

    auto plugin = edit.getPluginCache().createNewPlugin (te::ExternalPlugin::xmlTypeName, *description);

    if (auto externe = dynamic_cast<te::ExternalPlugin*> (plugin.get()); externe == nullptr || externe->getAudioPluginInstance() == nullptr)
    {
        juce::Logger::writeToLog ("Surge XT ne se charge pas"_fr);
        return {};
    }

    return plugin;
}

std::map<juce::String, te::AutomatableParameter*> parametres (te::Plugin& plugin)
{
    std::map<juce::String, te::AutomatableParameter*> liste;

    for (auto* p : plugin.getAutomatableParameters())
        liste[p->getParameterName()] = p;

    return liste;
}

} // namespace groovebox::surge
