/*
   LocoAddr-Display v5.0.0
   Afficheur 4 chiffres 7 segments de l'adresse DCC d'une locomotive.

   Nécessite la bibliothèque DCC-LocoAddr, installée depuis Arduino Library Manager
   ou directement depuis le dépôt GitHub.

   Matériel :
     - ESP32-WROOM-32 (ESP32 classique) ;
     - détecteur Locoduino LocoAddr, carte v3.1 ;
     - afficheur LED 7 segments 4 chiffres LFD039AUE-102A ;
     - 4 transistors 2N2222A (sélection des chiffres), R1 à R4 = 1 kΩ ;
     - R5 à R11 = 68 Ω (segments).

   Liaison avec le détecteur (carte v3.1) :
     CN1-1 +5V  -> J19 « 5V µC »
     CN1-2 RX   <- J19 « OUT »      (GPIO4)
     CN1-3 GND  -> J19 « GND µC »
     3V3 de l'ESP32 -> J22 « VIO »  (fil séparé : CN1 n'a pas de 3V3)

   Affichage : l'adresse sans zéros à gauche ; afficheur éteint sans locomotive.

   © Christophe BOBILLE - Locoduino - GPL-3.0
   https://github.com/BOBILLEChristophe/DCC-LocoAddr
*/

#include <DCC-LocoAddr.h>

#if !CONFIG_IDF_TARGET_ESP32
#error "LocoAddr-Display : le circuit imprime est prevu pour un ESP32-WROOM-32 (ESP32 classique)"
#endif

// =============================================================================
// RÉGLAGES
// =============================================================================

// Niveau des messages sur le moniteur série (115200 bauds) :
//   LOG_NONE, LOG_ERROR, LOG_INFO (défaut), LOG_VERBOSE (+ statistiques),
//   LOG_DEBUG (+ contenu brut de chaque cutout)
#define LOG_LEVEL LOG_INFO

// Durée d'allumage de chaque chiffre. 2 ms -> rafraîchissement à 125 Hz,
// sans scintillement. (La version 4.7 utilisait 5 ms, soit 50 Hz.)
constexpr uint32_t DIGIT_ON_MS = 2;

constexpr uint32_t STATS_PERIOD_MS = 10000;

// =============================================================================
// BROCHAGE DU CIRCUIT IMPRIMÉ (ne pas modifier sans modifier la carte)
// =============================================================================

constexpr gpio_num_t DETECTOR_RX_PIN = GPIO_NUM_4;

constexpr uint8_t NUM_DIGITS = 4;
constexpr uint8_t NUM_SEG = 7;

// Chiffres : unités, dizaines, centaines, milliers. HIGH = chiffre allumé.
// GPIO12 est lue au démarrage (tension de la flash) : la base du 2N2222A la
// maintient au niveau bas, ce qui convient. Ne jamais y ajouter de tirage haut.
constexpr uint8_t DIGIT_PINS[NUM_DIGITS] = {26, 25, 12, 14};

// Segments, bit 0 à bit 6 : a, f, g, c, d, e, b. HIGH = segment allumé.
constexpr uint8_t SEG_PINS[NUM_SEG] = {13, 27, 18, 19, 21, 22, 23};

constexpr uint8_t SEG_MAP[10] = {
  0b1111011, // 0
  0b1001000, // 1
  0b1110101, // 2
  0b1011101, // 3
  0b1001110, // 4
  0b0011111, // 5
  0b0111111, // 6
  0b1001001, // 7
  0b1111111, // 8
  0b1011111  // 9
};
constexpr uint8_t SEG_DASH = 0b0000100; // segment g seul (sécurité, adresse > 9999)

// =============================================================================

#define LOG_NONE 0
#define LOG_ERROR 1
#define LOG_INFO 2
#define LOG_VERBOSE 3
#define LOG_DEBUG 4

#define LOG_E(...) do { if (LOG_LEVEL >= LOG_ERROR)   Serial.printf(__VA_ARGS__); } while (0)
#define LOG_I(...) do { if (LOG_LEVEL >= LOG_INFO)    Serial.printf(__VA_ARGS__); } while (0)
#define LOG_V(...) do { if (LOG_LEVEL >= LOG_VERBOSE) Serial.printf(__VA_ARGS__); } while (0)

LocoAddrReader detector(UART_NUM_1, DETECTOR_RX_PIN);

// -----------------------------------------------------------------------------
// Afficheur
// -----------------------------------------------------------------------------

void allDigitsOff()
{
  for (uint8_t pin : DIGIT_PINS)
  {
    digitalWrite(pin, LOW);
  }
}

void setSegments(uint8_t pattern)
{
  for (uint8_t i = 0; i < NUM_SEG; ++i)
  {
    digitalWrite(SEG_PINS[i], (pattern >> i) & 1 ? HIGH : LOW);
  }
}

// Calcule les motifs des 4 chiffres (index 0 = unités). 0 = chiffre éteint.
void buildPatterns(uint16_t address, uint8_t patterns[NUM_DIGITS])
{
  for (uint8_t j = 0; j < NUM_DIGITS; ++j)
  {
    patterns[j] = 0;
  }

  if (address == 0)
  {
    return;
  }

  if (address > 9999)
  {
    for (uint8_t j = 0; j < NUM_DIGITS; ++j)
    {
      patterns[j] = SEG_DASH;
    }
    return;
  }

  for (uint8_t j = 0; j < NUM_DIGITS; ++j)
  {
    patterns[j] = SEG_MAP[address % 10];
    address /= 10;
    if (address == 0)
    {
      break; // pas de zéros à gauche
    }
  }
}

// Tâche de multiplexage, sur le cœur 0 (la tâche de lecture n'est pas épinglée).
void displayTask(void *)
{
  uint8_t patterns[NUM_DIGITS];
  uint16_t shown = 0xFFFF;
  TickType_t wake = xTaskGetTickCount();

  for (;;)
  {
    const uint16_t address = detector.address();
    if (address != shown)
    {
      shown = address;
      buildPatterns(address, patterns);
    }

    if (address == 0)
    {
      allDigitsOff();
      setSegments(0);
      vTaskDelay(pdMS_TO_TICKS(20));
      wake = xTaskGetTickCount();
      continue;
    }

    for (uint8_t j = 0; j < NUM_DIGITS; ++j)
    {
      allDigitsOff();            // éteindre avant de changer les segments
      setSegments(patterns[j]);
      if (patterns[j] != 0)
      {
        digitalWrite(DIGIT_PINS[j], HIGH);
      }
      vTaskDelayUntil(&wake, pdMS_TO_TICKS(DIGIT_ON_MS));
    }
  }
}

// -----------------------------------------------------------------------------

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

void setup()
{
  Serial.begin(115200);
  delay(200);

  for (uint8_t pin : DIGIT_PINS)
  {
    pinMode(pin, OUTPUT);
    digitalWrite(pin, LOW);
  }
  for (uint8_t pin : SEG_PINS)
  {
    pinMode(pin, OUTPUT);
    digitalWrite(pin, LOW);
  }

  LOG_I("\nLocoAddr-Display v%s\n", DCC_LOCOADDR_VERSION);
  LOG_V("RX GPIO%d\n", (int)DETECTOR_RX_PIN);

#if LOG_LEVEL >= LOG_DEBUG
  detector.setBurstCallback(printBurst);
#endif

  if (!detector.begin())
  {
    LOG_E("ERREUR : initialisation du détecteur impossible.\n");
    while (true)
    {
      delay(1000);
    }
  }

  xTaskCreatePinnedToCore(displayTask, "Display", 2048, nullptr, 5, nullptr, 0);

  LOG_I("En attente d'une locomotive...\n");
}

void loop()
{
  static uint16_t lastAddress = 0;
  static uint32_t lastStatsMs = 0;
  static uint32_t lastErrorCheckMs = 0;
  static uint32_t lastUartErrors = 0;

  const uint16_t address = detector.address();
  if (address != lastAddress)
  {
    if (address == 0)
    {
      LOG_I("Locomotive %u partie\n", lastAddress);
    }
    else
    {
      LOG_I("Locomotive %u\n", address);
    }
    lastAddress = address;
  }

  if (millis() - lastErrorCheckMs >= 1000)
  {
    lastErrorCheckMs = millis();
    const LocoAddrStats s = detector.stats();
    const uint32_t uartErrors = s.uartOverflows + s.frameErrors;
    if (uartErrors != lastUartErrors)
    {
      LOG_E("ERREUR : %lu erreur(s) de reception UART\n", (unsigned long)(uartErrors - lastUartErrors));
      lastUartErrors = uartErrors;
    }
  }

#if LOG_LEVEL >= LOG_VERBOSE
  if (millis() - lastStatsMs >= STATS_PERIOD_MS)
  {
    lastStatsMs = millis();
    const LocoAddrStats s = detector.stats();
    Serial.printf("[stats] cutouts=%lu adr=%lu derniere_lue=%u rejetes=%lu invalides=%lu\n",
                  (unsigned long)s.bursts, (unsigned long)s.ch1Datagrams, s.lastAddressRead,
                  (unsigned long)s.rejectedBursts, (unsigned long)s.invalidAddresses);
  }
#else
  (void)lastStatsMs;
#endif

  delay(10);
}
