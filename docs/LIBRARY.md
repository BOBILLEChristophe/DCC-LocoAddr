# DCC-LocoAddr

Bibliothèque Arduino qui lit le détecteur Locoduino LocoAddr (**carte v3.1**, voir [`detector`](../hardware/detector)) et fournit :

- l'**adresse DCC** de la locomotive présente sur le canton : adresse courte, longue ou de consist ;
- son **sens de pose** sur la voie, si l'option « sens de pose » est montée sur la carte.

Microcontrôleurs pris en charge :

| Carte | Détecteurs par carte | État |
|---|---|---|
| **ESP32** (classique, S2, S3, C3, C6, H2), y compris l'Arduino Nano ESP32 | 2 ou 3, un UART chacun | essayé sur réseau |
| **Raspberry Pi Pico** (RP2040) | jusqu'à 10 (9 sur Pico W) | **à essayer** : compile, pas encore essayé sur réseau |
| **Raspberry Pi Pico 2** (RP2350) | jusqu'à 14 (13 sur Pico 2 W) | **à essayer** |
| **Arduino Mega 2560** (AVR) | jusqu'à 3 sur Serial1, Serial2 et Serial3 | essayé sur réseau |

Le décodage proprement dit (`LocoAddrDecoder`) ne dépend d'aucun matériel. Seule la réception (UART, découpage des cutouts) est propre à chaque microcontrôleur. La classe s'appelle `LocoAddrReader` partout ; seul son constructeur change.

Version **5.1.0**. Elle remplace le code des anciens projets `Railcom-detector-on-ESP32`, `Railcom-detector-freeRtos-inClass` et `Railcom-Detector-Display`.

---

## Installation

1. Copier ce dossier `DCC-LocoAddr` (celui qui contient `library.properties`) dans le dossier des bibliothèques Arduino :
   - Windows : `Documents\Arduino\libraries\` ;
   - macOS et Linux : `~/Documents/Arduino/libraries/` ou `~/Arduino/libraries/`.
2. Redémarrer l'IDE Arduino.
3. Les exemples apparaissent dans *Fichier > Exemples > DCC-LocoAddr*, rangés par microcontrôleur : `ESP32/LocoAddr`, `ESP32/LocoAddr-multi`, `RP2040/LocoAddr`, `RP2040/LocoAddr-multi` et `AVR/Mega/LocoAddr-3det`.

Cœurs Arduino :
- **ESP32 :** paquet **esp32 d'Espressif**, en version 2.0.x (ESP-IDF 4.4) comme en version 3.x (ESP-IDF 5.x).
- **Raspberry Pi Pico :** paquet **Raspberry Pi Pico/RP2040/RP2350** d'Earle Philhower (vérifié avec la version 6.3.0).
- **Arduino Mega 2560 :** paquet **Arduino AVR Boards** ; réception sur les UART matériels `Serial1`, `Serial2` et `Serial3`.

La bibliothèque n'a pas d'autre dépendance.

---

## Câblage de la carte v3.1 (ESP32)

| Carte v3.1 | ESP32 | Remarque |
|---|---|---|
| J19 – 5V µC | 5V (VIN) | alimente le 6N137, qui doit recevoir **5 V** |
| J19 – OUT | GPIO4 | sortie série RailCom, 250 kbit/s |
| J19 – GND µC | GND | |
| J22 – VIO | **3V3** | fixe le niveau de OUT et de DIR. **Jamais 5 V avec un ESP32** |
| J22 – DIR | GPIO35 (ESP32 classique), GPIO5 (S2, S3, C3, C6, H2) | option sens de pose seulement ; sinon ne pas câbler |

Avec un Arduino 5 V, VIO se relierait au 5 V. Avec un ESP32, toujours au 3,3 V.

**VIO est indispensable** : c'est lui qui alimente la résistance de tirage R17 de la sortie OUT. La bibliothèque active en plus le tirage interne de l'ESP32 sur RX, environ 45 kΩ, ce qui peut dépanner sur un câble très court, mais ce n'est pas une solution fiable.

**Broches à éviter** pour OUT et DIR sur l'ESP32 classique (les autres puces ont leurs propres broches de démarrage, par exemple GPIO2, 8 et 9 sur le C3) :
- GPIO0, 2, 5, 12 et 15 : elles sont lues au démarrage ;
- GPIO1 et GPIO3 : elles sont reliées à l'adaptateur USB-série ;
- GPIO16 et GPIO17 sur les modules WROVER : elles sont prises par la PSRAM.

GPIO34, 35 et 39 sont des entrées seules, sans résistance de tirage interne. Elles conviennent très bien à DIR, déjà tiré vers VIO par R21 sur la carte.

---

## Utilisation

```cpp
#include <DCC_LocoAddr.h>

// UART, broche OUT, broche DIR (GPIO_NUM_NC si l'option n'est pas montée)
LocoAddrReader detector(UART_NUM_1, GPIO_NUM_4, GPIO_NUM_35);

void setup() {
  Serial.begin(115200);
  detector.begin();
}

void loop() {
  uint16_t adresse = detector.address();        // 0 = aucune locomotive
  LocoDirection sens = detector.direction(); // Normal, Reversed ou Unknown
  if (adresse) {
    Serial.printf("Loco %u, sens %s\n", adresse, LocoAddrReader::directionName(sens));
  }
  delay(100);
}
```

### Fonctions

| Fonction | Rôle |
|---|---|
| `LocoAddrReader(uart, rxPin, dirPin = GPIO_NUM_NC)` | Une instance par détecteur, chacune sur son propre UART |
| `bool begin(priorite = 10, coeur = tskNO_AFFINITY)` | Démarre l'UART et la tâche de réception |
| `void end()` | Arrête la tâche et libère l'UART |
| `uint16_t address()` | Adresse validée, 0 si aucune locomotive |
| `bool hasAddress()` | Vrai si une locomotive est présente |
| `LocoDirection direction()` | `Normal`, `Reversed`, ou `Unknown` (pas de loco, option absente ou lecture instable) |
| `LocoAddrStats stats()` | Compteurs de réception, pour le diagnostic |
| `LocoAddrReader::directionName(sens)` | Texte du sens : `"normal"`, `"inverse"` ou `"inconnu"` |

`address()`, `direction()` et `stats()` peuvent être appelées depuis n'importe quelle tâche.

### Réglages, à appeler avant `begin()`

| Fonction | Défaut | Rôle |
|---|---|---|
| `setAddressConfirmations(n)` | 5 | Nombre de lectures identiques de suite avant d'accepter une nouvelle adresse |
| `setLossTimeoutMs(ms)` | 1000 | Délai sans réception au bout duquel la locomotive est considérée comme partie |
| `setDirectionConfirmations(n)` | 3 | Nombre de lectures identiques de DIR avant de changer de sens |
| `setDirectionInverted(true)` | false | Échange `Normal` et `Reversed` (voir l'étalonnage ci-dessous) |
| `setBurstCallback(fonction, contexte)` | — | Appelle une fonction pour chaque cutout reçu, avec les octets bruts (diagnostic) |

---

## Sens de pose : étalonnage

> La lecture de DIR est validée par les tests du décodeur, mais pas encore sur une carte v3.1 réelle.

La valeur de DIR dépend du rail sur lequel le décodeur émet, donc de l'orientation de la locomotive. Le sens « normal » correspond simplement à DIR au niveau bas. Sa signification physique dépend du branchement de J20 et J21 sur la voie.

Pour l'étalonner, il suffit de le faire une seule fois :
1. Poser une locomotive cabine 1 vers un repère choisi, par exemple le côté de la gare.
2. Lire le sens affiché par l'exemple `LocoAddr` de votre microcontrôleur.
3. S'il indique `inverse`, mettre `setDirectionInverted(true)` dans le programme.

Le sens n'est connu que lorsqu'une locomotive est validée. Il repasse à `Unknown` quand elle quitte le canton.

---

## Fonctionnement

1. **Réception.** L'UART matériel de l'ESP32 reçoit le signal à 250 kbit/s en 8N1, par le pilote ESP-IDF et ses événements. Il n'y a pas d'interrogation en boucle.
2. **Découpage par cutout.** Deux cutouts sont séparés par un paquet DCC complet, soit plusieurs millisecondes sans aucun octet. La bibliothèque regroupe les octets reçus en *rafales*, une par cutout, closes par un silence de plus de 1,5 ms.
3. **Lecture à des positions fixes.** Dans un cutout, le canal 1 occupe les deux premiers octets, et le canal 2 commence juste après. La bibliothèque exige un ADR1 ou un ADR2 en tête de la rafale, puis parcourt le canal 2 datagramme par datagramme, en sautant les ACK et en tenant compte de la longueur propre à chaque type de datagramme. Un octet situé au milieu d'un datagramme ne peut donc plus être pris pour une adresse, contrairement aux versions 4.x qui lisaient le flux d'un seul bloc.
4. **Adresse.** Selon les décodeurs, ADR1 et ADR2 arrivent en alternance dans le canal 1, un par cutout, ou bien l'une des moitiés dans le canal 1 et l'autre dans le canal 2 du même cutout. Les deux cas sont gérés : chaque moitié reçue est associée à l'autre moitié la plus récente, prise dans le même cutout ou dans celui d'avant. Une adresse est donc reconstruite à chaque cutout. Les valeurs incohérentes sont rejetées :
   - ADR1 autre que 0x00, 0x60 ou 10xxxxxx ;
   - adresse 0 ;
   - adresse longue hors de la plage 128 à 10239.
5. **Validation.**
   - Une nouvelle adresse doit être lue 5 fois de suite.
   - Une adresse déjà validée est confirmée par chaque lecture correcte.
   - Elle est perdue après 1 s sans lecture.
6. **Sens de pose.** Après chaque cutout qui contient un ADR1 ou un ADR2 valide, DIR est lu. La bascule 74HC74 de la carte vient alors d'être chargée par les octets de la locomotive. Le sens ne change qu'après 3 lectures identiques.

---

## Arduino Mega 2560 (AVR)

L'Arduino Mega 2560 possède quatre UART matériels. `Serial` est conservé pour le moniteur série USB ; `Serial1`, `Serial2` et `Serial3` permettent donc de raccorder jusqu'à trois détecteurs LocoAddr simultanément.

À 16 MHz, l'USART matériel de l'ATmega2560 permet la réception à 250000 bauds.

### Câblage

| Carte v3.1 | Arduino Mega 2560 | Remarque |
|---|---|---|
| J19 – 5V µC | 5V | alimente le 6N137 |
| J19 – OUT | RX1 broche 19, RX2 broche 17 ou RX3 broche 15 | 250 kbit/s |
| J19 – GND µC | GND | |
| J22 – VIO | **5V** | niveau logique adapté au Mega 2560 |
| J22 – DIR | entrée numérique au choix | option sens de pose seulement |

### Utilisation

```cpp
#include <DCC_LocoAddr.h>

LocoAddrReader detector1(Serial1);
LocoAddrReader detector2(Serial2);
LocoAddrReader detector3(Serial3);

void setup()
{
    detector1.begin();
    detector2.begin();
    detector3.begin();
}

void loop()
{
    detector1.poll();
    detector2.poll();
    detector3.poll();
}
```

`poll()` doit être appelée aussi régulièrement que possible depuis `loop()`.

L'exemple complet se trouve dans `examples/AVR/Mega/LocoAddr-3det/LocoAddr-3det.ino`.

---

## Raspberry Pi Pico (RP2040, RP2350)

> **Version d'essai.** Le code compile pour le Pico et le Pico 2, mais il n'a pas encore été essayé sur un réseau. Merci de signaler le résultat de vos essais.

### Câblage

| Carte v3.1 | Pico | Remarque |
|---|---|---|
| J19 – 5V µC | VBUS (broche 40) | 5 V de l'USB ; alimente le 6N137 (10 à 15 mA par détecteur) |
| J19 – OUT | une broche GPn | voir ci-dessous |
| J19 – GND µC | GND | |
| J22 – VIO | **3V3(OUT)** (broche 36) | **jamais 5 V** : le Pico est en 3,3 V |
| J22 – DIR | une broche GPn | option sens de pose seulement |

### Utilisation

```cpp
#include <DCC_LocoAddr.h>

LocoAddrReader detector(1);       // OUT sur GP1, sans DIR
LocoAddrReader detector2(2, 11);  // OUT sur GP2, DIR sur GP11

void setup() {
  detector.begin();
  detector2.begin();
}
```

Le reste (`address()`, `direction()`, `stats()`, réglages) est identique à l'ESP32. Il n'y a rien à appeler dans `loop()` : une interruption de minuterie lit tous les détecteurs toutes les 250 µs.

### Ressources et nombre de détecteurs

Chaque détecteur prend, selon sa broche OUT :

- **un UART matériel** si la broche est une broche RX d'UART encore libre : UART0 sur GP1, GP13, GP17 ou GP29 ; UART1 sur GP5, GP9, GP21 ou GP25 ;
- **sinon une machine d'état PIO**, sur n'importe quelle broche.

| Carte | UART | Machines d'état PIO | Détecteurs au maximum |
|---|---|---|---|
| Pico (RP2040) | 2 | 8 | **10** |
| Pico W | 2 | 7 (une sert au Wi-Fi) | **9** |
| Pico 2 (RP2350) | 2 | 12 | **14** |
| Pico 2 W | 2 | 11 | **13** |

`transportName()` indique la ressource prise (« UART0 », « PIO1 SM2 »…). La constante `LocoAddrReader::MAX_READERS` donne le maximum pour la carte choisie.

### Précautions

- **Serial1 et Serial2** : un UART pris par un détecteur n'est plus disponible pour eux. Pour garder Serial1, placer le détecteur sur une broche qui n'est pas une broche RX d'UART, ou appeler `forcePio()` avant `begin()`. Le moniteur série (Serial, par l'USB) n'est pas concerné.
- **Fonction de rappel** (`setBurstCallback`) : elle s'exécute **sous interruption**. Elle doit seulement copier les octets ; jamais de `Serial.print()` ni de `delay()`. L'exemple `RP2040/LocoAddr` montre comment faire.
- Les broches GP23, GP24, GP25 et GP29 servent à la carte Pico elle-même : ne pas les utiliser.
- Les récepteurs PIO ne détectent pas les erreurs de trame : seul le compteur `frameErrors` des UART matériels est renseigné.

---

## Plusieurs détecteurs (ESP32)

Chaque détecteur utilise son propre UART. Voir l'exemple `ESP32/LocoAddr-multi`.

| ESP32 classique | Détecteurs |
|---|---|
| UART1 + UART2, moniteur série sur UART0 | 2 |
| UART0 + UART1 + UART2, sans moniteur série | 3 |

Si UART0 est utilisé, son entrée doit être placée sur une autre broche que GPIO3, par exemple GPIO33. GPIO3 est reliée à la puce USB-série de la carte ESP32.

---

## Diagnostic

Les exemples ont un réglage `LOG_LEVEL` qui choisit la quantité de messages :

| Niveau | Messages affichés |
|---|---|
| `LOG_NONE` | aucun |
| `LOG_ERROR` | erreurs seulement : initialisation, réception UART |
| `LOG_INFO` | en plus, arrivée et départ des locomotives, et sens de pose. C'est le niveau par défaut |
| `LOG_VERBOSE` | en plus, la configuration au démarrage et les statistiques toutes les 10 s |
| `LOG_DEBUG` | en plus, le contenu brut de chaque cutout, en hexadécimal |

Au niveau `LOG_VERBOSE`, les compteurs affichés sont les suivants :

| Compteur | Signification |
|---|---|
| `cutouts` | rafales reçues. Avec une locomotive équipée d'un décodeur compatible RailCom, ce nombre doit augmenter régulièrement |
| `adr` (`ADR1`, `ADR2`) | moitiés d'adresse valides reçues, au total puis séparément |
| `derniere_lue` | dernière adresse reconstruite, même pas encore validée |
| `rejetes` | rafales sans canal 1 exploitable. C'est normal si le décodeur n'émet qu'en canal 2 |
| `invalides` | ADR1 et ADR2 incohérents |
| `trop_longs` | rafales de plus de 16 octets : parasites, ou cutouts trop rapprochés |
| `debordements`, `erreurs_trame` | problèmes de réception UART |

Le niveau `LOG_DEBUG` produit environ 130 lignes par seconde : il sert seulement à diagnostiquer le câblage.

---

## Tests

Le décodage (`LocoAddrDecoder`) ne dépend pas de l'ESP32. On peut donc le tester sur un PC :

```
cd extras/tests
g++ -std=c++11 -I../../src test_decoder.cpp ../../src/LocoAddrDecoder.cpp -o test && ./test
```

Les tests couvrent :
- la table 4/8 ;
- les adresses courtes, longues et de consist ;
- les rafales du canal 2 seules ;
- la perte puis le retour d'une locomotive ;
- le changement de locomotive ;
- le sens de pose et son inversion ;
- 20 000 rafales aléatoires, qui ne doivent produire aucune adresse.

---

## Historique

- **5.0.0** :
  - bibliothèque unique ;
  - découpage par cutout ;
  - reconstruction stricte des adresses ;
  - règle de validation unique ;
  - sens de pose (carte v3.1) ;
  - statistiques ;
  - compatible avec les paquets esp32 2.0.x et 3.x, et avec les ESP32-S2, S3, C3, C6 et H2.
- **4.7** : réception par événements UART, avec des variantes divergentes entre les trois anciens projets.

---

© Christophe BOBILLE – Locoduino – licence GPL-3.0
https://github.com/BOBILLEChristophe/DCC-LocoAddr

---

*RailCom® est une marque déposée de Lenz Elektronik GmbH. Ce projet n'est ni affilié à Lenz Elektronik ni approuvé par elle. Le nom RailCom n'y est employé que pour désigner la norme de communication bidirectionnelle (NMRA S-9.3.2, RCN-217) que le matériel et le logiciel respectent.*
