/*
   LocoAddr - programme de base pour Raspberry Pi Pico (RP2040 / RP2350)
   DCC-LocoAddr v5.1.0

   Lit un détecteur Locoduino LocoAddr (carte v3.1) et affiche sur le moniteur
   série l'adresse de la locomotive et, si l'option est montée, son sens de pose.

   Cœur Arduino : "Raspberry Pi Pico/RP2040/RP2350" d'Earle Philhower.

   Câblage (carte v3.1) :
     J19  5V µC  -> VBUS (broche 40) : 5 V de l'USB, alimente le 6N137
     J19  OUT    -> DETECTOR_RX_PIN   (GP1, broche 2)
     J19  GND µC -> GND (broche 3, 38...)
     J22  VIO    -> 3V3(OUT) (broche 36)   (JAMAIS 5 V : le Pico est en 3,3 V)
     J22  DIR    -> DETECTOR_DIR_PIN  (GP11, broche 15), seulement avec l'option

   GP1 est une broche RX de l'UART0 : le détecteur utilise l'UART matériel.
   N'importe quelle autre broche fonctionne aussi : la bibliothèque prend alors
   un récepteur PIO (transportName() indique lequel).

   © Christophe BOBILLE - Locoduino - GPL-3.0
   https://github.com/BOBILLEChristophe/DCC-LocoAddr
*/

#ifndef ARDUINO_ARCH_RP2040
#error "Exemple pour Raspberry Pi Pico : pour un ESP32, ouvrir l'exemple ESP32/LocoAddr"
#endif

#include <DCC_LocoAddr.h>

// =============================================================================
// RÉGLAGES
// =============================================================================

// --- Niveau des messages sur le moniteur série --------------------------------
//   LOG_NONE    : aucun message
//   LOG_ERROR   : erreurs seulement
//   LOG_INFO    : + arrivée / départ des locomotives et sens de pose  (défaut)
//   LOG_VERBOSE : + configuration au démarrage et statistiques toutes les 10 s
//   LOG_DEBUG   : + contenu brut de chaque cutout (diagnostic du câblage)
#define LOG_LEVEL LOG_INFO

// --- Option "sens de pose" ----------------------------------------------------
#define WITH_DIRECTION false
#define INVERT_DIRECTION false

// --- Broches (numéros GPn) ------------------------------------------------------
constexpr uint8_t DETECTOR_RX_PIN = 1; // GP1 = UART0 RX
constexpr int DIR_GPIO = 11;           // GP11, comme le détecteur 1 de LocoAddr-multi

constexpr uint32_t STATS_PERIOD_MS = 10000;

// =============================================================================

#define LOG_NONE 0
#define LOG_ERROR 1
#define LOG_INFO 2
#define LOG_VERBOSE 3
#define LOG_DEBUG 4

#define LOG_E(...) do { if (LOG_LEVEL >= LOG_ERROR)   Serial.printf(__VA_ARGS__); } while (0)
#define LOG_I(...) do { if (LOG_LEVEL >= LOG_INFO)    Serial.printf(__VA_ARGS__); } while (0)
#define LOG_V(...) do { if (LOG_LEVEL >= LOG_VERBOSE) Serial.printf(__VA_ARGS__); } while (0)

constexpr int DETECTOR_DIR_PIN = WITH_DIRECTION ? DIR_GPIO : LocoAddrReader::NO_PIN;

LocoAddrReader detector(DETECTOR_RX_PIN, DETECTOR_DIR_PIN);

// --- Niveau DEBUG -----------------------------------------------------------------
// Sur le Pico, la fonction de rappel s'exécute sous interruption : elle se
// contente de copier le cutout dans une petite file, que loop() affiche.
struct RawBurst
{
  uint8_t len;
  uint8_t data[16];
};
constexpr uint8_t RAW_QUEUE_SIZE = 16;
volatile RawBurst rawQueue[RAW_QUEUE_SIZE];
volatile uint8_t rawHead = 0; // écrit sous interruption
volatile uint8_t rawTail = 0; // écrit par loop()

void storeBurst(const uint8_t *data, size_t len, void *)
{
  const uint8_t next = (rawHead + 1) % RAW_QUEUE_SIZE;
  if (next == rawTail)
  {
    return; // file pleine : cutout non affiché
  }
  volatile RawBurst &slot = rawQueue[rawHead];
  slot.len = (len > sizeof(slot.data)) ? sizeof(slot.data) : len;
  for (uint8_t i = 0; i < slot.len; ++i)
  {
    slot.data[i] = data[i];
  }
  rawHead = next;
}

void printRawBursts()
{
  while (rawTail != rawHead)
  {
    volatile RawBurst &slot = rawQueue[rawTail];
    Serial.print("Cutout :");
    for (uint8_t i = 0; i < slot.len; ++i)
    {
      Serial.printf(" %02X", slot.data[i]);
    }
    Serial.println();
    rawTail = (rawTail + 1) % RAW_QUEUE_SIZE;
  }
}

void printStats(const LocoAddrStats &s)
{
  LOG_V("[stats] cutouts=%lu adr=%lu (ADR1=%lu ADR2=%lu) derniere_lue=%u rejetes=%lu "
        "invalides=%lu trop_longs=%lu debordements=%lu erreurs_trame=%lu\n",
        (unsigned long)s.bursts, (unsigned long)s.ch1Datagrams,
        (unsigned long)s.adr1Count, (unsigned long)s.adr2Count, s.lastAddressRead,
        (unsigned long)s.rejectedBursts, (unsigned long)s.invalidAddresses,
        (unsigned long)s.oversizedBursts, (unsigned long)s.uartOverflows,
        (unsigned long)s.frameErrors);
}

void setup()
{
  Serial.begin(115200);
  const uint32_t t0 = millis();
  while (!Serial && (millis() - t0 < 3000))
  {
    delay(10); // laisse le temps d'ouvrir le moniteur série (USB)
  }

  LOG_I("\nLocoAddr v%s (Pico)\n", DCC_LOCOADDR_VERSION);

  detector.setDirectionInverted(INVERT_DIRECTION);
#if LOG_LEVEL >= LOG_DEBUG
  detector.setBurstCallback(storeBurst);
#endif

  if (!detector.begin())
  {
    LOG_E("ERREUR : initialisation du détecteur impossible (broche ou ressources).\n");
    while (true)
    {
      delay(1000);
    }
  }

  LOG_V("RX GP%u via %s, DIR %s\n", DETECTOR_RX_PIN, detector.transportName(),
        WITH_DIRECTION ? "GP11" : "non utilise");
  LOG_I("En attente d'une locomotive...\n");
}

void loop()
{
  static uint16_t lastAddress = 0;
  static LocoDirection lastDirection = LocoDirection::Unknown;
  static uint32_t lastStatsMs = 0;
  static uint32_t lastErrorCheckMs = 0;
  static uint32_t lastUartErrors = 0;

#if LOG_LEVEL >= LOG_DEBUG
  printRawBursts();
#endif

  const uint16_t address = detector.address();
  const LocoDirection direction = detector.direction();

  if ((address != lastAddress) || (direction != lastDirection))
  {
    if (address == 0)
    {
      LOG_I("Locomotive %u partie\n", lastAddress);
    }
    else if (WITH_DIRECTION)
    {
      LOG_I("Locomotive %u, sens de pose : %s\n", address, LocoAddrReader::directionName(direction));
    }
    else if (address != lastAddress)
    {
      LOG_I("Locomotive %u\n", address);
    }
    lastAddress = address;
    lastDirection = direction;
  }

  if (millis() - lastErrorCheckMs >= 1000)
  {
    lastErrorCheckMs = millis();
    const LocoAddrStats s = detector.stats();
    const uint32_t uartErrors = s.uartOverflows + s.frameErrors;
    if (uartErrors != lastUartErrors)
    {
      LOG_E("ERREUR : %lu erreur(s) de reception (debordements=%lu, trame=%lu)\n",
            (unsigned long)(uartErrors - lastUartErrors),
            (unsigned long)s.uartOverflows, (unsigned long)s.frameErrors);
      lastUartErrors = uartErrors;
    }
  }

#if LOG_LEVEL >= LOG_VERBOSE
  if (millis() - lastStatsMs >= STATS_PERIOD_MS)
  {
    lastStatsMs = millis();
    printStats(detector.stats());
  }
#else
  (void)lastStatsMs;
#endif

  delay(10);
}
