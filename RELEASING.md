# Publication de DCC-LocoAddr

Ce dépôt est structuré pour servir à la fois de :

- dépôt GitHub principal du projet ;
- bibliothèque Arduino ;
- bibliothèque PlatformIO ;
- projet PlatformIO de développement et de validation.

## 1. Avant une version

Mettre exactement le même numéro de version dans :

- `library.properties` ;
- `library.json` ;
- `DCC_LOCOADDR_VERSION` dans `src/DCC_LocoAddr.h` ;
- `CHANGELOG.md`.

Puis lancer :

```bash
python3 scripts/check_version.py
./scripts/test_decoder.sh
```

Avec PlatformIO installé :

```bash
pio run -e esp32-basic
pio run -e esp32-multi
pio run -e esp32-display
pio run -e esp32c3-mini-display
pio pkg pack
```

`pio pkg pack` valide aussi `library.json` et permet de vérifier le contenu réel du paquet PlatformIO.

## 2. GitHub

Créer un dépôt public vide nommé :

```text
DCC-LocoAddr
```

Puis, depuis le dossier local :

```bash
git init
git add .
git commit -m "Initial DCC-LocoAddr v5.0.0"
git branch -M main
git remote add origin https://github.com/BOBILLEChristophe/DCC-LocoAddr.git
git push -u origin main
```

Créer ensuite le premier tag :

```bash
git tag -a v5.0.0 -m "DCC-LocoAddr 5.0.0"
git push origin v5.0.0
```

Le tag est indispensable pour Arduino Library Manager.

## 3. Arduino Library Manager

Le fichier `library.properties` est volontairement placé à la racine du dépôt, comme l'exige Arduino Library Manager.

Pour la première inscription :

1. créer une release ou un tag conforme ;
2. forker `arduino/library-registry` ;
3. ajouter `https://github.com/BOBILLEChristophe/DCC-LocoAddr` dans `repositories.txt` ;
4. ouvrir une Pull Request vers `arduino/library-registry` ;
5. corriger, si nécessaire, les remarques du bot Arduino.

Une fois le dépôt enregistré, les nouvelles versions conformes sont détectées automatiquement à partir des tags.

Documentation :

- https://github.com/arduino/library-registry
- https://docs.arduino.cc/arduino-cli/library-specification

## 4. PlatformIO Registry

Le manifeste PlatformIO est `library.json`, également à la racine.

Première publication :

```bash
pio account login
pio pkg pack
pio pkg publish
```

Pour une CI, PlatformIO accepte le jeton dans la variable :

```text
PLATFORMIO_AUTH_TOKEN
```

À chaque nouvelle publication PlatformIO, augmenter la version avant `pio pkg publish`. Une combinaison nom/version publiée ne doit pas être réutilisée.

Documentation :

- https://docs.platformio.org/en/latest/librarymanager/creating.html
- https://docs.platformio.org/en/latest/core/userguide/pkg/cmd_publish.html

## 5. Anciens dépôts

Ne supprimer les trois anciens dépôts qu'après avoir vérifié :

- que `DCC-LocoAddr` est bien accessible publiquement ;
- que tous les fichiers matériels ont été transférés ;
- que le tag `v5.0.0` existe ;
- que les liens de l'article Locoduino ont été mis à jour.

Les trois anciens dépôts sont :

```text
Railcom-detector-on-ESP32
Railcom-detector-freeRtos-inClass
Railcom-Detector-Display
```

Pour une suppression via GitHub : `Settings > General > Danger Zone > Delete this repository`.

Avec GitHub CLI, après vérification :

```bash
gh repo delete BOBILLEChristophe/Railcom-detector-on-ESP32 --yes
gh repo delete BOBILLEChristophe/Railcom-detector-freeRtos-inClass --yes
gh repo delete BOBILLEChristophe/Railcom-Detector-Display --yes
```

Une alternative plus conservatrice consiste à les archiver après avoir remplacé leur README par un lien vers `DCC-LocoAddr`.
