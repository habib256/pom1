# Libretro — carte de la frontière

(c) 2026 VERHILLE Arnaud. Relevé du 2026-11-25, avant toute ligne de code libretro.

Ce document confronte ce que l'API libretro demande à un cœur avec ce que POM1 offre
aujourd'hui. Il dit ce qui est **prêt**, ce qui reste **à adapter**, et les **règles** que
l'adaptateur devra suivre. Il traite d'abord la question propre à POM1 : **une machine à
deux écrans** dans une API qui n'en connaît qu'un.

Même démarche que le document docs/LIBRETRO.md de NeoST et que le cœur déjà livré de POM2
(dossier `libretro/` de POM2, dont on reprendra le `.gitlab-ci.yml` et les profils
manette → clavier).

## Pourquoi un cœur POM1

D'après la liste officielle des cœurs (`libretro/libretro-core-info`, relevée le
2026-11-25), **aucun cœur libretro n'est consacré à l'Apple-1**. Seul MAME pourrait le
couvrir, ce qui reste à vérifier. L'Apple II n'a qu'AppleWin, l'Atari ST en a trois. POM1
viendrait donc combler un manque, avec ses cartes P-LAB, la GEN2 HGR et la cartouche CodeTank.

## Principe

Un cœur libretro est un **frontend de plus**, au même titre que l'interface ImGui, le
headless (`--headless`, `--cmd-port`) et le WebAssembly. Il vivrait dans un dossier
src/libretro et ne ferait que traduire. Le cœur émulé ne dépend pas de libretro, comme
il ne dépend déjà ni d'ImGui ni de GLFW (`architecture_check`).

Quatre contraintes libretro sont plus dures que celles des frontends existants :

1. **Une seule image par trame** (`retro_video_refresh`). Pas de deuxième fenêtre, pas
   de docking. Tout ce que l'utilisateur voit doit tenir dans ce seul cadre.
2. **Un seul blob d'état** (`retro_serialize`), appelé à chaque trame par l'exécution
   anticipée (*run-ahead*), le jeu en réseau (*netplay*) et le retour arrière de
   RetroArch. Il doit être rapide, de taille bornée et déterministe.
3. **Un processus, plusieurs jeux** : `retro_load_game` / `retro_unload_game` se
   succèdent sans redémarrer le processus.
4. **Aucune sortie propre** : ni fenêtre, ni périphérique audio, ni socket attendu.
   L'image, le son et les journaux passent par des rappels, sur le fil de `retro_run`.

## Deux écrans dans une seule image

Sur la vraie machine, le terminal 40×24 de l'Apple-1 et la carte graphique (GEN2 HGR,
TMS9918 ou GT-6144) sortent chacun sur **leur propre moniteur**. Le bureau POM1 les
montre dans deux fenêtres ImGui. Libretro n'a qu'une sortie vidéo par cœur, et
RetroArch n'ouvre pas de seconde fenêtre. **Le cœur compose donc lui-même les deux
écrans dans une seule image.** C'est ce que font les cœurs Nintendo DS (melonDS,
DeSmuME) et 3DS (Citra) : les joueurs connaissent cette approche.

### Les dispositions

Chaque écran occupe une **case de 288×216**, la taille du TMS9918 avec sa bordure
(`TMS9918::kFullWidth` × `kFullHeight`). Les autres sources y sont centrées : terminal
280×192, GEN2 280×192, GT-6144 64×96 agrandi ×4 / ×2 en 256×192. Une case de taille fixe
évite que la géométrie change quand on passe d'une carte à l'autre : elle ne change
qu'avec la disposition.

```
Hybride (défaut)        Côte à côte          Haut / bas      Un seul écran
┌──────────┬─────┐      ┌──────┬──────┐       ┌──────┐        ┌──────┐
│          │TEXTE│      │TEXTE │ GFX  │       │ GFX  │        │ GFX  │ ← un bouton
│   GFX    ├─────┤      └──────┴──────┘       ├──────┤        └──────┘   bascule
│          │     │                            │TEXTE │
└──────────┴─────┘                            └──────┘
  440×216               584×216               288×440         288×216
```

(avec une bande noire de 8 pixels entre deux écrans)

- **Hybride** par défaut : la carte graphique en grand, le terminal à moitié taille sur
  le côté. Sur une télé 16:9 c'est la disposition qui perd le moins de place. Le
  côte à côte donne un format 8:3 très plat, le haut/bas un format vertical.
- **Sans carte graphique branchée**, le cœur passe de lui-même en **un seul écran**
  (le terminal). C'est le cas des deux machines de 1976 et de la plupart des presets.
- **Plusieurs cartes graphiques à la fois** (preset *Multiplexing Fantasy*) : une option
  choisit la carte montrée, et par défaut c'est la première branchée dans l'ordre
  GEN2, TMS9918, GT-6144.

### Le mécanisme libretro

- `retro_get_system_av_info` annonce la **plus grande** géométrie possible
  (`max_width` 584, `max_height` 440) et la disposition courante comme `base`.
- À chaque changement de disposition, de bascule ou de carte branchée,
  `RETRO_ENVIRONMENT_SET_GEOMETRY` annonce la nouvelle taille et le nouveau rapport
  d'aspect. Tant qu'on reste sous le maximum annoncé, cet appel ne coûte rien
  (pas de réinitialisation du pilote vidéo).
- Le **rapport d'aspect** se calcule à partir d'un 4:3 par écran (les pixels de
  l'Apple-1 ne sont pas carrés : 280×192 sur un moniteur 4:3), pas à partir des pixels.
- **Le bouton de bascule** est lu par le cœur sur la manette (R3 par défaut, en
  option). Libretro ne permet pas à un cœur de déclarer un raccourci, et sous *Game
  Focus* tout le clavier appartient à l'Apple-1.

### Ce que cela implique

- **La composition est une décision, pas un dessin.** Elle a sa place dans une fonction
  pure, testée sans libretro, comme `src/LayoutDecisions.h` : (disposition, cartes
  branchées, bascule) → rectangles des écrans + géométrie annoncée.
- **Les shaders de RetroArch** s'appliquent à l'image entière, sans savoir qu'elle
  contient deux écrans. Les lignes de balayage d'un shader CRT ne tomberont pas juste
  sur deux écrans d'échelles différentes, d'où la bande noire. Les effets CRT du bureau
  (`src/Pom1CrtEffects.h`, phosphore, lueur) passent par le GPU et ne sont pas
  disponibles. Le cœur livre des pixels nets, teintés vert, ambre ou blanc selon une option.
- **Cadence.** C'est l'horloge de l'Apple-1 qui rythme `retro_run` (une trame =
  `POM1_CPU_CYCLES_PER_FRAME_1X_60HZ`, 17 062 cycles). Le TMS9918 a sa propre trame de
  17 067 cycles (`TMS9918::kCyclesPerFrame`) : environ une fois par minute, une image TMS
  est montrée deux fois ou sautée. Sur la vraie machine, les deux moniteurs ne sont pas
  synchronisés non plus. On l'accepte, et on le note ici pour que personne ne prenne
  ce saut pour un bogue.

## La carte, point par point

| Besoin libretro | Ce que POM1 offre | Verdict |
|---|---|---|
| `retro_run` : une trame, puis rendre la main | `EmulationController` en `ExecutionMode::Deterministic` : le fil d'émulation reste garé, seul `runCyclesSync(cycles)` avance le CPU, et les touches en file partent en tête de tranche | **Prêt** (le fil garé existe quand même, voir « Fils ») |
| Image du **terminal** | La grille 40×24, le défilement, le curseur, la séquence de démarrage et la rastérisation 280×192 vivent dans `Screen_ImGui`, **côté UI**. Le headless n'a que `HeadlessDisplayCapture` (texte brut, `src/main_imgui.cpp`) | **À adapter** : « Terminal hors de l'UI » |
| Image **GEN2** | `GraphicsCard::render(mémoire, latch, journal)` rastérise 280×192 sur CPU, à partir du journal de `Gen2VideoScanner`. `--dump-gen2-frame` le prouve au pixel | **Prêt**. Le `Gen2FieldPacer` du bureau ne sert pas : une trame libretro = un champ |
| Image **TMS9918** | `TMS9918::renderToBufferWithBorder` (288×216) et le framebuffer progressif, sur CPU. `--dump-tms-frame` le prouve | **Prêt** |
| Image **GT-6144** | `GT6144::renderToBuffer` (64×96) | **Prêt** (agrandissement dans la composition) |
| Format `RETRO_PIXEL_FORMAT_XRGB8888` | Les trois cartes écrivent en ordre `IM_COL32` (R dans l'octet bas) | **À adapter** : permutation R↔B dans la composition |
| Son `audio_sample_batch` en `int16` stéréo | Le son est un service **injecté** (`pom1::IAudioService`, `src/AudioService.h`) ; `mixSources(float*, n)` rend du mono `float`, et `NullAudioService` montre qu'un service sans périphérique suffit | **À adapter** : un service libretro qui appelle `mixSources` une fois par trame, puis convertit `float` mono → `int16` stéréo |
| Son cadencé par l'émulation | Le SID et la cassette suivent les cycles émulés, pas l'horloge murale (README : *tempo follows emulation speed*) | **Prêt**, à confirmer par une mesure : nombre exact d'échantillons par trame, sans asservissement |
| `retro_serialize` rapide | `Memory::saveSnapshotToBuffer` / `loadSnapshotFromBuffer`, validés puis appliqués avec retour arrière (`SnapshotIO.h`). Restauration ≈ 452 µs, état réel ≈ 117 Ko | **Prêt** |
| Le blob contient tout ce qui change | Snapshot v7 : section `RUN` (rafraîchissement DRAM, interruptions en attente, touches en file), registres PIA, cartes, et la grille texte par `DisplayDevice::serialize` | **Prêt**, à condition que le terminal sorti de l'UI garde le même format de sérialisation |
| `retro_serialize_size` fixe | La taille dépend des cartes branchées (16 Ko de VRAM TMS) et du **nombre d'événements GEN2VID** du champ courant | **À adapter** : « Borne de l'état » |
| Déterminisme (run-ahead, netplay) | Mode Deterministic, films d'entrée (`InputMovie.h`) rejoués au cycle près. L'horloge A1-IO RTC peut être figée (`--rtc-freeze`) | **À adapter** : une restauration d'état ou un reset **rend la RTC à l'horloge de l'hôte** (`doc/CLI.md`). Le run-ahead restaure à chaque trame : voir « RTC figée à travers les états » |
| Journaux `retro_log_printf` | `Logger` est une interface (`src/Logger.h`) avec plusieurs implémentations (flux, anneau, fichier, tee) | **Prêt** : un `Logger` de plus |
| BIOS dans le dossier système | `ResourceLocator::rootedAt(dir)` dit « cherche ici seulement » | **Prêt** (`<system>/pom1/roms/…`). Voir « ROM Apple » pour la question de licence |
| Clavier (`retro_keyboard_event`) | `EmulationController::queueKey(char)` en ASCII ; `Apple1KeyMap::mapKey` lit des codes **GLFW** | **À adapter** : « Clavier libretro » |
| Manette (`RETRO_DEVICE_JOYPAD`) | L'Apple-1 n'a pas de manette : les jeux lisent le clavier | **À adapter** : profils manette → touches par programme, comme le cœur POM2 |
| Contenu | Hex dumps `.txt .hex .apl .mon .tur` (`pom1::isHexDumpPath`), cassettes `.wav .aiff`, cartouches CodeTank, images D64 (IEC), fichiers de preset (`--preset-file`) ; les cartes se branchent seules selon le dossier ou le code (`SoftwareDirRules.h`, `ProgramCardSniff.h`) | **Prêt** avec `need_fullpath = true`, sauf « Démarrage automatique » et les `.bin` sans adresse |
| Choix de la machine | 13 presets, `machinePresetAt(int)`, preset externe validé par `CardTopology` | **Prêt** : une option de cœur ; les presets DevBench (0-2) ne sont pas proposés |
| Carte mémoire (succès, triches) | `Memory::getMemoryPointerMutable()` : les 64 Ko, `std::vector` jamais réalloué | **Prêt** (`RETRO_MEMORY_SYSTEM_RAM` + descripteurs ; la VRAM TMS en option) |
| Changement de cassette (interface disque) | Charger, éjecter, PLAY | **À adapter**, facultatif |
| Bibliothèque partagée | `pom1_core`, `pom1_devices`… sont des cibles `INTERFACE` **sans sources** ; seuls les tests compilent le cœur (`pom1_test_devices`, bibliothèque OBJECT) | **À adapter** : « Une vraie bibliothèque du cœur » |
| Fils | Deterministic garde un fil d'émulation **garé**. `WiFiModem` a son fil, `CommandPort` les siens | **À adapter** : accepter le fil garé, ou un mode sans fil du tout ; les cartes réseau sont exclues (règles) |
| Charger/décharger 100 fois | Pas de mesure. Un premier SID coûte ~120 ms par processus, puis 0,46 ms | **Ouvert** : un test qui recharge et compare l'état, comme `neost-selftest` |

## Les chantiers « à adapter »

### Terminal hors de l'UI

Le plus gros morceau, et **il profite aussi au bureau**. Aujourd'hui la grille de l'Apple-1
vit dans `Screen_ImGui` (classe UI, GL), qui est aussi le `DisplayDevice` de la machine.
Il faut en sortir, sans ImGui ni GL :

- la grille 40×24, le défilement circulaire, le curseur, `writeChar` ;
- la séquence de mise sous tension (écran de garbage, effacement) ;
- `serialize` / `deserialize`, **dans le même format** : les états restent échangeables
  entre le bureau et RetroArch ;
- la rastérisation CPU 280×192 (`buildScreenFramebuffer`, cellules 7×8, glyphes de
  `charmap.rom`), en blanc ; la teinte est appliquée par le frontend.

**Piège de déterminisme** : les durées de la séquence de démarrage sont en **secondes
murales** (`GARBAGE_DURATION = 1.5f`, `BLACK_SCREEN_DURATION`). Dans le modèle sorti de
l'UI, elles doivent compter des trames ou des cycles. Sinon deux exécutions du même film
ne montrent pas la même image.

`Screen_ImGui` deviendrait l'afficheur de ce modèle, et le headless gagnerait un vrai
écran (une capture `--dump-text-frame` deviendrait possible).

### Une vraie bibliothèque du cœur

Les cibles `pom1_core` / `pom1_devices` ne propagent que des drapeaux. Il faut une
bibliothèque statique **PIC, à visibilité cachée**, avec les sources de
`POM1_TEST_CORE_FILES` (`tests/CMakeLists.txt`), que l'application, les tests et le cœur
libretro lient tous trois. Elle est construite avec `-DPOM1_DEVTOOLS=OFF`, sans ImGui,
sans GLFW et sans périphérique audio (miniaudio sert encore à décoder les cassettes).
`architecture_check` garde déjà le sens des dépendances.

### Borne de l'état

`retro_serialize_size` doit rester constant pendant une session. La borne vaut la taille
d'un état avec **toutes** les cartes de la machine choisie, plus la plus grande section
GEN2VID possible : il faut donc un **plafond d'événements par champ** dans
`Gen2VideoScanner`, s'il n'existe pas encore. Le blob est complété par des zéros, et la
lecture ignore ce remplissage. Un changement de preset recharge le jeu, donc la borne
est recalculée.

### RTC figée à travers les états

En libretro, la RTC de l'A1-IO doit **toujours** avancer en temps émulé, et une
restauration d'état ne doit pas la ramener à l'heure de l'hôte : l'ancre et le compte de
cycles voyagent dans l'état. Sinon le run-ahead (une restauration par trame) et le netplay
divergent dès qu'un programme lit l'heure.

### Clavier libretro

- Le cœur enregistre `RETRO_ENVIRONMENT_SET_KEYBOARD_CALLBACK`. Avec *Auto Game Focus =
  Detect*, RetroArch passe alors en *Game Focus* au chargement. La documentation du cœur
  le dira, comme celle du cœur rp6502 (autre machine 6502 pilotée au clavier).
- On prend le **caractère** fourni par le frontend quand il existe : la disposition
  AZERTY ou QWERTZ est alors réglée par l'hôte. Le code `RETROK_*` sert pour Entrée,
  Échap et Ctrl+lettre. On met en majuscules, comme l'Apple-1. **Retour arrière envoie
  `_`**, jamais `$08`, même règle que le bureau.
- La table de correspondance est une fonction pure, testée, à côté de
  `src/Apple1KeyMap.h`.
- Pour jouer sans clavier : profils manette → touches par programme, et un **clavier
  virtuel** pour taper une commande Monitor (`4000R` pour une cartouche CodeTank).

### Démarrage automatique

Un hex dump place des octets en mémoire mais ne dit pas toujours où démarrer, et un `.bin`
ne dit même pas où se charger. Il faut une convention lue par le cœur, à trancher :
adresse dans le nom (`PROG.0300.bin`), fichier voisin, ou entrée d'un `.m3u`. Sans
adresse, le cœur charge et laisse l'utilisateur au Monitor, ce qui est fidèle à 1976.

## ROM Apple

Le Woz Monitor, Integer BASIC, l'ACI et le générateur de caractères sont du firmware
d'Apple, sans licence libre (`roms/README.md` § 1). POM1 compile en plus le Woz Monitor
**dans le binaire**, comme repli. Un cœur distribué par le buildbot libretro
distribuerait donc du firmware Apple, même sans fichier système. **À trancher avec
l'équipe libretro avant la soumission** :

- soit le cœur garde le repli, comme le binaire de bureau ;
- soit le cœur ne contient aucun firmware Apple, et tout vient du dossier système (liste
  des fichiers et de leurs empreintes dans le `.info`, comme le BIOS d'un autre cœur).

Le cœur AppleWin (firmware Apple II) est le précédent à regarder : il faut vérifier ce
qu'il embarque et ce qu'il demande dans le dossier système.

## Règles de l'adaptateur

1. **Le cœur reste sans dépendance libretro.** Aucun `#include "libretro.h"` hors de
   src/libretro.
2. **Aucune méthode publique de plus** sur `Memory` ni sur `EmulationController` : les
   cliquets `memory_public_methods` et `controller_public_methods` tiennent. Si
   l'adaptateur en réclame une, c'est le signe que la façade manque d'une chose que
   ses autres appelants voudraient aussi (`CLAUDE.md`, canal de contrôle).
3. **Au chargement d'un jeu** : `Logger` libretro, `ResourceLocator::rootedAt` sur le
   dossier système, contrôleur en `ExecutionMode::Deterministic`, preset de l'option,
   RTC figée, puis le contenu.
4. **Chaque trame** : entrées (`queueKey`, profil manette), `runCyclesSync(17 062)`,
   `mixSources` une fois, composition des écrans, `retro_video_refresh`,
   `SET_GEOMETRY` seulement si la géométrie a changé.
5. **Pas de réseau, pas de port ouvert** : Wi-Fi Modem, Terminal Card, `--cmd-port` et
   le port de télémétrie sont exclus du cœur. Un preset qui les contient (Multiplexing
   Fantasy) les perd en libretro, et le journal le dit.
6. **Toute décision est une fonction pure testée** : composition des écrans, clavier,
   choix de la carte montrée, borne de l'état. C'est la règle du dépôt (§ 2.5
   d'`ARCHITECTURE.md`), et c'est ce qui la rend vérifiable sans RetroArch.

## Ce qui ne passe pas en libretro

Le DevBench et les éditeurs, le débogueur, les inspecteurs, le PR-40 et son rouleau de
papier (à moins de l'écrire en `.txt` dans le dossier de sauvegarde), et le rewind propre
de POM1 (RetroArch a le sien, bâti sur `retro_serialize`). La vitesse ×2 / MAX est
remplacée par l'avance rapide du frontend.

## Ordre proposé

1. Terminal hors de l'UI (utile au bureau tout de suite, testable seul).
2. Bibliothèque du cœur, PIC, liée par l'application et les tests.
3. Composition des écrans en fonction pure, avec son test.
4. Adaptateur minimal : image, clavier, son, états.
5. Options, profils manette, interface cassette, succès.
6. `.info`, `.gitlab-ci.yml` (repris de POM2), puis la soumission à libretro, après la
   réponse sur les ROM Apple.
