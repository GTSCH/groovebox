#!/usr/bin/env python3
"""Installe les kits acoustiques de la batterie guide dans Musique/groovebox/Sons/Batterie acoustique.

Les sons viennent des AVL Drumkits de Glen MacArthur (https://www.bandshed.net/avldrumkits/), en SFZ,
licence Creative Commons BY-SA 3.0. Le script télécharge les 5 archives (environ 86 Mo) s'il ne les trouve pas,
lit chaque fichier SFZ et copie, pour chacun des 16 pads, les niveaux de frappe du plus doux au plus fort :
« Black Pearl/Caisse claire/1.wav » à « 5.wav ».

Usage : python3 -I scripts/installer-batterie-acoustique.py [dossier des archives]
"""

import pathlib
import re
import shutil
import sys
import tempfile
import urllib.request
import zipfile

ADRESSE = "https://www.bandshed.net/sounds/AVLDrumkits_SFZ/"

# Nom du kit dans groovebox : archive, et notes MIDI du SFZ jouées par les 16 pads (notes 36 à 51 de groovebox)
BATTERIE = [36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 56, 49, 53, 51]
KITS = {
    "Black Pearl": ("BLACK_PEARL_2023_repack.zip", BATTERIE),
    "Blonde Bop": ("BLONDE_BOP_SFZ.zip", BATTERIE),
    "Blonde Bop fagots": ("BLONDE_BOP_HR_SFZ.zip", BATTERIE),
    "Red Zeppelin": ("RED_ZEPPELIN_2023_repack.zip", BATTERIE),
    "Cajón": ("BUSKMANS_HOLIDAY_SFZ.zip", list(range(36, 52))),
}

# Mêmes noms que kitsAcoustiques::instruments dans source/modele/Kit.cpp
INSTRUMENTS_BATTERIE = [
    "Grosse caisse", "Coup de cercle", "Caisse claire", "Clap", "Bord de caisse claire", "Tom basse", "Charleston fermé",
    "Bord de tom basse", "Pied de charleston", "Tom", "Charleston ouvert", "Bord de tom", "Cloche", "Crash", "Cloche de ride", "Ride"]
INSTRUMENTS_CAJON = [
    "Cajón grave", "Claquement de doigts", "Cajón claqué", "Claps", "Cajón claqué droite", "Conga grave", "Shaker",
    "Conga grave droite", "Tambourin secoué", "Conga aiguë", "Tambourin frappé", "Conga aiguë droite", "Claves",
    "Cymbale", "Cloche de cymbale", "Cloche"]

A_PROPOS = """Batterie acoustique de groovebox

Ces sons viennent des AVL Drumkits de Glen MacArthur (AV Linux), https://www.bandshed.net/avldrumkits/
Licence Creative Commons Attribution - Partage dans les mêmes conditions 3.0 (CC BY-SA 3.0) :
https://creativecommons.org/licenses/by-sa/3.0/deed.fr

Ils peuvent servir librement dans ta musique. Si tu redistribues les sons eux-mêmes (pas tes morceaux),
cite leur auteur et garde la même licence.

Kits : Black Pearl, Blonde Bop, Blonde Bop fagots (Blonde Bop HR, joué aux baguettes fagots),
Red Zeppelin, Cajón (Buskman's Holiday).
Chaque instrument est un dossier : 1.wav est la frappe la plus douce, le dernier la plus forte.
Installés par scripts/installer-batterie-acoustique.py.
"""


def regions_par_note(sfz: pathlib.Path):
    """Note MIDI : échantillons du groupe, triés du plus doux au plus fort (lovel croissant)."""
    notes = {}
    note = None

    for ligne in sfz.read_text(errors="replace").splitlines():
        ligne = ligne.split("//")[0]

        if "<group>" in ligne:
            m = re.search(r"\bkey=(\d+)", ligne)
            note = int(m.group(1)) if m else None

        if "<region>" in ligne and note is not None:
            echantillon = re.search(r"sample=(\S+\.wav)", ligne)
            lovel = re.search(r"\blovel=(\d+)", ligne)

            if echantillon:
                notes.setdefault(note, []).append((int(lovel.group(1)) if lovel else 1, echantillon.group(1)))

    return {n: [e for _, e in sorted(liste)] for n, liste in notes.items()}


def main():
    archives = pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else pathlib.Path.home() / "Code/.outils-groovebox/avl-telechargement"
    archives.mkdir(parents=True, exist_ok=True)
    sons = pathlib.Path.home() / "Music/groovebox/Sons/Batterie acoustique"
    sons.mkdir(parents=True, exist_ok=True)

    for kit, (archive, notes) in KITS.items():
        chemin = archives / archive

        if not chemin.exists():
            print(f"Téléchargement de {archive}...")
            urllib.request.urlretrieve(ADRESSE + archive, chemin)

        with tempfile.TemporaryDirectory() as dossier:
            with zipfile.ZipFile(chemin) as z:
                z.extractall(dossier)

            sfz = next(pathlib.Path(dossier).rglob("*.sfz"))
            regions = regions_par_note(sfz)
            noms = INSTRUMENTS_CAJON if kit == "Cajón" else INSTRUMENTS_BATTERIE
            cible = sons / kit
            shutil.rmtree(cible, ignore_errors=True)

            for nom, note in zip(noms, notes):
                if note not in regions:
                    print(f"  {kit} : pas de note {note} pour {nom}")
                    continue

                (cible / nom).mkdir(parents=True)

                for i, echantillon in enumerate(regions[note], start=1):
                    shutil.copyfile(sfz.parent / echantillon, cible / nom / f"{i}.wav")

            print(f"{kit} : {len(list(cible.iterdir()))} instruments")

    (sons / "À propos de ces sons.txt").write_text(A_PROPOS)


if __name__ == "__main__":
    main()
