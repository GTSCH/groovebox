# groovebox (nom provisoire)

Groovebox logicielle pour Mac, pilotée au Push 2. Le cahier des charges est dans le dossier OneDrive « #Outils maison IA/MAO maison/CAHIER_DES_CHARGES.md » ; ce dossier-ci contient le code, hors OneDrive pour pouvoir utiliser git.

## Où en est le projet

| Étape | État |
|---|---|
| 1. Fondations | Faite et validée à l'oreille le 03.11.2026 |
| 2. Horloge, mesures, clic | Faite et validée à l'oreille le 03.11.2026 |
| 3. Push 2 | Faite le 03.11.2026, validée avec le simulateur et les essais ; essai sur le vrai Push à faire |
| 4. Session | Faite et validée à l'oreille le 03.11.2026 |
| 5a. Batterie, pas à pas | Faite et validée à l'oreille le 03.11.2026 |
| 5b. Générateurs rythmiques | Faite et validée à l'oreille le 03.11.2026 |
| 6a. Surge XT, jeu en gamme | Faite et validée à l'oreille le 03.11.2026 |
| 6b. Générateur de basses et mélodies | Faite et validée à l'oreille le 03.11.2026 |
| 7 à 11 | Pas commencées |

## Organisation

- `source/modele/Melodies` : générateur de basses et de mélodies (7 styles : basse dub, drum & bass, acid, hard techno, stabs minimal, arpège electro, nappe), toujours dans la gamme du morceau.
- `source/modele/Generateurs` : moteurs euclidien, carte de grooves (`CarteDeGrooves.h`, données de Grids d'Emilie Gillet, GPL v3), carte maison (9 rythmes de référence bâtis sur le groupement), humanisation, roulement, variation.
- `source/modele/` : le morceau (tempo, mesure, clic, 8 pistes, scènes, clips de notes), sauvegardé en JSON lisible ; `Bibliotheque` (un dossier par morceau, copies de secours, repérage des plantages) ; `Demo` (morceau de démonstration) ; Tap Tempo.
- `source/moteur/` : le moteur audio (Tracktion Engine), la batterie (`BatteriePlugin`, 16 pads lus depuis des WAV, `BanqueDeSons` pour le dossier Sons, `FabriqueDeSons` pour les kits de départ calculés), le clic, l'instrument provisoire (batterie et petit synthé calculés, en attendant les étapes 5 et 6), le bip de test, les réglages. Code commun à l'application et aux essais.
- `source/push/` : le Push 2. `Controles` (numéros des boutons, lecture des messages, commandes sysex), `Palette` (8 couleurs de piste), `Surface` (état partagé entre le Push et le simulateur), `PushMidi` (port User), `EcranPush` (écran par USB, fil dédié à 60 images par seconde), `Pilote` (ce que fait chaque contrôle, LED, dessin de l'écran).
- `source/app/` : l'application Mac (onglets « Jeu » et « Carte son », simulateur du Push dans une fenêtre à part, point d'entrée).
- `source/Texte.h` : écrire `"Décompte"_fr` pour tout texte accentué dans le code, sinon JUCE lit les accents de travers.
- `essais/` : essais automatiques, avec une carte son simulée (rien ne sort des haut-parleurs).
- `externe/tracktion_engine/` : Tracktion Engine v3.2.0, avec JUCE 8.0.6 (sous-module git, figé sur cette version).
- `externe/libusb/` : libusb 1.0.29, compilée avec le projet (réglages macOS fournis par libusb dans `Xcode/config.h`).
- `scripts/construire.sh` : compile, lance les essais, installe dans /Applications.

Outils de compilation : CMake et Ninja dans `~/Code/.outils-groovebox` (pas besoin de Xcode, seulement des outils en ligne de commande d'Apple).

## Construire

```bash
~/Code/groovebox/scripts/construire.sh
```

La première compilation prend quelques minutes ; les suivantes sont rapides.

## Fichiers sur le Mac

- Réglages (carte son, fréquence, tampon) : `~/Library/Application Support/groovebox/Settings.xml`
- Sons : `~/Music/groovebox/Sons/<catégorie>/*.wav`. Les 4 kits de départ (Analogique, Minimal, Dub, Hard techno) y sont fabriqués au premier lancement s'ils manquent ; on peut y ajouter ses propres fichiers.
- Morceaux : `~/Music/groovebox/Morceaux/<nom>/morceau.json` (sauvegardé dans les 2 secondes qui suivent chaque changement), copies de secours dans `sauvegardes/` (une toutes les 5 minutes, les 20 dernières). Le dernier ouvert revient au lancement. Hors du OneDrive.
- Journal : `~/Library/Logs/groovebox/journal.txt`

## Diagnostic sans fenêtre

```bash
/Applications/groovebox.app/Contents/MacOS/groovebox --diagnostic 60
```

Fait tourner le moteur 60 secondes avec les bips actifs mais muets, puis écrit dans le journal les coupures du son et la charge maximale.

## Points techniques à retenir

- Quand le moteur refait la liste des sorties (au démarrage, ou quand on change de carte son), il arrête la lecture : `Moteur` la relance aussitôt.
- Les sorties 1 à 4 sont toujours ouvertes quand la carte les a. Sans sorties 3-4, la piste du casque est coupée : elle ne finit jamais dans la sono.
- Tempo toujours à la noire ; en x/8, le moteur compte en croches. La ligne du temps commence par 2 mesures de préparation (le décompte) ; la mesure 1 vient après.
- Changer le tempo en pleine lecture ajoute un point de tempo juste après la position actuelle : la mesure ne saute pas. À l'arrêt, on revient à un seul tempo.
- Le clic est un clip MIDI d'une mesure, en boucle, joué par `ClicPlugin`. Le casque (3-4) reçoit le clic et une copie de la musique (envoi et retour du bus 0). Sans sorties 3-4, il se tait, sauf « écoute sur le Mac », qui envoie le clic seul sur 1-2.
- Si un bloc audio prend plus de 98 % du temps disponible, le moteur rend un bloc de silence et prend un bloc de retard. Les essais désactivent cette sécurité (le calcul y est hors temps réel) ; en concert, c'est la marge de calcul qui compte (étape 11).
- Session : chaque piste est un dossier du moteur (volume, panoramique, mute, solo, envoi au casque) qui contient les voix A et B ; les clips sont dans les cases de la voix A, la voix B attend les enchaînements (étape 8). Les lancements utilisent les « launch handles » du moteur, calés sur la limite voulue (temps, mesure, 2 ou 4 mesures, comptées depuis la mesure 1).
- Démarrage : la lecture part tout de suite (au début du décompte, ou 80 ms avant la mesure 1 sans décompte) et les clips sont calés dès que le fil audio confirme la position. À l'arrêt, le moteur ne met pas sa position à jour : ne jamais compter dessus.
- Le son est calculé sur le seul fil audio (`getNumberOfCPUsToUseForAudio` à 1) : réparti sur les 8 cœurs de la puce M3, il provoquait des coupures malgré 4 % de charge. À revoir avec Surge XT.
- Annuler : historique du morceau (1000 étapes), un même geste continu (un encodeur) ne compte qu'une fois. Les lancements de clips ne s'annulent pas (c'est du jeu, pas de l'édition).
- Push 2 : seuls les messages documentés par Ableton (manuel « Push 2 MIDI and Display Interface »). À la connexion : mode User, palette (entrées 64 à 95 seulement), bande tactile ; à la fermeture : LED éteintes et retour au mode Live. Ableton Live doit être fermé (il garde l'écran).
- Le simulateur ne reçoit pas les clics quand sa fenêtre est en arrière-plan sous le contrôle d'écran de Claude : pour l'essayer, prendre la main à l'écran (clic réel).
- Batterie : un kit par piste batterie (16 pads, notes 36 à 51), enregistré dans le morceau ; les sons restent en mémoire jusqu'à la fermeture, ce qui permet au fil audio de les lire sans risque. Sur chaque voix, l'instrument provisoire et la batterie sont présents, un seul est actif selon le son de la piste.
- Push, mode Note (pistes batterie) : 4 sur 4 en bas à gauche pour les pads, les 4 rangées du haut pour 32 pas du pas à pas (Page gauche et droite au-delà). Device : réglages du pad choisi (encodeurs 1 à 6) et du kit (7 et 8). Browse : catégorie (encodeur 1) puis son ou kit (encodeur 2), avec préécoute.
- Générateurs : un clip peut porter sa recette (`Clip::generateur`) ; ses notes sont recalculées à chaque réglage et à chaque changement de mesure ; Convert la retire (clip figé). Résolution : la double-croche, quelle que soit la mesure. Le swing ne touche pas les notes du morceau : le moteur retarde les double-croches du contretemps au moment d'écrire le clip.
- Push, pages Device d'une piste de synthé : Device fait tourner Macros, Mélodie, Commun (swing, graine, longueur). Le style de départ d'une mélodie suit le son de Surge de la piste.
- Push, pages Device d'une piste batterie : Device fait tourner Pad, Générateur, Commun. Clip : réglages de Shift + Duplicate. Encodeur Swing : swing global ; avec Shift, celui de la piste.
- Surge XT (GPL v3, installé à part dans /Library/Audio/Plug-Ins/VST3) : une instance sur la voix A de chaque piste de synthé, pilotée par ses paramètres nommés (`PiloteDeSurge`). Les 24 sons de départ (`SonsDeSynthe.cpp`) sont écrits en unités affichées par Surge (« LP 24 dB », Hz, ms...) ; groovebox retrouve la valeur interne en parcourant l'échelle de chaque réglage. Sans Surge, le son provisoire reprend la main. La voix B recevra son Surge avec les enchaînements.
- Outils : `groovebox --parametres-surge` liste les 776 réglages de Surge ; `--options-surge "A Filter 1 Type,..."` donne leurs choix.
- Gamme du morceau (bouton Scale) ; mode Note des synthés : un degré par colonne, une quarte par rangée, comme le Push d'Ableton.
- Niveaux : la batterie sort à -6 dB (facteur 0,5), Surge a un volume général de -4 dB ; la démo culmine sous 0 dB. Limiteur de protection : étape 7.
- Licences : Surge XT GPL v3 ; Grids (carte de grooves) GPL v3 ; libusb LGPL 2.1 ; Tracktion Engine GPL v3, JUCE AGPL v3. Usage personnel, aucune contrainte.
