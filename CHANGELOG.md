# Historique des versions

## 5.0.1 — 10/2026

- Réorganisation des fichiers matériels sous `hardware/`.
- Déplacement des wrappers PlatformIO sous `development/platformio/`.
- Mise à jour des chemins dans la documentation et la configuration PlatformIO.
- Mise à jour du schéma du détecteur LocoAddr v3.1.
- Aucun changement d'API de la bibliothèque.

## 10/2026 — regroupement des projets

### Dépôt
- Les projets `Railcom-detector-on-ESP32`, `Railcom-detector-freeRtos-inClass` et `Railcom-Detector-Display` sont regroupés dans un seul dépôt.
- Chaque matériel a désormais son schéma, son fichier Fritzing, ses Gerbers, sa BOM et son README.
- Nouveau nom : **DCC-LocoAddr**. RailCom® étant une marque déposée de Lenz Elektronik GmbH, le mot n'est plus employé que pour désigner la norme, jamais comme nom de produit.

### Détecteur LocoAddr v3.1
- U5 en 74HC02, au lieu du 7402 TTL de l'ancienne BOM.
- R1 = R2 = 1 kΩ ; R15 = 1 kΩ ; C10 ajouté ; R18 et R19 ajoutées.
- 6N137 en 5 V côté microcontrôleur, R17 = 2,2 kΩ tirée vers VIO : sortie compatible 3,3 V (ESP32) et 5 V (Arduino).
- Sorties sur J19 (5V µC, OUT, GND µC) et J22 (VIO, DIR).
- Option sens de pose sur la même carte (74HC74, PC817, LED bicolore). Elle n'a pas encore été essayée sur une carte réelle.
- Trous de D4 et D5 agrandis à 1,3 mm. Empreinte TO-92 d'IC2 corrigée : l'ancienne générait un perçage non métallisé parasite.

### Bibliothèque DCC-LocoAddr v5.0.0
- Nouveaux noms : bibliothèque `DCC-LocoAddr` (sans référence au microcontrôleur, pour permettre de futurs portages), exemples `LocoAddr` et `LocoAddr-multi`, en-tête `DCC_LocoAddr.h`, classe `LocoAddrReader` (au lieu de `Railcom`), types `LocoDirection` et `LocoAddrStats`, macro `DCC_LOCOADDR_VERSION`.
- Bibliothèque commune pour tous les programmes, compatible ESP32, S2, S3, C3, C6 et H2 (core Arduino 2.x et 3.x).
- Décodage 4/8 du canal 1 (ADR1 et ADR2), y compris quand ADR2 arrive dans le canal 2.
- Adresses courtes, longues et de consist.
- Validation de l'adresse : 5 lectures identiques ; perte de la locomotive après 1 s.
- Sens de pose (option du détecteur v3.1), plusieurs détecteurs (un UART chacun), statistiques et niveaux de messages.
- Tests du décodeur exécutables sur PC.

### LocoAddr Display v5.0.0 (circuit imprimé v2.1)
- Décodage par la bibliothèque commune.
- Multiplexage à 125 Hz : l'afficheur ne scintille plus.
- Adresses de consist corrigées.

### Mini Display v5.0.0 (circuit imprimé v3.0)
- Nouveau circuit imprimé : ESP32-C3 SuperMini, adaptateur de niveau Pololu 2595, module TM1637 en 5 V, alimentation 9-12 V par module abaisseur.
- Programme fondé sur la bibliothèque commune ; validation en 5 lectures au lieu de 20.

### Packaging Arduino / PlatformIO
- La bibliothèque est maintenant placée directement à la racine du dépôt (`src/`, `examples/`, `library.properties`).
- Ajout du manifeste `library.json` pour PlatformIO Registry.
- Ajout d'un `platformio.ini` multi-environnements pour compiler les exemples et les deux afficheurs.
- Ajout d'une CI GitHub : Arduino Lint, tests natifs, builds PlatformIO et validation du paquet.
- Ajout des scripts de contrôle de version et de test du décodeur.
