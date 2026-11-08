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
| 7. Effets | Faite et validée à l'oreille le 03.11.2026 |
| 8. Enchaînements | 8a (enchaînements automatiques) validée à l'oreille le 03.11.2026 ; 8b (à la main, montée) validée à l'oreille le 03.11.2026 |
| 9. Capture, retouche, rejeu | 9a (capture) validée à l'oreille le 03.11.2026 ; 9b (retouche, déroulé) validée le 03.11.2026 ; 9c (rejeu, reprise en main, section ouverte) validée le 08.11.2026 |
| 10 et 11 | Pas commencées |

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
- Prises : dans le dossier du morceau, `prises/<date et heure>/` : `prise.json` (le journal des gestes), `sortie.wav` (la sortie principale) et `micros.wav` (entrées 1-2, seulement si la carte de sortie a ses propres entrées). WAV 24 bits, environ 17 Mo par minute et par fichier. Elles suivent le morceau quand on le renomme. L'onglet « Prises » les montre, les réécoute (sur 1-2), les renomme et les met à la corbeille.
- Déroulés : dans le dossier du morceau, `deroules/<nom>.json`. Chaque section garde l'état du morceau à son début, ses gestes et l'endroit de la prise d'où vient son son.
- Journal : `~/Library/Logs/groovebox/journal.txt`

## Diagnostic sans fenêtre

```bash
/Applications/groovebox.app/Contents/MacOS/groovebox --diagnostic 60
```

Avec `--demo` en plus, le diagnostic joue la démo au lieu du dernier morceau ouvert (qui n'est ni changé ni sauvegardé). Avec `--prise`, il enregistre aussi une prise pendant tout le diagnostic, dans un dossier temporaire effacé à la fermeture.

Fait tourner le moteur 60 secondes avec les bips actifs mais muets, en enchaînant toutes les 4 secondes par un fondu d'une mesure, avec montée, entre les deux scènes les plus remplies, puis écrit dans le journal les coupures du son et la charge maximale.

Essais : `construction/groovebox_essais_artefacts/Release/groovebox_essais` les lance tous ; avec un nom de série en argument (« Enchaînements », « Surge XT »...), seulement celle-là. « Rejeu long » (environ 30 s, hors de la série habituelle) vérifie le critère de l'étape 9 : une impro de 10 minutes, jouée par un script, dont 4 minutes prises au milieu se rejouent avec le même son.

## Points techniques à retenir

- Quand le moteur refait la liste des sorties (au démarrage, ou quand on change de carte son), il arrête la lecture : `Moteur` la relance aussitôt.
- Les sorties 1 à 4 sont toujours ouvertes quand la carte les a. Sans sorties 3-4, la piste du casque est coupée : elle ne finit jamais dans la sono.
- Tempo toujours à la noire ; en x/8, le moteur compte en croches. La ligne du temps commence par 2 mesures de préparation (le décompte) ; la mesure 1 vient après.
- Changer le tempo en pleine lecture ajoute un point de tempo juste après la position actuelle : la mesure ne saute pas. À l'arrêt, on revient à un seul tempo.
- Le clic est un clip MIDI d'une mesure, en boucle, joué par `ClicPlugin`. Le casque (3-4) reçoit le clic et une copie de la musique (envoi et retour du bus 0). Sans sorties 3-4, il se tait, sauf « écoute sur le Mac », qui envoie le clic seul sur 1-2.
- Si un bloc audio prend plus de 98 % du temps disponible, le moteur rend un bloc de silence et prend un bloc de retard. Les essais désactivent cette sécurité (le calcul y est hors temps réel) ; en concert, c'est la marge de calcul qui compte (étape 11).
- Session : chaque piste est un dossier du moteur (volume, panoramique, mute, solo, envoi au casque) qui contient les voix A et B ; chaque clip existe dans les cases des deux voix ; la « voix courante » porte la musique, l'autre sert aux enchaînements. Les lancements utilisent les « launch handles » du moteur, calés sur la limite voulue (temps, mesure, 2 ou 4 mesures, comptées depuis la mesure 1).
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
- Surge XT (GPL v3, installé à part dans /Library/Audio/Plug-Ins/VST3) : une instance par voix (A et B) de chaque piste de synthé, avec les mêmes réglages, pilotée par ses paramètres nommés (`PiloteDeSurge`). Les 24 sons de départ (`SonsDeSynthe.cpp`) sont écrits en unités affichées par Surge (« LP 24 dB », Hz, ms...) ; groovebox retrouve la valeur interne en parcourant l'échelle de chaque réglage. Sans Surge, le son provisoire reprend la main.
- Outils : `groovebox --parametres-surge` liste les 776 réglages de Surge ; `--options-surge "A Filter 1 Type,..."` donne leurs choix.
- Gamme du morceau (bouton Scale) ; mode Note des synthés : un degré par colonne, une quarte par rangée, comme le Push d'Ableton.
- Niveaux : la batterie sort à -6 dB (facteur 0,5), Surge a un volume général de -4 dB ; la démo culmine sous 0 dB. Limiteur de protection : étape 7.
- Effets : 3 emplacements par piste, avant son volume. Maison (`EffetsMaison`) : filtre à un bouton, saturation, distorsion lourde (gain jusqu'à +40 dB, de l'écrêtage doux au repliement), punch (attaque, maintien, puissance), grave (renforcé et saturé), delay dub (retour jusqu'à 120 %, borné par la saturation de la boucle), limiteur ; Airwindows du moteur (option `TRACKTION_AIR_WINDOWS`) : DeRez2 (lo-fi), ButterComp2 (compresseur), TapeDelay (écho court), ChorusEnsemble (chorus). Effets de départ : deux pistes de retour (bus 1 delay, bus 2 réverbération du moteur), reprises dans le casque (bus 0). Sortie : filtre global (bande tactile, retour au centre) puis compresseur léger et limiteur à -1 dB ; le casque a son propre limiteur.
- Écran du Push : les 8 colonnes de réglages restent toujours affichées ; la colonne touchée s'éclaire ; les annonces des boutons passent 1,5 s dans le bandeau du haut, sans cacher le clic ni la prise. Touches : toutes éclairées (à peine pour celles qui ne servent pas encore, blanc faible pour les disponibles, blanc fort pour l'actif).
- Push : les 8 boutons du haut de l'écran = envoi momentané au delay ; Add Device ajoute un effet (rappuyer pour confirmer) ; Device passe aussi par les pages d'effets (encodeur 8 : sauter ; Delete + Device : retirer) ; Mix avec Page gauche et droite : volumes, envois delay, envois réverbération, effets de départ et compression de sortie.
- Enchaînements (`modele/Enchainements`, `moteur/VoixPlugin`) : le nouveau clip part sur la voix libre au calage, l'ancien continue sur la sienne ; chaque voix finit par un `VoixPlugin` (volume, passe-bas, passe-haut, graves coupés sous 180 Hz) qui suit son trajet d'après la position de lecture, à 32 échantillons près, sans attendre le fil principal. Les formes (fondu à puissance constante, échange des basses à mi-parcours, filtre, queue d'écho) sont dans `formeDeLaVoix`. Le fil principal (toutes les 10 ms) ne fait que les envois de la queue d'écho (deux envois par voix, avant son réglage, montés dès le temps qui précède la bascule) et arrête l'ancien clip une fois muet. Annuler : relancer le clip qu'on quitte ; les deux voix refont le chemin à l'envers depuis là où elles en sont (la queue d'écho, elle, repart dans l'autre sens). Une coupe pendant un enchaînement le termine au calage. Le Surge de la voix libre se repose (désactivé, sans reconstruire le graphe du moteur) et se réveille au lancement de l'enchaînement.
- Push, page Enchaînement (bouton User) : type, durée, batterie nette, intensité ; l'écran montre l'enchaînement en cours et les deux scènes, et la ligne sous l'en-tête avance avec lui sur toutes les pages. Page ouverte, un bouton de scène lance avec ces réglages ; User tenu + scène retient l'enchaînement pour entrer dans la scène (rappuyer le retire). Ailleurs, une scène entre avec son enchaînement retenu, sinon celui de la page ; un clip lancé seul prend celui de la page.
- Enchaînement à la main : Shift + scène (`Moteur::preparerScene`) lance la scène en silence sur les voix libres, au calage ; la bande tactile passe en curseur (barre depuis le bas, sans retour au centre : `EtatSurface::bandeSansRetour`, qui envoie la configuration au Push) et donne l'avancée de toutes les pistes. Batterie nette : elle change d'un coup au milieu de la bande. En haut, la nouvelle scène prend la main ; redescendu en bas (après être monté), rien n'a changé ; relancer la scène préparée finit le mélange seul, à la vitesse de la durée choisie ; lancer une autre scène l'annule d'abord. Ensuite, la bande attend d'être lâchée avant de redevenir le filtre global. Une coupe choisie devient un fondu à la main.
- Montée (encodeur 5 de la page Enchaînement) : sur la dernière mesure avant la bascule (ou ce qui en reste), le filtre de sortie monte en passe-haut, de plus en plus vite, à l'échantillon près (`FiltrePlugin::programmerMontee`), et les envois à la réverbération de toutes les pistes grimpent (fil principal). À la bascule, tout retombe d'un coup et la queue de réverbération sonne sur la nouvelle scène. Marche aussi avec la coupe ; pas de montée quand on annule.
- Prises (`modele/Prise`, `moteur/EnregistreurAudio`) : Record (Push ou onglet Prises) note chaque geste venu de l'extérieur du moteur (un appel interne n'est pas noté deux fois) avec son temps et, pour les lancements, le temps du calage où il prend effet ; les réglages sont notés comme écarts entre deux JSON du morceau (`ecarts::entre`, réappliqués par `ecarts::appliquer`). Le morceau et les scènes qui jouent au départ sont gardés. Son : un maillon en fin de sortie principale (`EnregistreurDeSortiePlugin`) et un rappel à côté du moteur pour l'entrée (`EnregistreurDEntree`) déposent les échantillons ; `ThreadedWriter` les écrit sur un fil à part (8 s de réserve). À l'arrêt, une prise lance la lecture et commence à la mesure 1 ; en lecture, à la mesure en cours ; elle finit au second appui, à l'arrêt de la lecture ou en changeant de morceau.
- Entrées : ouvertes seulement si la carte de sortie a les siennes (`reglerLesEntrees`) ; marier les haut-parleurs et le micro du Mac en une seule carte risquerait des coupures. Dans les essais, la carte simulée donne la même mémoire à l'entrée et à la sortie : le contenu des micros s'y vérifie à part.
- Retouche (`modele/Deroule`, onglet Retouche) : `retouche::decouper` fait une section par lancement de scène (ou par Shift + scène), à son calage ; à calage égal, le dernier geste l'emporte. Chaque section garde l'état du morceau à son début (JSON complet) : on peut supprimer, déplacer ou répéter une section, elle repart toujours de son propre état. Couper donne une « suite » (pas d'enchaînement d'entrée : la scène continue). Lisser : moyenne glissante sur un temps, dernière valeur gardée ; effacer : le réglage garde sa valeur de départ. Les changements notés (JSON) sont partagés entre copies : une section est détachée avant d'être modifiée. Écouter une section joue le passage de `sortie.wav` (et des micros) d'où elle vient, en suivant les changements de tempo de la prise.
- Rejeu (`Moteur::rejouer`) : à l'arrêt, l'état de la première section est remis (mesure comprise) et ce qui doit jouer part à la mesure 1. Ensuite, chaque section est lancée avec la même avance que pendant la prise, à sa mesure exacte (calage imposé, `caleForcee`), ses clips mis en place juste avant ; à son début, son état est remis ; ses gestes repartent à leur temps (fil principal, toutes les 10 ms ; les lancements gardent leur calage exact). Une piste qu'on touche est reprise (le déroulé n'y touche plus) ; lancer une scène reprend tout ; rendre la main remet ses réglages et son clip là où en est le déroulé. Section ouverte : elle reprend son état et ses gestes à chaque tour, jusqu'à passer à la suite (à la prochaine mesure). À la fin ou à l'arrêt, le morceau d'avant le rejeu revient, l'historique aussi ; rien n'est enregistré pendant le rejeu.
- Push pendant un rejeu : Play arrête ; Shift + Play rend la main à toutes les pistes ; Shift + bouton de piste la rend à une piste ; Shift + Page droite quitte une section ouverte. En haut de l'écran : la section, les mesures restantes, la suivante, les pistes reprises. Onglet Retouche : « Section ouverte », « Rejouer depuis la section », et « Play rejoue ce déroulé » (Play au Push ou barre d'espace).
- Démo 2 (`morceauDeDemonstration2`, bouton « Démo 2 » de l'onglet Session) : 6/8 à 120 à la noire, ré mineur, kit Hard techno avec punch et grave, basse industrielle distordue, nappe sombre, cuivres, stabs durs, tambours. Six scènes à jouer à la main (Intro, Approche, Batterie, Construction, Sommet, Fin) et un déroulé « Démo 2 en entier » (36 mesures, 54 s) écrit à la main dans `derouleDeLaDemo2`, avec ses enchaînements, deux montées et des réglages qui s'ouvrent ; à l'ouverture, Play le rejoue. Essai hors série « Démo 2 » : rendu complet avec Surge, niveaux de chaque partie.
- Licences : Airwindows MIT ; Surge XT GPL v3 ; Grids (carte de grooves) GPL v3 ; libusb LGPL 2.1 ; Tracktion Engine GPL v3, JUCE AGPL v3. Usage personnel, aucune contrainte.
