/*
   LocoAddr-Display-min v5.0.0
   Afficheur réduit de l'adresse DCC d'une locomotive :
   ESP32-C3 SuperMini + module afficheur TM1637 (4 chiffres).

   Nécessite :
     - la bibliothèque DCC-LocoAddr, installée depuis Arduino Library Manager
       ou directement depuis le dépôt GitHub ;
     - la bibliothèque TM1637Display d'Avishay Orpaz (gestionnaire de
       bibliothèques de l'IDE Arduino : « TM1637 »).

   Réglages de l'IDE Arduino :
     - carte « ESP32C3 Dev Module » (ou « Nologo ESP32C3 Super Mini ») ;
     - « USB CDC On Boot » : Enabled, pour voir les messages sur le moniteur
       série par la prise USB-C.

   Câblage :
     Détecteur v3.1  J19 « 5V µC »  -> 5V du C3
                     J19 « OUT »    -> GPIO10 (DETECTOR_RX_PIN)
                     J19 « GND µC » -> GND
                     J22 « VIO »    -> 3V3 du C3 (JAMAIS 5 V)
     Module TM1637   VCC -> 5V, GND -> GND
                     CLK -> GPIO2 et DIO -> GPIO3 À TRAVERS un adaptateur de
                     niveau 3,3 V / 5 V (2 x BSS138), voir le README.
                     ATTENTION : jamais CLK/DIO d'un module en 5 V directement
                     sur le C3 (5 V sur ses broches). Essai sans adaptateur :
                     module en 3V3, LED rouges uniquement, hors fiche technique.

   Affichage : l'adresse de la locomotive (1 à 9999), afficheur éteint sans
   locomotive (ou "----" avec SHOW_DASHES_WHEN_EMPTY).

   © Christophe BOBILLE - Locoduino - GPL-3.0
   https://github.com/BOBILLEChristophe/DCC-LocoAddr
*/

#include <DCC-LocoAddr.h>
#include <TM1637Display.h>

// =============================================================================
// RÉGLAGES
// =============================================================================

// Niveau des messages sur le moniteur série :
//   LOG_NONE, LOG_ERROR, LOG_INFO (défaut), LOG_VERBOSE (+ statistiques),
//   LOG_DEBUG (+ contenu brut de chaque cutout)
#define LOG_LEVEL LOG_INFO

// Luminosité de l'afficheur, de 0 (faible) à 7 (maximale)
constexpr uint8_t BRIGHTNESS = 7;

// Sans locomotive : false = afficheur éteint, true = "----"
constexpr bool SHOW_DASHES_WHEN_EMPTY = false;

// Broches du circuit imprimé Mini Display v3.0
constexpr gpio_num_t DETECTOR_RX_PIN = GPIO_NUM_10;
constexpr uint8_t TM1637_CLK_PIN = 2;   // GPIO2 : lue au démarrage, maintenue haute par le Pololu
constexpr uint8_t TM1637_DIO_PIN = 3;

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

LocoAddrReader detector(UART_NUM_1, DETECTOR_RX_PIN);
TM1637Display display(TM1637_CLK_PIN, TM1637_DIO_PIN);

constexpr uint8_t SEG_DASH = SEG_G;

void showAddress(uint16_t address)
{
  if (address == 0)
  {
    if (SHOW_DASHES_WHEN_EMPTY)
    {
      const uint8_t dashes[4] = {SEG_DASH, SEG_DASH, SEG_DASH, SEG_DASH};
      display.setSegments(dashes);
    }
    else
    {
      display.clear();
    }
  }
  else if (address > 9999)
  {
    const uint8_t dashes[4] = {SEG_DASH, SEG_DASH, SEG_DASH, SEG_DASH}; // sécurité
    display.setSegments(dashes);
  }
  else
  {
    display.showNumberDec(address, false); // sans zéros à gauche
  }
}

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

  display.setBrightness(BRIGHTNESS);
  display.showNumberDec(8888);   // test des segments au démarrage
  delay(500);
  showAddress(0);

  LOG_I("\nLocoAddr-Display-min v%s\n", DCC_LOCOADDR_VERSION);
  LOG_V("RX GPIO%d, TM1637 CLK GPIO%d DIO GPIO%d\n", (int)DETECTOR_RX_PIN,
        TM1637_CLK_PIN, TM1637_DIO_PIN);

#if LOG_LEVEL >= LOG_DEBUG
  detector.setBurstCallback(printBurst);
#endif

  if (!detector.begin())
  {
    LOG_E("ERREUR : initialisation du détecteur impossible.\n");
    const uint8_t err[4] = {SEG_A | SEG_D | SEG_E | SEG_F | SEG_G, SEG_E | SEG_G, SEG_E | SEG_G, 0}; // "Err"
    display.setSegments(err);
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
  static uint32_t lastStatsMs = 0;
  static uint32_t lastErrorCheckMs = 0;
  static uint32_t lastUartErrors = 0;

  // Le TM1637 garde l'affichage tout seul : on ne lui écrit qu'au changement.
  const uint16_t address = detector.address();
  if (address != lastAddress)
  {
    showAddress(address);
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
