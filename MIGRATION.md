# Migration vers DCC-LocoAddr

Ce dépôt regroupe et remplace les trois anciens dépôts :

- `BOBILLEChristophe/Railcom-detector-on-ESP32`
- `BOBILLEChristophe/Railcom-detector-freeRtos-inClass`
- `BOBILLEChristophe/Railcom-Detector-Display`

La bibliothèque commune se trouve désormais directement à la racine du dépôt :

```text
library.properties
library.json
src/
examples/
```

Les matériels restent regroupés dans :

```text
hardware/
├── detector/
├── display/
└── display_min/
```

## Correspondance

| Ancien projet | Nouveau chemin |
|---|---|
| Détecteur ESP32 | `src/` + `examples/LocoAddr/` + `hardware/detector/` |
| Version FreeRTOS en classe | `src/` + `examples/LocoAddr-multi/` |
| RailCom Detector Display | `hardware/display/` |
| Mini Display | `hardware/display_min/` |

À partir de la v5.0.0, le code de décodage n'est plus dupliqué entre les applications : toutes utilisent la bibliothèque `DCC-LocoAddr`.
