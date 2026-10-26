/*
   LocoAddr-LocoNet-Pico - détecteurs LocoAddr lus par un Raspberry Pi Pico W
   (ou Pico 2 W) et envoyés à Rocrail (ou JMRI) par LocoNet sur TCP

   Reprend l'exemple RP2040/LocoAddr-multi (une instance LocoAddrReader par
   détecteur) et LocoNet-Test (serveur LbServer + transpondage) : chaque fois
   que la loco change dans un canton, le Pico envoie à Rocrail "ancienne loco
   absente" puis "nouvelle loco présente" (message Digitrax OPC_MULTI_SENSE).
   Rocrail en tire le code de la loco et la place dans le bloc.

   Le détecteur 1 correspond au canton 1, le détecteur 2 au canton 2, etc.

   Organisation :
     LbServer     (LbServer.h/.cpp)    : le réseau (connexion, lignes, keepalive)
     Transponder  (Transponder.h/.cpp) : les cantons et les messages LocoNet
     ce fichier                        : détecteurs, Wi-Fi, et le lien entre les deux

   Cœur Arduino : "Raspberry Pi Pico/RP2040/RP2350" d'Earle Philhower.
   Carte à choisir dans l'IDE : "Raspberry Pi Pico W" (ou "Pico 2 W").

   Câblage commun à tous les détecteurs (cartes v3.1) :
     J19  5V µC  -> VBUS (broche 40)       chaque 6N137 consomme 10 à 15 mA
     J19  GND µC -> GND
     J22  VIO    -> 3V3(OUT) (broche 36)   (JAMAIS 5 V)
   Câblage propre à chaque détecteur n :
     J19  OUT    -> RX_PINS[n]
   GP23 à GP25 et GP29 servent au Wi-Fi : ne pas les utiliser.

   Réglages Rocrail :
     - centrale (Controller) LocoNet, type LbServer, Hostname = adresse IP du
       Pico (affichée au démarrage), Port 1234, Interface ID "LN" ;
     - un capteur par canton : Interface ID LN, Node ID 0, Address = numéro du
       canton, type Railcom ;
     - dans chaque bloc, onglet Routes : le capteur avec l'événement "ident" ;
     - champ "Code" de chaque loco = son adresse DCC.

----------------------------------------------------------------------------------------------

  LocoAddr-LocoNet-Pico - LocoAddr detectors read by a Raspberry Pi Pico W
  (or Pico 2 W) and sent to Rocrail (or JMRI) via LocoNet over TCP

  Based on the RP2040/LocoAddr-multi example (one LocoAddrReader instance per
  detector) and LocoNet-Test (LbServer + transponding): whenever the
  locomotive in a block changes, the Pico sends "old locomotive absent"
  followed by "new locomotive present" messages (Digitrax OPC_MULTI_SENSE)
  to Rocrail. Rocrail extracts the locomotive ID from these and assigns it
  to the block.

  Detector 1 corresponds to block 1, detector 2 to block 2, etc.

  Organization:
  LbServer     (LbServer.h/.cpp)    : network handling (connection, lines, keepalive)
  Transponder  (Transponder.h/.cpp) : block management and LocoNet messaging
  This file                         : detectors, Wi-Fi, and the integration between them

  Arduino core: "Raspberry Pi Pico/RP2040/RP2350" by Earle Philhower.
  Select "Raspberry Pi Pico W" (or "Pico 2 W") in the IDE.

  Wiring common to all detectors (v3.1 boards):
  J19  5V µC  -> VBUS (pin 40)       (each 6N137 consumes 10–15 mA)
  J19  GND µC -> GND
  J22  VIO    -> 3V3(OUT) (pin 36)   (NEVER 5V)
  Wiring specific to each detector 'n':
  J19  OUT   ​​ -> RX_PINS[n]
  GP23 through GP25 and GP29 are used for Wi-Fi; do not use them. Rocrail settings:
  - LocoNet controller (type: LbServer), Hostname = IP address of the
  Pico (displayed at startup), Port 1234, Interface ID "LN";
  - one sensor per block: Interface ID "LN", Node ID 0, Address = block
  number, type: "Railcom";
  - in each block, under the "Routes" tab: the sensor with the "ident"
  event;
  - "Code" field for each locomotive = its DCC address.


   © Christophe BOBILLE - Locoduino - GPL-3.0
   https://github.com/BOBILLEChristophe/DCC-LocoAddr
*/

#if !defined(ARDUINO_ARCH_RP2040)
#error "Programme pour Raspberry Pi Pico W / Pico 2 W : pour un ESP32, ouvrir LocoAddr-LocoNet"
#endif

#include <WiFi.h>
#include <DCC_LocoAddr.h>
#include "LbServer.h"
#include "Transponder.h"

// =============================================================================
// RÉGLAGES
// =============================================================================

const char *WIFI_SSID = "mon_reseau";
const char *WIFI_PASSWORD = "mon_mot_de_passe";

constexpr uint16_t LBSERVER_PORT = 1234; // port habituel d'un LbServer

// Adresse IP fixe (sinon : adresse donnée par la box, affichée au démarrage).
// Autre solution : réserver l'adresse du Pico dans la box (bail DHCP).
#define USE_STATIC_IP false
const IPAddress STATIC_IP(192, 168, 1, 15);
const IPAddress GATEWAY(192, 168, 1, 1);
const IPAddress SUBNET(255, 255, 255, 0);

// true : affiche les détecteurs, les messages envoyés et les échanges avec Rocrail
#define VERBOSE true

// Nombre de détecteurs câblés
constexpr uint8_t NB_DETECTORS = 4;

// Broches OUT (J19), mêmes que l'exemple LocoAddr-multi : GP1 et GP5 sont les
// RX des UART0 et UART1, les autres passent par les PIO.
constexpr uint8_t RX_PINS[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 26, 27, 28};

// =============================================================================

#define LOG(...) do { if (VERBOSE) Serial.printf(__VA_ARGS__); } while (0)

static_assert(NB_DETECTORS >= 1 && NB_DETECTORS <= LocoAddrReader::MAX_READERS,
              "NB_DETECTORS : trop de détecteurs pour ce microcontrôleur");
static_assert(NB_DETECTORS <= sizeof(RX_PINS) / sizeof(RX_PINS[0]), "RX_PINS : pas assez de broches");

LocoAddrReader *detectors[NB_DETECTORS];

// --- Liaison avec Rocrail -------------------------------------------------------
LbServer lbServer(LBSERVER_PORT);
Transponder transponder(lbServer, NB_DETECTORS); // un canton par détecteur

// Rocrail a pu démarrer (ou se reconnecter) après le Pico : on lui renvoie l'état
void onRocrailConnect()
{
  transponder.resendAll();
}

// --- Wi-Fi ------------------------------------------------------------------------
void connectWifi()
{
  WiFi.mode(WIFI_STA);
#if USE_STATIC_IP
  // Attention : sur Pico W, l'ordre est IP, DNS, passerelle, masque
  WiFi.config(STATIC_IP, GATEWAY, GATEWAY, SUBNET);
#endif
  WiFi.noLowPowerMode(); // pas de mise en veille de la puce Wi-Fi (réponses plus rapides)

  LOG("Connexion Wi-Fi a \"%s\"", WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD); // reconnexion automatique gérée par le cœur
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    LOG(".");
  }
  LOG("\nConnecte : %s\n", WiFi.localIP().toString().c_str());
}

// =============================================================================

void setup()
{
  Serial.begin(115200);
  const uint32_t t0 = millis();
  while (!Serial && (millis() - t0 < 3000))
  {
    delay(10); // laisse le temps d'ouvrir le moniteur série (USB)
  }
  LOG("\nLocoAddr-LocoNet-Pico v%s - %u detecteur(s), %u au maximum\n",
      DCC_LOCOADDR_VERSION, (unsigned)NB_DETECTORS, (unsigned)LocoAddrReader::MAX_READERS);

  // Détecteurs d'abord : ils fonctionnent même si le Wi-Fi tarde
  for (uint8_t i = 0; i < NB_DETECTORS; ++i)
  {
    detectors[i] = new LocoAddrReader(RX_PINS[i]);
    if (detectors[i]->begin())
    {
      LOG("Detecteur %u (canton %u) : RX GP%u via %s\n", i + 1, i + 1, RX_PINS[i],
          detectors[i]->transportName());
    }
    else
    {
      LOG("ERREUR : detecteur %u (GP%u) impossible a demarrer\n", i + 1, RX_PINS[i]);
    }
  }

  connectWifi();

  lbServer.setVerbose(VERBOSE);
  transponder.setVerbose(VERBOSE);
  lbServer.onConnect(onRocrailConnect);
  lbServer.begin();
  LOG("Serveur LbServer pret : dans Rocrail, Hostname = %s  Port = %u\n",
      WiFi.localIP().toString().c_str(), LBSERVER_PORT);
}

void loop()
{
  lbServer.update(); // connexions et messages de Rocrail

  // Détecteur i -> canton i + 1. Le Transponder n'envoie rien si rien ne change.
  // La bibliothèque confirme déjà l'adresse (5 lectures identiques) et ne
  // déclare la loco partie qu'après 1 s sans réponse : pas de filtrage à ajouter.
  for (uint8_t i = 0; i < NB_DETECTORS; ++i)
  {
    transponder.setCanton(i + 1, detectors[i]->address());
  }

  delay(10);
}
