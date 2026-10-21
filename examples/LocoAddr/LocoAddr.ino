/*
   LocoAddr - programme de base
   DCC-LocoAddr v5.0.0

   Lit un détecteur Locoduino LocoAddr (carte v3.1) et affiche sur le moniteur
   série (115200 bauds) l'adresse de la locomotive et, si l'option est montée,
   son sens de pose.

   Câblage (carte v3.1) :
     J19  5V µC  -> 5V (VIN) de l'ESP32   (alimente le 6N137, 5 V obligatoire)
     J19  OUT    -> DETECTOR_RX_PIN        (GPIO4)
     J19  GND µC -> GND de l'ESP32
     J22  VIO    -> 3V3 de l'ESP32        (JAMAIS 5 V avec un ESP32)
     J22  DIR    -> DETECTOR_DIR_PIN       (option sens de pose, sinon non câblé)
                    GPIO35 sur ESP32 classique, GPIO5 sur S2, S3, C3, C6, H2

   Fonctionne sur ESP32, ESP32-S2, ESP32-S3, ESP32-C3, ESP32-C6 et ESP32-H2.

   © Christophe BOBILLE - Locoduino - GPL-3.0
   https://github.com/BOBILLEChristophe/DCC-LocoAddr
*/

#include <DCC_LocoAddr.h>

// =============================================================================
// RÉGLAGES
// =============================================================================

// --- Niveau des messages sur le moniteur série --------------------------------
//   LOG_NONE    : aucun message
//   LOG_ERROR   : erreurs seulement (initialisation, réception UART)
//   LOG_INFO    : + arrivée / départ des locomotives et sens de pose  (défaut)
//   LOG_VERBOSE : + configuration au démarrage et statistiques toutes les 10 s
//   LOG_DEBUG   : + contenu brut de chaque cutout (diagnostic du câblage)
#define LOG_LEVEL LOG_INFO

// --- Option "sens de pose" ----------------------------------------------------
// true seulement si l'option est montée sur la carte et DIR câblé :
// une entrée DIR en l'air donnerait un sens au hasard.
#define WITH_DIRECTION false

// Si "normal" et "inverse" sont permutés sur votre réseau, passer à true.
#define INVERT_DIRECTION false

// --- Broches -------------------------------------------------------------------
// Choisies hors des broches lues au démarrage et hors USB / flash.
constexpr gpio_num_t DETECTOR_RX_PIN = GPIO_NUM_4;
#if CONFIG_IDF_TARGET_ESP32
constexpr gpio_num_t DIR_GPIO = GPIO_NUM_35; // entrée seule, sans tirage interne
#else
constexpr gpio_num_t DIR_GPIO = GPIO_NUM_5;
#endif

// --- Intervalle des statistiques (niveau VERBOSE) --------------------------------
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

constexpr gpio_num_t DETECTOR_DIR_PIN = WITH_DIRECTION ? DIR_GPIO : GPIO_NUM_NC;

LocoAddrReader detector(UART_NUM_1, DETECTOR_RX_PIN, DETECTOR_DIR_PIN);

// Niveau DEBUG : appelé depuis la tâche de lecture pour chaque cutout.
void printBurst(const uint8_t *data, size_t len, void *)
{
  char line[8 + 3 * 16 + 2];
  int n = snprintf(line, sizeof(line), "Cutout :");
  for (size_t i = 0; (i < len) && (n < (int)sizeof(line) - 4); ++i)
  {
    n += snprintf(line + n, sizeof(line) - n, " %02X", data[i]);
  }
  Serial.println(line);
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
  delay(200);

  LOG_I("\nLocoAddr v%s\n", DCC_LOCOADDR_VERSION);
  LOG_V("RX GPIO%d, DIR ", (int)DETECTOR_RX_PIN);
  if (WITH_DIRECTION)
  {
    LOG_V("GPIO%d%s\n", (int)DETECTOR_DIR_PIN, INVERT_DIRECTION ? " (inverse)" : "");
  }
  else
  {
    LOG_V("non utilise\n");
  }

  detector.setDirectionInverted(INVERT_DIRECTION);
#if LOG_LEVEL >= LOG_DEBUG
  detector.setBurstCallback(printBurst);
#endif

  if (!detector.begin())
  {
    LOG_E("ERREUR : initialisation du détecteur impossible (UART ou broches).\n");
    while (true)
    {
      delay(1000);
    }
  }

  LOG_I("En attente d'une locomotive...\n");
}

void loop()
{
  static uint16_t lastAddress = 0;
  static LocoDirection lastDirection = LocoDirection::Unknown;
  static uint32_t lastStatsMs = 0;
  static uint32_t lastErrorCheckMs = 0;
  static uint32_t lastUartErrors = 0;

  // --- Arrivée, départ, changement de sens ------------------------------------
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

  // --- Erreurs de réception : signalées au plus une fois par seconde ----------
  if (millis() - lastErrorCheckMs >= 1000)
  {
    lastErrorCheckMs = millis();
    const LocoAddrStats s = detector.stats();
    const uint32_t uartErrors = s.uartOverflows + s.frameErrors;
    if (uartErrors != lastUartErrors)
    {
      LOG_E("ERREUR : %lu erreur(s) de reception UART (debordements=%lu, trame=%lu)\n",
            (unsigned long)(uartErrors - lastUartErrors),
            (unsigned long)s.uartOverflows, (unsigned long)s.frameErrors);
      lastUartErrors = uartErrors;
    }
  }

  // --- Statistiques ------------------------------------------------------------
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
