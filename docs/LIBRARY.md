# DCC-LocoAddr

Bibliothèque Arduino qui lit le détecteur Locoduino LocoAddr (**carte v3.1**, voir [`detector`](../detector)) et fournit :

- l'**adresse DCC** de la locomotive présente sur le canton : adresse courte, longue ou de consist ;
- son **sens de pose** sur la voie, si l'option « sens de pose » est montée sur la carte.

Cette version fonctionne sur **ESP32** (classique, S2, S3, C3, C6, H2). Le décodage proprement dit (`LocoAddrDecoder`) ne dépend d'aucun matériel : il pourra servir à des portages vers d'autres cartes, par exemple les Arduino récents ou le Raspberry Pi Pico.

Version **5.0.0**. Elle remplace le code des anciens projets `Railcom-detector-on-ESP32`, `Railcom-detector-freeRtos-inClass` et `Railcom-Detector-Display`.

---

## Installation

### Arduino IDE

Après publication dans Arduino Library Manager, rechercher **DCC-LocoAddr** dans le gestionnaire de bibliothèques.

Pour travailler directement sur la version GitHub, cloner le dépôt dans le dossier `libraries` du sketchbook Arduino. Le dépôt lui-même est la racine de la bibliothèque : `library.properties`, `src/` et `examples/` se trouvent au premier niveau.

Les exemples apparaissent ensuite dans *Fichier > Exemples > DCC-LocoAddr*.

### PlatformIO

Avant publication dans PlatformIO Registry, la bibliothèque peut être utilisée directement depuis GitHub :

```ini
lib_deps =
    https://github.com/BOBILLEChristophe/DCC-LocoAddr.git
```

Le dépôt contient également un `platformio.ini` permettant de compiler directement les exemples et les afficheurs.

La bibliothèque compile avec le paquet **esp32 d'Espressif**, en version 2.0.x (ESP-IDF 4.4) comme en version 3.x (ESP-IDF 5.x), pour l'ESP32 classique, les ESP32-S2, S3, C3, C6 et H2. Elle n'a pas d'autre dépendance.

---

## Câblage de la carte v3.1

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
2. Lire le sens affiché par l'exemple `LocoAddr`.
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

## Plusieurs détecteurs

Chaque détecteur utilise son propre UART. Voir l'exemple `LocoAddr-multi`.

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
