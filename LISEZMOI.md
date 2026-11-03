# groovebox (nom provisoire)

Groovebox logicielle pour Mac, pilotée au Push 2. Le cahier des charges est dans le dossier OneDrive « #Outils maison IA/MAO maison/CAHIER_DES_CHARGES.md » ; ce dossier-ci contient le code, hors OneDrive pour pouvoir utiliser git.

## Où en est le projet

| Étape | État |
|---|---|
| 1. Fondations | Faite et validée à l'oreille le 03.11.2026 |
| 2 à 11 | Pas commencées |

## Organisation

- `source/moteur/` : le moteur audio (Tracktion Engine), le bip de test, les réglages. Code commun à l'application et aux essais.
- `source/app/` : l'application Mac (fenêtre, point d'entrée).
- `essais/` : essais automatiques, avec une carte son simulée (rien ne sort des haut-parleurs).
- `externe/tracktion_engine/` : Tracktion Engine v3.2.0, avec JUCE 8.0.6 (sous-module git, figé sur cette version).
- `scripts/construire.sh` : compile, lance les essais, installe dans /Applications.

Outils de compilation : CMake et Ninja dans `~/Code/.outils-groovebox` (pas besoin de Xcode, seulement des outils en ligne de commande d'Apple).

## Construire

```bash
~/Code/groovebox/scripts/construire.sh
```

La première compilation prend quelques minutes ; les suivantes sont rapides.

## Fichiers sur le Mac

- Réglages (carte son, fréquence, tampon) : `~/Library/Application Support/groovebox/Settings.xml`
- Journal : `~/Library/Logs/groovebox/journal.txt`

## Diagnostic sans fenêtre

```bash
/Applications/groovebox.app/Contents/MacOS/groovebox --diagnostic 60
```

Fait tourner le moteur 60 secondes avec les bips actifs mais muets, puis écrit dans le journal les coupures du son et la charge maximale.

## Points techniques à retenir

- Quand le moteur refait la liste des sorties (au démarrage, ou quand on change de carte son), il arrête la lecture : `Moteur` la relance aussitôt.
- Les sorties 1 à 4 sont toujours ouvertes quand la carte les a. Sans sorties 3-4, la piste du casque est coupée : elle ne finit jamais dans la sono.
- Licences : Tracktion Engine GPL v3, JUCE AGPL v3. Usage personnel, aucune contrainte.
