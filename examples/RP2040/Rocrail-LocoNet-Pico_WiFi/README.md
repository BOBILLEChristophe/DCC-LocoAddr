# LocoAddr + Raspberry Pi Pico W + LocoNet over TCP

## Identification des locomotives dans Rocrail sur 4 cantons
### Jusqu’à 8 cantons possibles par Raspberry Pi Pico W

Cet exemple montre une chaîne complète d’identification des locomotives depuis la voie jusqu’à Rocrail.

Quatre détecteurs **LocoAddr** sont placés sur quatre sections de voie isolées. Un **Raspberry Pi Pico W** lit leur adresse DCC et transmet les changements à Rocrail en Wi-Fi au moyen d’un serveur **LocoNet over TCP (LbServer)**.

Le programme envoie des messages de transpondage LocoNet `OPC_MULTI_SENSE`. Rocrail peut ainsi identifier automatiquement la locomotive présente dans chaque bloc.

L’utilisation de LocoNet over TCP rend également cet exemple utilisable avec **JMRI** et potentiellement avec d’autres logiciels compatibles.

## Fichiers

- `Rocrail-LocoNet-Pico_WiFi.ino` : détecteurs, Wi-Fi et intégration générale
- `LbServer.h` / `LbServer.cpp` : serveur LocoNet over TCP
- `Transponder.h` / `Transponder.cpp` : gestion des cantons et messages `OPC_MULTI_SENSE`

## Configuration par défaut

L’exemple est configuré pour quatre détecteurs :

`NB_DETECTORS = 4`

Correspondance :

- détecteur 1 → GP1 → canton 1
- détecteur 2 → GP2 → canton 2
- détecteur 3 → GP3 → canton 3
- détecteur 4 → GP4 → canton 4

Le Raspberry Pi Pico W peut gérer jusqu’à **8 cantons** dans cette application.

## Documentation complète

Le câblage, le fonctionnement détaillé, les réglages Rocrail, les captures d’écran et la vidéo sont documentés ici :

[Documentation complète : Pico W + LocoNet + Rocrail](../../../docs/integration/rocrail-loconet-pico-wifi.md)

RailCom® est une marque déposée de Lenz Elektronik GmbH.
LocoNet® est une marque de Digitrax, Inc.
