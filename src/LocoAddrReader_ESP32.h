/*
   LocoAddrReader_ESP32.h - DCC-LocoAddr
   Ne pas inclure directement : #include <DCC_LocoAddr.h>

   Lecture d'un détecteur Locoduino LocoAddr (carte v3.1) par un ESP32.
     - réception par l'UART matériel (pilote ESP-IDF), 250 kbit/s 8N1
     - découpage du flux en rafales, une par cutout
     - adresse de la locomotive (canal 1) : courte, longue ou consist
     - sens de pose (sortie DIR de l'option "sens de pose")

   Une instance = un détecteur = un UART.
   ESP32 classique : 2 instances (UART1, UART2), 3 si UART0 est libéré.

   © Christophe BOBILLE - Locoduino - GPL-3.0
   https://github.com/BOBILLEChristophe/DCC-LocoAddr
*/

#ifndef LOCOADDR_READER_ESP32_H
#define LOCOADDR_READER_ESP32_H

#include <Arduino.h>
#include <atomic>
#include "driver/gpio.h"
#include "driver/uart.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "LocoAddrDecoder.h"

class LocoAddrReader
{
public:
    // Fonction appelée pour chaque rafale reçue (diagnostic).
    // Elle s'exécute dans la tâche de lecture : rester bref, pas d'attente.
    typedef void (*BurstCallback)(const uint8_t *data, size_t len, void *context);

    // uartNum : UART_NUM_1, UART_NUM_2 (ou UART_NUM_0)
    // rxPin   : GPIO relié à OUT (J19)
    // dirPin  : GPIO relié à DIR (J22), GPIO_NUM_NC si l'option n'est pas montée
    LocoAddrReader(uart_port_t uartNum, gpio_num_t rxPin, gpio_num_t dirPin = GPIO_NUM_NC);
    ~LocoAddrReader();

    // Réglages, à appeler avant begin()
    void setAddressConfirmations(uint8_t n);   // défaut 5
    void setLossTimeoutMs(uint32_t ms);        // défaut 1000
    void setDirectionConfirmations(uint8_t n); // défaut 3
    void setDirectionInverted(bool inverted);  // échange Normal / Reversed
    void setBurstCallback(BurstCallback cb, void *context = nullptr);

    // Démarre l'UART et la tâche FreeRTOS. Retourne false en cas d'échec.
    bool begin(UBaseType_t taskPriority = 10, BaseType_t core = tskNO_AFFINITY);
    void end();

    // Lecture, utilisable depuis n'importe quelle tâche
    uint16_t address() const;          // 0 = aucune locomotive
    bool hasAddress() const { return address() != 0; }
    LocoDirection direction() const; // Unknown sans locomotive ou sans option
    LocoAddrStats stats() const;

    static const char *directionName(LocoDirection d) { return locoDirectionName(d); }

private:
    static constexpr uint32_t BAUD_RATE = 250000;
    static constexpr int RX_BUFFER_SIZE = 256;     // > FIFO matériel (128)
    static constexpr int EVENT_QUEUE_SIZE = 20;
    static constexpr uint8_t RX_FIFO_THRESHOLD = 120;
    static constexpr uint8_t RX_TIMEOUT_SYMBOLS = 2; // ~80 us à 250 kbit/s

    // Silence séparant deux cutouts. Un cutout dure au plus 488 us et deux
    // cutouts sont séparés par un paquet DCC complet (plusieurs ms).
    static constexpr int64_t BURST_GAP_US = 1500;

    // Un cutout contient au plus 8 octets (2 canal 1 + 6 canal 2).
    static constexpr size_t MAX_BURST = 16;

    static void taskEntry(void *param);
    void taskLoop();
    void appendBytes(size_t count);
    void flushBurst();
    void publish();
    bool initUart();

    const uart_port_t m_uartNum;
    const gpio_num_t m_rxPin;
    const gpio_num_t m_dirPin;

    QueueHandle_t m_queue;
    TaskHandle_t m_task;
    bool m_started;

    LocoAddrDecoder m_decoder;      // modifié uniquement par la tâche de lecture
    BurstCallback m_callback;
    void *m_callbackContext;

    uint8_t m_burst[MAX_BURST];
    size_t m_burstLen;
    bool m_burstOversized;

    // Valeurs publiées pour les autres tâches
    std::atomic<uint32_t> m_published; // adresse (16 bits) + sens (8 bits)
    mutable portMUX_TYPE m_statsLock;
    LocoAddrStats m_statsCopy;
};

#endif
