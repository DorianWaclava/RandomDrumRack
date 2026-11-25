# Random Drum Rack — VST3 / AU pour Ableton Live

Un instrument façon Drum Rack : 16 pads (C1 → D#2, mêmes notes que le Drum Rack d'Ableton).
Chaque pad pioche un sample **au hasard** dans un dossier (sous-dossiers compris).

## Fonctions

| Action | Comment |
|---|---|
| Dossier pour **tous les pads** | Bouton **Dossier de samples…** (scan récursif : wav, aif/aiff, flac, ogg, mp3). Les pads non verrouillés qui l'utilisent tirent tout de suite un nouveau sample |
| Appliquer le dossier d'un pad à tous | DIR → « Appliquer le dossier de ce pad à TOUS les pads » |
| Remettre tous les pads sur le dossier global | DIR → « Remettre TOUS les pads sur le dossier global » |
| **Randomiser tous les pads d'un coup** | Bouton **RANDOMISER TOUT** |
| **Randomiser un seul pad** | Bouton **RND** du pad |
| Protéger un pad de « Randomiser tout » | **LOCK** (le RND du pad marche toujours) |
| Dossier propre à un pad (ex. Kicks sur le pad 1, Snares sur le pad 2) | **DIR** → « Choisir un dossier pour ce pad… », ou glisser un dossier sur le pad |
| Charger un sample précis | DIR → « Charger un sample précis… », ou glisser un fichier sur le pad |
| Pré-écouter | Clic gauche sur le pad (clic droit = menu) |
| Réglages par pad | Vol (dB), Pitch (±24 demi-tons), Pan |
| Randomiser depuis Ableton | Paramètre automatisable **Randomize All** : chaque passage OFF → ON relance un tirage. Tu peux l'automatiser dans un clip ou le mapper sur un contrôleur MIDI |

- Le même sample n'est jamais tiré deux fois de suite sur un pad.
- Les samples tirés, dossiers et verrous sont **sauvegardés dans le projet Live** : à la réouverture tu retrouves exactement le même kit.
- Les samples sont joués en one-shot (le note-off est ignoré, comme un pad de batterie), et tronqués à 30 s.

## Obtenir le plugin sans rien installer (GitHub)

1. Crée un compte sur github.com puis un nouveau dépôt (bouton **New**, peu importe le nom).
2. Sur la page du dépôt : **Add file → Upload files**, glisse **le contenu** du dossier `RandomDrumRack` (y compris le dossier caché `.github`) → **Commit changes**.
   Si le dossier `.github` ne passe pas par le glisser-déposer : **Add file → Create new file**, nom `.github/workflows/build.yml`, colle le contenu du fichier.
3. Onglet **Actions** : la compilation démarre (environ 10 à 15 min). Quand c'est vert, clique sur le build et télécharge **RandomDrumRack-Windows** ou **RandomDrumRack-macOS** en bas de la page.
4. Installe (voir « Installer » plus bas).
   - Mac : le plugin n'est pas signé. Avant d'ouvrir Live, lance dans le Terminal :
     `xattr -cr ~/Library/Audio/Plug-Ins/VST3/"Random Drum Rack.vst3"`

## Compiler soi-même

Prérequis : **CMake ≥ 3.22**, **git**, et un compilateur C++17 :
- Windows : Visual Studio 2022 (workload « Développement Desktop en C++ »)
- macOS : Xcode (+ `xcode-select --install`)

JUCE 8 est téléchargé automatiquement au premier lancement de CMake.

```bash
cd RandomDrumRack
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

Le plugin se trouve ensuite dans :
- `build/RandomDrumRack_artefacts/Release/VST3/Random Drum Rack.vst3`
- (macOS) `build/RandomDrumRack_artefacts/Release/AU/Random Drum Rack.component`
- Une version **Standalone** est aussi générée pour tester sans Ableton.

### Installer
- Windows : copier le `.vst3` dans `C:\Program Files\Common Files\VST3\`
- macOS : copier le `.vst3` dans `~/Library/Audio/Plug-Ins/VST3/` (et le `.component` dans `~/Library/Audio/Plug-Ins/Components/`)

Dans Ableton : Préférences → Plug-ins → activer « Use VST3 Plug-in System Folders » → **Rescan**.
Le plugin apparaît dans Plug-ins → MyStudio → Random Drum Rack. Mets-le sur une piste MIDI et joue/programme les notes C1 à D#2.

## Organisation du code

- `Source/PluginProcessor.*` : moteur audio (lecteur de samples polyphonique 48 voix, interpolation, rééchantillonnage), scan des dossiers, tirage aléatoire et chargement sur un thread séparé (le son ne craque pas pendant un tirage), sauvegarde de l'état.
- `Source/PluginEditor.*` : interface (grille 4×4, pad 1 en bas à gauche comme dans Live).

## Idées pour la suite
- Sorties séparées par pad (multi-out) pour traiter chaque son sur sa propre piste
- Groupes de choke (ex. charley ouvert / fermé)
- Historique pour revenir au tirage précédent
- Note MIDI dédiée pour déclencher la randomisation d'un pad précis
