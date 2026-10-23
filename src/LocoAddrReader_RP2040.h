/*
   LocoAddrReader_RP2040.h - DCC-LocoAddr
   Ne pas inclure directement : #include <DCC_LocoAddr.h>

   Lecture de détecteurs Locoduino LocoAddr (carte v3.1) par un RP2040 ou un
   RP2350 (Raspberry Pi Pico, Pico W, Pico 2), cœur Arduino "Raspberry Pi
   Pico/RP2040/RP2350" d'Earle Philhower.

   Chaque détecteur utilise, selon sa broche RX :
     - un UART matériel si la broche est une broche RX d'UART
       (UART0 : GP1, GP13, GP17, GP29 ; UART1 : GP5, GP9, GP21, GP25)
       et si cet UART est encore libre ;
     - sinon une machine d'état PIO (n'importe quelle broche).
   Capacité : 2 UART + 4 machines d'état par bloc PIO, soit 10 détecteurs sur
   RP2040 (2 + 8) et 14 sur RP2350 (2 + 12). Le Pico W réserve une machine
   d'état pour sa puce Wi-Fi.

   Les UART utilisés ici ne sont plus disponibles pour Serial1 / Serial2.
   Serial (USB) n'est pas concerné.

   Lecture : une interruption de minuterie toutes les 250 µs vide les FIFO de
   tous les détecteurs et traite les cutouts terminés. Le programme principal
   n'a rien à appeler ; il lit address(), direction() et stats() quand il veut,
   depuis l'un ou l'autre cœur.

   © Christophe BOBILLE - Locoduino - GPL-3.0
   https://github.com/BOBILLEChristophe/DCC-LocoAddr
*/

#ifndef LOCOADDR_READER_RP2040_H
#define LOCOADDR_READER_RP2040_H

#include <Arduino.h>
#include "hardware/pio.h"
#include "hardware/uart.h"
#include "pico/critical_section.h"
#include "pico/time.h"
#include "LocoAddrDecoder.h"

class LocoAddrReader
{
public:
    // Fonction appelée pour chaque rafale reçue (diagnostic).
    // ATTENTION : sur RP2040, elle s'exécute SOUS INTERRUPTION. Elle doit être
    // très brève : copier les octets, jamais de Serial.print() ni de delay().
    typedef void (*BurstCallback)(const uint8_t *data, size_t len, void *context);

    static constexpr int NO_PIN = -1;

    // Nombre maximal de détecteurs : 2 UART + 4 machines d'état par bloc PIO
    static constexpr uint8_t MAX_READERS = NUM_UARTS + 4 * NUM_PIOS;

    // rxPin  : broche GPn reliée à OUT (J19)
    // dirPin : broche GPn reliée à DIR (J22), NO_PIN si l'option n'est pas montée
    explicit LocoAddrReader(uint8_t rxPin, int dirPin = NO_PIN);
    ~LocoAddrReader();

    // Réglages, à appeler avant begin()
    void setAddressConfirmations(uint8_t n);   // défaut 5
    void setLossTimeoutMs(uint32_t ms);        // défaut 1000
    void setDirectionConfirmations(uint8_t n); // défaut 3
    void setDirectionInverted(bool inverted);  // échange Normal / Reversed
    void setBurstCallback(BurstCallback cb, void *context = nullptr);

    // Interdit l'UART matériel : le détecteur passera par un récepteur PIO même
    // si sa broche est une broche RX d'UART (pour laisser Serial1/Serial2 libres).
    void forcePio(bool force = true);

    // Réserve un UART ou une machine d'état PIO et démarre la lecture.
    // Retourne false si la broche est invalide ou s'il ne reste plus de ressource.
    bool begin();
    void end();

    // Lecture, utilisable depuis n'importe quel cœur
    uint16_t address() const;        // 0 = aucune locomotive
    bool hasAddress() const { return address() != 0; }
    LocoDirection direction() const; // Unknown sans locomotive ou sans option
    LocoAddrStats stats() const;

    // Ressource utilisée : "UART0", "UART1", "PIO0 SM2"... ou "-" avant begin()
    const char *transportName() const { return m_transportName; }

    static const char *directionName(LocoDirection d) { return locoDirectionName(d); }

private:
    static constexpr uint32_t BAUD_RATE = 250000;
    static constexpr uint32_t POLL_PERIOD_US = 250;  // < 8 octets PIO (320 µs)
    static constexpr uint32_t BURST_GAP_US = 1500;   // silence entre deux cutouts
    static constexpr uint32_t PUBLISH_PERIOD_US = 10000;
    static constexpr size_t MAX_BURST = 16;          // un cutout : 8 octets au plus

    enum class Transport : uint8_t { None, Uart, Pio };

    // Interruption commune à tous les détecteurs
    static bool timerCallback(repeating_timer_t *rt);
    static bool addToPoll(LocoAddrReader *reader);
    static void removeFromPoll(LocoAddrReader *reader);

    bool startUart(uint8_t index);
    bool startPio();
    void poll(uint32_t nowUs);
    void receiveByte(uint8_t b);
    void flushBurst(uint32_t nowUs);
    void publish();

    const uint8_t m_rxPin;
    const int m_dirPin;
    bool m_forcePio;

    Transport m_transport;
    uart_inst_t *m_uart;
    PIO m_pio;
    int m_sm;
    char m_transportName[16];

    LocoAddrDecoder m_decoder; // modifié uniquement sous interruption
    BurstCallback m_callback;
    void *m_callbackContext;

    uint8_t m_burst[MAX_BURST];
    size_t m_burstLen;
    bool m_burstOversized;
    uint32_t m_lastByteUs;
    uint32_t m_lastPublishUs;

    // Valeurs publiées
    volatile uint32_t m_published; // adresse (16 bits) + sens (8 bits)
    // Copie des statistiques protégée par un compteur de séquence : écrite
    // seulement sous interruption, lue sans verrou depuis l'un ou l'autre cœur.
    volatile uint32_t m_statsSeq;
    LocoAddrStats m_statsCopy;

    // État partagé par toutes les instances
    static LocoAddrReader *s_readers[MAX_READERS];
    static uint8_t s_readerCount;
    static repeating_timer_t s_timer;
    static bool s_timerRunning;
    static int s_pioOffset[NUM_PIOS];
    static bool s_uartUsed[NUM_UARTS];
};

#endif
