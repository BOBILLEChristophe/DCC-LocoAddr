/*
   LocoAddrReader_AVR.cpp - DCC-LocoAddr
   © Christophe BOBILLE - Locoduino - GPL-3.0
*/

#if defined(ARDUINO_ARCH_AVR)

#include "DCC_LocoAddr.h"

LocoAddrReader::LocoAddrReader(HardwareSerial &serial, int8_t dirPin)
    : m_serial(serial),
      m_dirPin(dirPin),
      m_started(false),
      m_callback(nullptr),
      m_callbackContext(nullptr),
      m_burstLen(0),
      m_burstOversized(false),
      m_lastByteUs(0)
{
}

LocoAddrReader::~LocoAddrReader()
{
    end();
}

void LocoAddrReader::setAddressConfirmations(uint8_t n)
{
    m_decoder.setAddressConfirmations(n);
}

void LocoAddrReader::setLossTimeoutMs(uint32_t ms)
{
    m_decoder.setLossTimeoutMs(ms);
}

void LocoAddrReader::setDirectionConfirmations(uint8_t n)
{
    m_decoder.setDirectionConfirmations(n);
}

void LocoAddrReader::setDirectionInverted(bool inverted)
{
    m_decoder.setDirectionInverted(inverted);
}

void LocoAddrReader::setBurstCallback(BurstCallback cb, void *context)
{
    m_callback = cb;
    m_callbackContext = context;
}

bool LocoAddrReader::begin()
{
    if (m_started)
    {
        return true;
    }

    m_decoder.reset();
    m_burstLen = 0;
    m_burstOversized = false;

    if (m_dirPin != NO_PIN)
    {
        pinMode(m_dirPin, INPUT);
    }

    // ATmega2560 @ 16 MHz :
    // 250000 bauds est obtenu exactement par l'USART matériel.
    m_serial.begin(BAUD_RATE);

    m_lastByteUs = micros();
    m_started = true;

    return true;
}

void LocoAddrReader::end()
{
    if (!m_started)
    {
        return;
    }

    m_serial.end();

    m_burstLen = 0;
    m_burstOversized = false;
    m_started = false;
}

void LocoAddrReader::receiveByte(uint8_t b)
{
    if (m_burstLen < MAX_BURST)
    {
        m_burst[m_burstLen++] = b;
    }
    else
    {
        m_burstOversized = true;
    }
}

void LocoAddrReader::flushBurst()
{
    if (m_burstLen == 0)
    {
        return;
    }

    const int dirLevel =
        (m_dirPin != NO_PIN) ? digitalRead(m_dirPin) : -1;

    if (m_burstOversized)
    {
        ++m_decoder.stats().oversizedBursts;
    }

    if (m_callback != nullptr)
    {
        m_callback(m_burst, m_burstLen, m_callbackContext);
    }

    m_decoder.processBurst(
        m_burst,
        m_burstLen,
        dirLevel,
        millis());

    m_burstLen = 0;
    m_burstOversized = false;
}

void LocoAddrReader::poll()
{
    if (!m_started)
    {
        return;
    }

    uint32_t nowUs = micros();

    // Termine une rafale si aucun octet n'est arrivé depuis assez longtemps.
    if ((m_burstLen > 0) &&
        ((uint32_t)(nowUs - m_lastByteUs) > BURST_GAP_US))
    {
        flushBurst();
    }

    // Vide immédiatement le tampon de l'UART Arduino.
    while (m_serial.available() > 0)
    {
        nowUs = micros();

        // Si poll() a été appelée après un silence, terminer d'abord
        // la rafale précédente.
        if ((m_burstLen > 0) &&
            ((uint32_t)(nowUs - m_lastByteUs) > BURST_GAP_US))
        {
            flushBurst();
        }

        const int value = m_serial.read();

        if (value >= 0)
        {
            receiveByte(static_cast<uint8_t>(value));
            m_lastByteUs = micros();
        }
    }

    nowUs = micros();

    if ((m_burstLen > 0) &&
        ((uint32_t)(nowUs - m_lastByteUs) > BURST_GAP_US))
    {
        flushBurst();
    }

    m_decoder.tick(millis());
}

uint16_t LocoAddrReader::address() const
{
    return m_decoder.address();
}

LocoDirection LocoAddrReader::direction() const
{
    return m_decoder.direction();
}

LocoAddrStats LocoAddrReader::stats() const
{
    return m_decoder.stats();
}

#endif // ARDUINO_ARCH_AVR
