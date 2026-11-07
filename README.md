# MINI ISAAC (TI-83 Premium CE)

Roguelike d'action inspire de *The Binding of Isaac*, ecrit en C pour l'eZ80
de la TI-83 Premium CE avec la toolchain CEdev. Fan-game non officiel : tout le
code et les graphismes (pixel art maison, 16x16) sont originaux.

## Contenu

- Etages aleatoires, salles, secrettes, boutique (pieces), tresors (cles)
- Boss (Monstro, Maman), ennemis varies et champions (recolors coriaces)
- 3+ personnages jouables deblocables : Isaac, Magda, Caine, Lazare (revit)
- Objets passifs (24) : degats, portee, larmes perceantes, homing, venin...
- Item actif TELE rechargeable, pacte du diable aux premiers etages
- Maledictions d'etage (obscurite, harte), pieces d'argent (5), coeurs d'ame
- Sauvegarde 3 slots (appvar), reprise de partie en cours
- Selftest integre : [Yequ] sur l'ecran titre

## Touches

- Croix directionnelle : se deplacer
- 2 / 4 / 6 / 8 : tirer dans la direction
- [MODE] : carte de l'etage, puis page stats (PV, DMG, VIT, CAD, objets)
- [ENTREE] : reprendre la partie depuis la carte/stats

## Compiler

Toolchain CEdev requise (convimg + make). Les graphismes prennent source
dans `tools/sprites_art.py` :

    cd tools && python -X utf8 make_gfx.py
    cd ../src/gfx && convimg.exe
    make          # -> bin/ISAAC.8xp

Transferer `bin/ISAAC.8xp` sur la calculatrice puis lancer ISAAC. Les
sauvegardes (appvars `ISAACSV1..3`) sont archivees en flash a chaque
sauvegarde : elles survivent aux resets de RAM et aux changements de
version du programme (v1.0 -> v1.1 -> v1.2 ...). Ne supprimez pas ces
appvars a la main, c'est la seule chose qui casse une sauvegarde.
