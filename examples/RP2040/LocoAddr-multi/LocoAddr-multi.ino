/*
   LocoAddr-multi - plusieurs détecteurs sur un même Raspberry Pi Pico
   DCC-LocoAddr v5.1.0

   Jusqu'à 10 détecteurs sur un Pico (RP2040), 14 sur un Pico 2 (RP2350),
   un de moins sur les modèles W (une machine d'état PIO sert au Wi-Fi).
   Les deux premiers utilisent les UART matériels, les suivants des
   récepteurs PIO ; le programme affiche la ressource prise par chacun.

   Cœur Arduino : "Raspberry Pi Pico/RP2040/RP2350" d'Earle Philhower.

   Câblage commun à tous les détecteurs (cartes v3.1) :
     J19  5V µC  -> VBUS (broche 40)       chaque 6N137 consomme 10 à 15 mA
     J19  GND µC -> GND
     J22  VIO    -> 3V3(OUT) (broche 36)   (JAMAIS 5 V)
   Câblage propre à chaque détecteur n :
     J19  OUT    -> RX_PINS[n]
     J22  DIR    -> DIR_PINS[n]            (seulement avec l'option sens de pose)

   Broches libres sur un Pico : GP0 à GP22 et GP26 à GP28 (GP23 à GP25 et GP29
   servent à la carte elle-même).

   © Christophe BOBILLE - Locoduino - GPL-3.0
   https://github.com/BOBILLEChristophe/DCC-LocoAddr
*/

#ifndef ARDUINO_ARCH_RP2040
#error "Exemple pour Raspberry Pi Pico : pour un ESP32, ouvrir l'exemple ESP32/LocoAddr-multi"
#endif

#include <DCC_LocoAddr.h>

// =============================================================================
// RÉGLAGES
// =============================================================================

//   LOG_NONE, LOG_ERROR, LOG_INFO (défaut), LOG_VERBOSE (+ statistiques)
#define LOG_LEVEL LOG_INFO

// Nombre de détecteurs câblés (1 à LocoAddrReader::MAX_READERS)
constexpr uint8_t NB_DETECTORS = 10;

// Option "sens de pose" montée sur toutes les cartes et DIR câblés ?
#define WITH_DIRECTION false
#define INVERT_DIRECTION false

// Broches OUT (J19) : GP1 et GP5 sont les RX des UART0 et UART1, les autres
// passent par les PIO. Les 4 dernières ne servent que sur un Pico 2.
constexpr uint8_t RX_PINS[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 26, 27, 28, 0};

// Broches DIR (J22), dans le même ordre que RX_PINS
constexpr int DIR_PINS[] = {11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22};

constexpr uint32_t STATS_PERIOD_MS = 10000;

// =============================================================================

#define LOG_NONE 0
#define LOG_ERROR 1
#define LOG_INFO 2
#define LOG_VERBOSE 3

#define LOG_E(...) do { if (LOG_LEVEL >= LOG_ERROR)   Serial.printf(__VA_ARGS__); } while (0)
#define LOG_I(...) do { if (LOG_LEVEL >= LOG_INFO)    Serial.printf(__VA_ARGS__); } while (0)
#define LOG_V(...) do { if (LOG_LEVEL >= LOG_VERBOSE) Serial.printf(__VA_ARGS__); } while (0)

static_assert(NB_DETECTORS >= 1 && NB_DETECTORS <= LocoAddrReader::MAX_READERS,
              "NB_DETECTORS : trop de détecteurs pour ce microcontrôleur");
static_assert(NB_DETECTORS <= sizeof(RX_PINS) / sizeof(RX_PINS[0]), "RX_PINS : pas assez de broches");
static_assert(!WITH_DIRECTION || (NB_DETECTORS <= sizeof(DIR_PINS) / sizeof(DIR_PINS[0])),
              "DIR_PINS : pas assez de broches");

LocoAddrReader *detectors[NB_DETECTORS];

void setup()
{
  Serial.begin(115200);
  const uint32_t t0 = millis();
  while (!Serial && (millis() - t0 < 3000))
  {
    delay(10);
  }

  LOG_I("\nLocoAddr-multi v%s (Pico) - %u detecteur(s), %u au maximum\n",
        DCC_LOCOADDR_VERSION, (unsigned)NB_DETECTORS, (unsigned)LocoAddrReader::MAX_READERS);

  for (uint8_t i = 0; i < NB_DETECTORS; ++i)
  {
    const int dir = WITH_DIRECTION ? DIR_PINS[i] : LocoAddrReader::NO_PIN;
    detectors[i] = new LocoAddrReader(RX_PINS[i], dir);
    detectors[i]->setDirectionInverted(INVERT_DIRECTION);

    if (detectors[i]->begin())
    {
      LOG_I("Detecteur %u : RX GP%u via %s", i + 1, RX_PINS[i], detectors[i]->transportName());
      if (WITH_DIRECTION)
      {
        LOG_I(", DIR GP%d", dir);
      }
      LOG_I("\n");
    }
    else
    {
      LOG_E("ERREUR : detecteur %u (GP%u) impossible a demarrer\n", i + 1, RX_PINS[i]);
    }
  }

  LOG_I("En attente des locomotives...\n");
}

void loop()
{
  static uint16_t lastAddress[NB_DETECTORS] = {};
  static LocoDirection lastDirection[NB_DETECTORS] = {};
  static uint32_t lastErrors[NB_DETECTORS] = {};
  static uint32_t lastStatsMs = 0;
  static uint32_t lastErrorCheckMs = 0;

  for (uint8_t i = 0; i < NB_DETECTORS; ++i)
  {
    const uint16_t address = detectors[i]->address();
    const LocoDirection direction = detectors[i]->direction();

    if ((address != lastAddress[i]) || (direction != lastDirection[i]))
    {
      if (address == 0)
      {
        LOG_I("Canton %u : locomotive %u partie\n", i + 1, lastAddress[i]);
      }
      else if (WITH_DIRECTION)
      {
        LOG_I("Canton %u : locomotive %u, sens de pose : %s\n", i + 1, address,
              LocoAddrReader::directionName(direction));
      }
      else if (address != lastAddress[i])
      {
        LOG_I("Canton %u : locomotive %u\n", i + 1, address);
      }
      lastAddress[i] = address;
      lastDirection[i] = direction;
    }
  }

  if (millis() - lastErrorCheckMs >= 1000)
  {
    lastErrorCheckMs = millis();
    for (uint8_t i = 0; i < NB_DETECTORS; ++i)
    {
      const LocoAddrStats s = detectors[i]->stats();
      const uint32_t errors = s.uartOverflows + s.frameErrors;
      if (errors != lastErrors[i])
      {
        LOG_E("ERREUR canton %u : %lu erreur(s) de reception\n", i + 1,
              (unsigned long)(errors - lastErrors[i]));
        lastErrors[i] = errors;
      }
    }
  }

#if LOG_LEVEL >= LOG_VERBOSE
  if (millis() - lastStatsMs >= STATS_PERIOD_MS)
  {
    lastStatsMs = millis();
    for (uint8_t i = 0; i < NB_DETECTORS; ++i)
    {
      const LocoAddrStats s = detectors[i]->stats();
      LOG_V("[canton %u %s] cutouts=%lu adr=%lu derniere_lue=%u rejetes=%lu debordements=%lu\n",
            i + 1, detectors[i]->transportName(), (unsigned long)s.bursts,
            (unsigned long)s.ch1Datagrams, s.lastAddressRead,
            (unsigned long)s.rejectedBursts, (unsigned long)s.uartOverflows);
    }
  }
#else
  (void)lastStatsMs;
#endif

  delay(10);
}
