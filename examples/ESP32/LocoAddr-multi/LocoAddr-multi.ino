/*
   LocoAddr-multi - plusieurs détecteurs sur un même ESP32
   DCC-LocoAddr v5.1.0

   Une instance LocoAddrReader par détecteur, chacune sur son propre UART.

   ESP32 classique et ESP32-S3 (3 UART) :
     - 2 détecteurs sur UART1 et UART2, moniteur série sur UART0 ;
     - 3 détecteurs avec USE_UART0 = true.
   ESP32-S2, C3, C6, H2 (2 UART) :
     - 1 détecteur sur UART1, moniteur série sur UART0 ;
     - 2 détecteurs avec USE_UART0 = true.
   Avec USE_UART0 = true, le moniteur série reste disponible seulement si
   "USB CDC On Boot" est activé (moniteur par l'USB natif).

   Choix des broches (ESP32 classique) :
     - pas de GPIO0, 2, 5, 12, 15 (broches lues au démarrage) ;
     - pas de GPIO1 / GPIO3 (reliés à l'adaptateur USB-série) ;
     - pas de GPIO16 / GPIO17 sur les modules WROVER (PSRAM) ;
     - DIR sur GPIO34, 35 ou 39 : entrées seules, sans tirage interne,
       ce qui convient puisque R21 tire DIR vers VIO sur la carte.

   Sur chaque carte : VIO -> 3V3, 5V µC -> 5V, GND µC -> GND.

   © Christophe BOBILLE - Locoduino - GPL-3.0
   https://github.com/BOBILLEChristophe/DCC-LocoAddr
*/

#ifndef ARDUINO_ARCH_ESP32
#error "Exemple pour ESP32 : pour un Raspberry Pi Pico, ouvrir l'exemple RP2040/LocoAddr-multi"
#endif

#include <DCC_LocoAddr.h>

// =============================================================================
// RÉGLAGES
// =============================================================================

// Niveau des messages (voir l'exemple LocoAddr) :
//   LOG_NONE, LOG_ERROR, LOG_INFO (défaut), LOG_VERBOSE (+ statistiques)
#define LOG_LEVEL LOG_INFO

// true : utilise aussi UART0 pour un détecteur supplémentaire
#define USE_UART0 false

constexpr uint32_t STATS_PERIOD_MS = 10000;

// =============================================================================

#define LOG_NONE 0
#define LOG_ERROR 1
#define LOG_INFO 2
#define LOG_VERBOSE 3
#define LOG_DEBUG 4

#if defined(SOC_UART_HP_NUM)
#define NB_UART SOC_UART_HP_NUM
#else
#define NB_UART SOC_UART_NUM
#endif

// Moniteur série disponible ?
#if !USE_UART0 || ARDUINO_USB_CDC_ON_BOOT
#define HAS_SERIAL 1
#else
#define HAS_SERIAL 0
#endif

#if CONFIG_IDF_TARGET_ESP32
LocoAddrReader detector1(UART_NUM_1, GPIO_NUM_4, GPIO_NUM_35);
LocoAddrReader detector2(UART_NUM_2, GPIO_NUM_32, GPIO_NUM_34);
#if USE_UART0
// UART0 est redirigé sur GPIO33 : GPIO3 reste à l'adaptateur USB-série.
LocoAddrReader detector3(UART_NUM_0, GPIO_NUM_33, GPIO_NUM_39);
#endif
#else
LocoAddrReader detector1(UART_NUM_1, GPIO_NUM_4, GPIO_NUM_5);
#if NB_UART > 2
LocoAddrReader detector2(UART_NUM_2, GPIO_NUM_6, GPIO_NUM_7);
#if USE_UART0
LocoAddrReader detector3(UART_NUM_0, GPIO_NUM_15, GPIO_NUM_16);
#endif
#elif USE_UART0
LocoAddrReader detector2(UART_NUM_0, GPIO_NUM_6, GPIO_NUM_7);
#endif
#endif

LocoAddrReader *const detectors[] = {
  &detector1,
#if (NB_UART > 2) || USE_UART0
  &detector2,
#endif
#if (NB_UART > 2) && USE_UART0
  &detector3,
#endif
};
constexpr size_t NB_DETECTORS = sizeof(detectors) / sizeof(detectors[0]);

void setup()
{
#if HAS_SERIAL
  Serial.begin(115200);
  delay(200);
  if (LOG_LEVEL >= LOG_INFO)
  {
    Serial.printf("\nLocoAddr-multi v%s - %u detecteurs\n",
                  DCC_LOCOADDR_VERSION, (unsigned)NB_DETECTORS);
  }
#else
  // UART0 est pris par un détecteur : on coupe les messages du système.
  esp_log_level_set("*", ESP_LOG_NONE);
#endif

  for (size_t i = 0; i < NB_DETECTORS; ++i)
  {
    if (!detectors[i]->begin())
    {
#if HAS_SERIAL
      if (LOG_LEVEL >= LOG_ERROR)
      {
        Serial.printf("ERREUR : detecteur %u non initialise\n", (unsigned)(i + 1));
      }
#endif
      while (true)
      {
        delay(1000);
      }
    }
  }
}

void loop()
{
  static uint16_t lastAddress[NB_DETECTORS];
  static LocoDirection lastDirection[NB_DETECTORS];
  static bool first = true;

  for (size_t i = 0; i < NB_DETECTORS; ++i)
  {
    const uint16_t address = detectors[i]->address();
    const LocoDirection direction = detectors[i]->direction();

    if (first || (address != lastAddress[i]) || (direction != lastDirection[i]))
    {
      lastAddress[i] = address;
      lastDirection[i] = direction;

#if HAS_SERIAL
      if (LOG_LEVEL < LOG_INFO)
      {
        // messages d'information désactivés
      }
      else if (address == 0)
      {
        Serial.printf("Detecteur %u : vide\n", (unsigned)(i + 1));
      }
      else
      {
        Serial.printf("Detecteur %u : locomotive %u, sens %s\n", (unsigned)(i + 1),
                      address, LocoAddrReader::directionName(direction));
      }
#else
      // Sans moniteur série : exploiter ici address / direction
      // (bus CAN, satellites autonomes, etc.).
#endif
    }
  }
  first = false;

#if HAS_SERIAL && (LOG_LEVEL >= LOG_VERBOSE)
  static uint32_t lastStatsMs = 0;
  if (millis() - lastStatsMs >= STATS_PERIOD_MS)
  {
    lastStatsMs = millis();
    for (size_t i = 0; i < NB_DETECTORS; ++i)
    {
      const LocoAddrStats st = detectors[i]->stats();
      Serial.printf("[stats %u] cutouts=%lu adr=%lu derniere_lue=%u rejetes=%lu erreurs_uart=%lu\n",
                    (unsigned)(i + 1), (unsigned long)st.bursts, (unsigned long)st.ch1Datagrams,
                    st.lastAddressRead, (unsigned long)st.rejectedBursts,
                    (unsigned long)(st.uartOverflows + st.frameErrors));
    }
  }
#endif

  delay(10);
}
