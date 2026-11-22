# Groovebox (nom provisoire)

Groovebox logicielle pour Mac, pilotée par un contrôleur Ableton Push 2. Elle joue une batterie pas à pas, génère des rythmes, des basses et des mélodies dans la gamme du morceau, enchaîne les sections, capture et rejoue ce qui a été joué, avec un sampler, des effets et des exports.

Je la développe sur mon temps libre avec Claude Code, pour mon usage personnel. C'est un projet en cours, fourni tel quel, sans support et sans garantie.

## Construire

La groovebox se compile avec CMake sur un Mac. Elle s'appuie sur Tracktion Engine et libusb, inclus comme sous-modules dans `externe/` : clonez le dépôt avec ses sous-modules (`git clone --recursive`). Les étapes de compilation et l'organisation du code sont décrites dans `LISEZMOI.md`.

## Licence

La groovebox est publiée sous licence GNU AGPL 3.0 (voir `LICENSE`). Ce choix découle des bibliothèques qu'elle utilise : Tracktion Engine est publié sous GPL 3 et le framework JUCE, qu'il contient, sous AGPL 3. Ces deux bibliothèques existent aussi sous licence commerciale, que ce dépôt n'utilise pas.

<sub>Guillaume Tschupp · Co-créé avec Claude Opus 5.5</sub>
