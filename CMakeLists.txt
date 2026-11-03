cmake_minimum_required (VERSION 3.22)

project (groovebox VERSION 0.1.0 LANGUAGES C CXX)

set (CMAKE_OSX_DEPLOYMENT_TARGET "13.0" CACHE STRING "" FORCE)
set (CMAKE_OSX_ARCHITECTURES "arm64" CACHE STRING "" FORCE)

add_subdirectory (externe/tracktion_engine/modules/juce)
add_subdirectory (externe/tracktion_engine/modules)

# Code commun à l'application et aux essais
set (SOURCES_MOTEUR
    source/moteur/BipPlugin.cpp
    source/moteur/Moteur.cpp
    source/moteur/Reglages.cpp)

set (DEFINITIONS_COMMUNES
    JUCE_PLUGINHOST_AU=1
    JUCE_PLUGINHOST_VST3=1
    JUCE_USE_CURL=0
    JUCE_WEB_BROWSER=0
    JUCE_STRICT_REFCOUNTEDPOINTER=1
    JUCE_DISPLAY_SPLASH_SCREEN=0
    TRACKTION_ENABLE_TIMESTRETCH_SOUNDTOUCH=1)

set (MODULES_COMMUNS
    tracktion::tracktion_core
    tracktion::tracktion_engine
    tracktion::tracktion_graph
    juce::juce_audio_devices
    juce::juce_audio_processors
    juce::juce_audio_utils
    juce::juce_recommended_config_flags)

# Application
juce_add_gui_app (groovebox
    PRODUCT_NAME "groovebox"
    COMPANY_NAME "Guillaume Tschupp"
    BUNDLE_ID "com.guillaumetschupp.groovebox"
    VERSION ${PROJECT_VERSION}
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "groovebox enregistre la batterie pendant les répétitions."
    PLUGINHOST_AU TRUE)

juce_generate_juce_header (groovebox)
target_compile_features (groovebox PRIVATE cxx_std_20)
target_sources (groovebox PRIVATE
    ${SOURCES_MOTEUR}
    source/app/Main.cpp
    source/app/FenetrePrincipale.cpp)
target_include_directories (groovebox PRIVATE source)
target_compile_definitions (groovebox PRIVATE ${DEFINITIONS_COMMUNES})
target_link_libraries (groovebox PRIVATE ${MODULES_COMMUNS})

# Essais automatiques
juce_add_console_app (groovebox_essais PRODUCT_NAME "groovebox_essais")
juce_generate_juce_header (groovebox_essais)
target_compile_features (groovebox_essais PRIVATE cxx_std_20)
target_sources (groovebox_essais PRIVATE
    ${SOURCES_MOTEUR}
    essais/Essais.cpp)
target_include_directories (groovebox_essais PRIVATE source)
target_compile_definitions (groovebox_essais PRIVATE ${DEFINITIONS_COMMUNES})
target_link_libraries (groovebox_essais PRIVATE ${MODULES_COMMUNS})
