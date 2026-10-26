/*
   LocoAddrReader_AVR.h - DCC-LocoAddr
   Lecture d'un détecteur Locoduino LocoAddr v3.1 sur Arduino AVR.

   Arduino Mega 2560 :
   - utilisation des UART matériels Serial1, Serial2 et Serial3 ;
   - une instance LocoAddrReader par détecteur ;
   - Serial reste disponible pour le moniteur série.

   IMPORTANT :
   poll() doit être appelée très régulièrement depuis loop().

   © Christophe BOBILLE - Locoduino - GPL-3.0
*/

#ifndef LOCOADDR_READER_AVR_H
#define LOCOADDR_READER_AVR_H

#include <Arduino.h>
#include "LocoAddrDecoder.h"

class LocoAddrReader
{
public:
    typedef void (*BurstCallback)(const uint8_t *data, size_t len, void *context);

    static constexpr int8_t NO_PIN = -1;

    // serial : UART matériel utilisé pour recevoir OUT
    // dirPin : entrée DIR, ou NO_PIN si non utilisée
    explicit LocoAddrReader(HardwareSerial &serial, int8_t dirPin = NO_PIN);
    ~LocoAddrReader();

    void setAddressConfirmations(uint8_t n);
    void setLossTimeoutMs(uint32_t ms);
    void setDirectionConfirmations(uint8_t n);
    void setDirectionInverted(bool inverted);
    void setBurstCallback(BurstCallback cb, void *context = nullptr);

    bool begin();
    void end();

    // À appeler aussi souvent que possible dans loop().
    void poll();

    uint16_t address() const;
    bool hasAddress() const { return address() != 0; }
    LocoDirection direction() const;
    LocoAddrStats stats() const;

    static const char *directionName(LocoDirection d)
    {
        return locoDirectionName(d);
    }

private:
    static constexpr uint32_t BAUD_RATE = 250000;
    static constexpr uint32_t BURST_GAP_US = 1500;
    static constexpr size_t MAX_BURST = 16;

    void receiveByte(uint8_t b);
    void flushBurst();

    HardwareSerial &m_serial;
    const int8_t m_dirPin;

    bool m_started;

    LocoAddrDecoder m_decoder;

    BurstCallback m_callback;
    void *m_callbackContext;

    uint8_t m_burst[MAX_BURST];
    size_t m_burstLen;
    bool m_burstOversized;

    uint32_t m_lastByteUs;
};

#endif
