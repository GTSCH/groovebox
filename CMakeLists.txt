cmake_minimum_required (VERSION 3.22)

project (groovebox VERSION 0.1.0 LANGUAGES C CXX)

set (CMAKE_OSX_DEPLOYMENT_TARGET "13.0" CACHE STRING "" FORCE)
set (CMAKE_OSX_ARCHITECTURES "arm64" CACHE STRING "" FORCE)

add_subdirectory (externe/tracktion_engine/modules/juce)
add_subdirectory (externe/tracktion_engine/modules)

# libusb, pour l'écran du Push (compilée avec le projet, réglages macOS fournis par libusb)
set (LIBUSB externe/libusb/libusb)
add_library (libusb_groovebox STATIC
    ${LIBUSB}/core.c ${LIBUSB}/descriptor.c ${LIBUSB}/hotplug.c ${LIBUSB}/io.c
    ${LIBUSB}/strerror.c ${LIBUSB}/sync.c
    ${LIBUSB}/os/darwin_usb.c ${LIBUSB}/os/events_posix.c ${LIBUSB}/os/threads_posix.c)
target_include_directories (libusb_groovebox PUBLIC ${LIBUSB} PRIVATE externe/libusb/Xcode)
target_compile_options (libusb_groovebox PRIVATE -w)
target_link_libraries (libusb_groovebox PUBLIC "-framework IOKit" "-framework CoreFoundation" "-framework Security" objc)

# Code commun à l'application et aux essais
set (SOURCES_MOTEUR
    source/modele/Mesure.cpp
    source/modele/Morceau.cpp
    source/modele/Demo.cpp
    source/modele/Bibliotheque.cpp
    source/modele/Kit.cpp
    source/moteur/BipPlugin.cpp
    source/moteur/ClicPlugin.cpp
    source/moteur/InstrumentProvisoire.cpp
    source/moteur/BanqueDeSons.cpp
    source/moteur/BatteriePlugin.cpp
    source/moteur/FabriqueDeSons.cpp
    source/moteur/Moteur.cpp
    source/moteur/Reglages.cpp
    source/push/Controles.cpp
    source/push/Palette.cpp
    source/push/Surface.cpp
    source/push/EcranPush.cpp
    source/push/PushMidi.cpp
    source/push/Pilote.cpp)

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
    juce::juce_recommended_config_flags
    libusb_groovebox)

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
    source/app/FenetrePrincipale.cpp
    source/app/PageJeu.cpp
    source/app/PageCarteSon.cpp
    source/app/PageSession.cpp
    source/app/Simulateur.cpp)
target_include_directories (groovebox PRIVATE source)
target_compile_definitions (groovebox PRIVATE ${DEFINITIONS_COMMUNES})
target_link_libraries (groovebox PRIVATE ${MODULES_COMMUNS})

# Essais automatiques
juce_add_console_app (groovebox_essais PRODUCT_NAME "groovebox_essais")
juce_generate_juce_header (groovebox_essais)
target_compile_features (groovebox_essais PRIVATE cxx_std_20)
target_sources (groovebox_essais PRIVATE
    ${SOURCES_MOTEUR}
    source/app/Simulateur.cpp
    essais/Essais.cpp)
target_include_directories (groovebox_essais PRIVATE source)
target_compile_definitions (groovebox_essais PRIVATE ${DEFINITIONS_COMMUNES} JUCE_MODAL_LOOPS_PERMITTED=1)
target_link_libraries (groovebox_essais PRIVATE ${MODULES_COMMUNS})
