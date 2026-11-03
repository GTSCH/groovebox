#pragma once

#include "Mesure.h"
#include "Kit.h"
#include "Generateurs.h"
#include "Gamme.h"
#include "Melodies.h"

namespace groovebox
{

struct ReglagesClic
{
    bool actif = true;
    float volume = 0.8f;              ///< 0 à 1
    bool subdivisions = false;
    bool unClicParGroupe = false;
    int decompte = 1;                 ///< 0, 1 ou 2 mesures avant le départ
    bool ecouteSurMac = false;        ///< Essai sans carte son : clic sur les sorties 1-2. Ignoré dès que la carte a des sorties 3-4.

    bool operator== (const ReglagesClic&) const = default;
};

constexpr int nombreDePistes = 8;

/** Une note d'un clip, en temps du moteur (l'unité du bas de la mesure) depuis le début du clip. */
struct Note
{
    double debut = 0.0;
    double duree = 0.25;
    int hauteur = 60;
    int velocite = 100;

    bool operator== (const Note&) const = default;
};

/** Un clip : une boucle de notes de 1, 2, 4, 8 ou 16 mesures. */
struct Clip
{
    static constexpr std::array<int, 5> longueursPossibles { 1, 2, 4, 8, 16 };

    juce::String nom;
    int longueurMesures = 1;
    std::vector<Note> notes;
    std::optional<Generateur> generateur;   ///< Batterie : si présent, les notes sont calculées par le générateur rythmique
    std::optional<GenerateurMelodique> melodie;     ///< Synthé : si présent, les notes sont calculées par le générateur mélodique

    /** Double la longueur en recopiant les notes (Double Loop). Rien si on est déjà à 16 mesures. */
    bool doubler (int tempsParMesure);

    /** Recalcule les notes d'un clip généré (après un changement de réglage, de mesure ou de gamme). */
    void regenerer (const Mesure&, const Gamme&);
    bool estGenere() const              { return generateur.has_value() || melodie.has_value(); }

    /** Figer : le clip garde ses notes et n'est plus généré (Convert). */
    void figer()                        { generateur.reset(); melodie.reset(); }

    bool operator== (const Clip&) const = default;
};

/** Son provisoire d'une piste, en attendant les vraies machines (étapes 5 et 6). */
enum class SonProvisoire { batterie, basse, nappe, lead, arpege, percussions, stab, texture };
juce::String nomDuSon (SonProvisoire);

struct Piste
{
    juce::String nom;
    SonProvisoire son = SonProvisoire::nappe;
    float volumeDb = 0.0f;
    float panoramique = 0.0f;       ///< -1 à gauche, 1 à droite
    bool mute = false, solo = false;
    std::optional<float> swing;     ///< Propre à la piste ; sinon, le swing du morceau
    Kit kit;
    juce::String sonDeSynthe;       ///< Son de Surge XT (pistes de synthé), par son nom
    std::array<float, 8> macros { 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f };                        ///< Utilisé quand le son est Batterie ou Percussions

    bool estUneBatterie() const     { return son == SonProvisoire::batterie || son == SonProvisoire::percussions; }
    bool operator== (const Piste&) const = default;
};

/** Une scène : rangée de clips lancés ensemble. Mesure et tempo propres prévus pour plus tard, inactifs. */
struct Scene
{
    juce::String nom;
    std::optional<Mesure> mesure;
    std::optional<double> tempo;
    std::array<std::optional<Clip>, nombreDePistes> clips;

    bool estVide() const;
    bool operator== (const Scene&) const = default;
};

/** Calage des lancements de clips et de scènes. */
enum class Calage { temps, mesure, deuxMesures, quatreMesures };
juce::String nomDuCalage (Calage);

/** Le morceau : tout ce qui se sauvegarde, en JSON lisible. */
struct Morceau
{
    static constexpr double tempoMin = 60.0, tempoMax = 200.0;
    static constexpr int versionDuFormat = 2;

    juce::String nom { "Sans titre" };
    double tempo = 120.0;             ///< BPM, toujours à la noire
    Mesure mesure;
    ReglagesClic clic;
    float musiqueDansCasqueDb = 0.0f; ///< Niveau de la musique dans le casque du batteur
    float volumeGeneralDb = 0.0f;     ///< Volume de la sortie principale (sono), sans effet sur le casque
    Calage calage = Calage::mesure;
    Gamme gamme;                      ///< Do mineur par défaut, comme la démo
    float swing = 0.0f;               ///< 0 à 0,75 : retard des double-croches du contretemps (encodeur Swing)
    Variation variation;              ///< Réglages de « dupliquer et varier »
    juce::uint32 prochaineGraine = 2; ///< Graine donnée à la prochaine variation
    std::array<Piste, nombreDePistes> pistes = pistesDeDepart();
    std::vector<Scene> scenes;

    static std::array<Piste, nombreDePistes> pistesDeDepart();

    /** Le clip d'une case, s'il y en a un. */
    const Clip* clip (int piste, int scene) const;

    /** Ajoute des scènes vides jusqu'à en avoir au moins n. */
    void assurerScenes (int n);

    /** Swing qui s'applique à une piste. */
    float swingDeLaPiste (int piste) const;

    /** Recalcule tous les clips générés (après un changement de mesure ou de gamme). */
    void regenererTout();

    juce::var versJson() const;
    static Morceau depuisJson (const juce::var&);

    bool enregistrer (const juce::File&) const;
    static std::optional<Morceau> charger (const juce::File&);

    bool operator== (const Morceau&) const = default;
};

} // namespace groovebox
