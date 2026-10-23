/*
   LocoAddrReader_RP2040.cpp - DCC-LocoAddr

   © Christophe BOBILLE - Locoduino - GPL-3.0
*/

#if defined(ARDUINO_ARCH_RP2040)

#include "DCC_LocoAddr.h"
#include "hardware/clocks.h"
#include "hardware/gpio.h"
#include "hardware/sync.h"
#include <string.h>

// -----------------------------------------------------------------------------
// Programme PIO : réception série 8N1, suréchantillonnage x8
// (programme "uart_rx_mini" des exemples officiels du pico-sdk)
//
//   0: wait   0 pin, 0        ; attendre le bit de start
//   1: set    x, 7      [10]  ; se placer au milieu du premier bit de données
//   2: in     pins, 1         ; lire un bit
//   3: jmp    x--, 2    [6]   ; 8 bits, un toutes les 8 périodes
//
// Le bit de stop n'est pas vérifié : le décodage 4 parmi 8 rejette de toute
// façon les octets erronés.
// -----------------------------------------------------------------------------
static const uint16_t kUartRxInstructions[] = {0x2020, 0xea27, 0x4001, 0x0642};

// Construit à l'exécution : la structure pio_program_t a gagné des champs avec
// les versions du pico-sdk ; les champs absents ici restent à zéro.
static pio_program_t makeUartRxProgram()
{
    pio_program_t p;
    memset(&p, 0, sizeof(p));
    p.instructions = kUartRxInstructions;
    p.length = 4;
    p.origin = -1;
    return p;
}
static const pio_program_t kUartRxProgram = makeUartRxProgram();

LocoAddrReader *LocoAddrReader::s_readers[LocoAddrReader::MAX_READERS] = {};
uint8_t LocoAddrReader::s_readerCount = 0;
repeating_timer_t LocoAddrReader::s_timer;
bool LocoAddrReader::s_timerRunning = false;
int LocoAddrReader::s_pioOffset[NUM_PIOS] = {};
bool LocoAddrReader::s_uartUsed[NUM_UARTS] = {};

static bool s_staticInit = false;
static critical_section_t s_listLock;

// Horloge en millisecondes pour le décodeur. time_us_32() / 1000 reviendrait
// à zéro toutes les 71 minutes ; time_us_64() / 1000 ne le fait qu'après 49 jours.
static inline uint32_t nowMs()
{
    return static_cast<uint32_t>(time_us_64() / 1000);
}

// Bloc PIO n° i (pio_get_instance() n'existe pas dans les anciens pico-sdk)
static PIO pioInstance(uint i)
{
#if NUM_PIOS > 2
    if (i == 2)
    {
        return pio2;
    }
#endif
    return (i == 0) ? pio0 : pio1;
}

// Broche RX d'UART ? Retourne 0 ou 1 (UART0 / UART1), -1 sinon.
// RX = GP1, 5, 9, 13, 17, 21, 25, 29 ; UART0 pour 1, 13, 17, 29.
static int uartIndexForRxPin(uint8_t pin)
{
    if ((pin > 29) || ((pin & 3) != 1))
    {
        return -1;
    }
    return (((pin >> 2) + 1) >> 1) & 1;
}

LocoAddrReader::LocoAddrReader(uint8_t rxPin, int dirPin)
    : m_rxPin(rxPin),
      m_dirPin(dirPin),
      m_forcePio(false),
      m_transport(Transport::None),
      m_uart(nullptr),
      m_pio(nullptr),
      m_sm(-1),
      m_transportName("-"),
      m_callback(nullptr),
      m_callbackContext(nullptr),
      m_burstLen(0),
      m_burstOversized(false),
      m_lastByteUs(0),
      m_lastPublishUs(0),
      m_published(0),
      m_statsSeq(0),
      m_statsCopy()
{
}

LocoAddrReader::~LocoAddrReader()
{
    end();
}

// -----------------------------------------------------------------------------
// Réglages
// -----------------------------------------------------------------------------
void LocoAddrReader::setAddressConfirmations(uint8_t n) { m_decoder.setAddressConfirmations(n); }
void LocoAddrReader::setLossTimeoutMs(uint32_t ms) { m_decoder.setLossTimeoutMs(ms); }
void LocoAddrReader::setDirectionConfirmations(uint8_t n) { m_decoder.setDirectionConfirmations(n); }
void LocoAddrReader::setDirectionInverted(bool inverted) { m_decoder.setDirectionInverted(inverted); }
void LocoAddrReader::forcePio(bool force) { m_forcePio = force; }

void LocoAddrReader::setBurstCallback(BurstCallback cb, void *context)
{
    m_callbackContext = context;
    m_callback = cb;
}

// -----------------------------------------------------------------------------
// Démarrage / arrêt
// -----------------------------------------------------------------------------
bool LocoAddrReader::begin()
{
    if (m_transport != Transport::None)
    {
        return true;
    }
    if (m_rxPin >= NUM_BANK0_GPIOS)
    {
        return false;
    }

    if (!s_staticInit)
    {
        // Verrou matériel réservé à la liste des détecteurs : jamais partagé
        // avec un autre verrou, et jamais imbriqué (voir timerCallback).
        critical_section_init_with_lock_num(&s_listLock, spin_lock_claim_unused(true));
        for (int i = 0; i < (int)NUM_PIOS; ++i)
        {
            s_pioOffset[i] = -1;
        }
        s_staticInit = true;
    }

    m_decoder.reset();
    m_burstLen = 0;
    m_burstOversized = false;
    m_published = 0;
    m_statsCopy = LocoAddrStats();

    if (m_dirPin >= 0)
    {
        // DIR est tiré vers VIO par R21 sur la carte : pas de tirage interne.
        gpio_init(m_dirPin);
        gpio_set_dir(m_dirPin, GPIO_IN);
        gpio_disable_pulls(m_dirPin);
    }

    bool ok = false;
    const int uartIndex = m_forcePio ? -1 : uartIndexForRxPin(m_rxPin);
    if ((uartIndex >= 0) && !s_uartUsed[uartIndex])
    {
        ok = startUart(static_cast<uint8_t>(uartIndex));
    }
    if (!ok)
    {
        ok = startPio();
    }
    if (!ok)
    {
        return false;
    }

    // Tirage interne vers le haut : sécurité si VIO n'est pas câblé.
    // Avec VIO câblé, R17 (2,2 kΩ) l'emporte largement.
    gpio_pull_up(m_rxPin);

    m_lastByteUs = time_us_32();
    m_lastPublishUs = m_lastByteUs;

    if (!addToPoll(this))
    {
        end();
        return false;
    }
    return true;
}

bool LocoAddrReader::startUart(uint8_t index)
{
    m_uart = uart_get_instance(index);
    uart_init(m_uart, BAUD_RATE);
    uart_set_format(m_uart, 8, 1, UART_PARITY_NONE);
    uart_set_hw_flow(m_uart, false, false);
    uart_set_fifo_enabled(m_uart, true); // FIFO de 32 octets
    gpio_set_function(m_rxPin, GPIO_FUNC_UART);

    s_uartUsed[index] = true;
    m_transport = Transport::Uart;
    snprintf(m_transportName, sizeof(m_transportName), "UART%u", (unsigned)index);
    return true;
}

bool LocoAddrReader::startPio()
{
    for (uint i = 0; i < NUM_PIOS; ++i)
    {
        PIO pio = pioInstance(i);

        // Programme chargé une seule fois par bloc PIO, partagé par ses 4 machines
        if (s_pioOffset[i] < 0)
        {
            if (!pio_can_add_program(pio, &kUartRxProgram))
            {
                continue;
            }
        }

        const int sm = pio_claim_unused_sm(pio, false);
        if (sm < 0)
        {
            continue;
        }

        if (s_pioOffset[i] < 0)
        {
            s_pioOffset[i] = pio_add_program(pio, &kUartRxProgram);
        }
        const uint offset = static_cast<uint>(s_pioOffset[i]);

        pio_sm_set_consecutive_pindirs(pio, sm, m_rxPin, 1, false);
        pio_gpio_init(pio, m_rxPin);

        pio_sm_config c = pio_get_default_sm_config();
        sm_config_set_wrap(&c, offset, offset + 3);
        sm_config_set_in_pins(&c, m_rxPin);
        sm_config_set_in_shift(&c, true, true, 8);   // vers la droite, poussée auto à 8 bits
        sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_RX); // FIFO de réception de 8 mots
        sm_config_set_clkdiv(&c, static_cast<float>(clock_get_hz(clk_sys)) / (8.0f * BAUD_RATE));
        pio_sm_init(pio, sm, offset, &c);
        pio_sm_set_enabled(pio, sm, true);

        m_pio = pio;
        m_sm = sm;
        m_transport = Transport::Pio;
        snprintf(m_transportName, sizeof(m_transportName), "PIO%u SM%u", i & 3u, static_cast<unsigned>(sm) & 3u);
        return true;
    }
    return false;
}

void LocoAddrReader::end()
{
    if (m_transport == Transport::None)
    {
        return;
    }

    removeFromPoll(this);

    if (m_transport == Transport::Uart)
    {
        const uint index = uart_get_index(m_uart);
        uart_deinit(m_uart);
        s_uartUsed[index] = false;
        m_uart = nullptr;
    }
    else
    {
        pio_sm_set_enabled(m_pio, m_sm, false);
        pio_sm_unclaim(m_pio, m_sm);
        m_pio = nullptr;
        m_sm = -1;
        // Le programme reste chargé : il resservira au prochain begin().
    }

    gpio_init(m_rxPin);
    m_transport = Transport::None;
    snprintf(m_transportName, sizeof(m_transportName), "-");
    m_published = 0;
}

// -----------------------------------------------------------------------------
// Liste des détecteurs lus par l'interruption
// -----------------------------------------------------------------------------
bool LocoAddrReader::addToPoll(LocoAddrReader *reader)
{
    bool ok = false;
    critical_section_enter_blocking(&s_listLock);
    if (s_readerCount < MAX_READERS)
    {
        s_readers[s_readerCount++] = reader;
        ok = true;
    }
    critical_section_exit(&s_listLock);

    if (ok && !s_timerRunning)
    {
        // Délai négatif : périodes régulières, indépendantes de la durée du traitement
        s_timerRunning = add_repeating_timer_us(-static_cast<int64_t>(POLL_PERIOD_US),
                                                timerCallback, nullptr, &s_timer);
        if (!s_timerRunning)
        {
            removeFromPoll(reader);
            ok = false;
        }
    }
    return ok;
}

void LocoAddrReader::removeFromPoll(LocoAddrReader *reader)
{
    critical_section_enter_blocking(&s_listLock);
    for (uint8_t i = 0; i < s_readerCount; ++i)
    {
        if (s_readers[i] == reader)
        {
            s_readers[i] = s_readers[--s_readerCount];
            s_readers[s_readerCount] = nullptr;
            break;
        }
    }
    const bool empty = (s_readerCount == 0);
    critical_section_exit(&s_listLock);

    if (empty && s_timerRunning)
    {
        cancel_repeating_timer(&s_timer);
        s_timerRunning = false;
    }
}

bool LocoAddrReader::timerCallback(repeating_timer_t *)
{
    const uint32_t nowUs = time_us_32();
    critical_section_enter_blocking(&s_listLock);
    for (uint8_t i = 0; i < s_readerCount; ++i)
    {
        s_readers[i]->poll(nowUs);
    }
    critical_section_exit(&s_listLock);
    return true; // continuer
}

// -----------------------------------------------------------------------------
// Réception (sous interruption)
// -----------------------------------------------------------------------------
void LocoAddrReader::poll(uint32_t nowUs)
{
    bool received = false;

    if (m_transport == Transport::Uart)
    {
        uart_hw_t *hw = uart_get_hw(m_uart);
        while (uart_is_readable(m_uart))
        {
            const uint32_t dr = hw->dr;
            if (dr & UART_UARTDR_OE_BITS)
            {
                ++m_decoder.stats().uartOverflows;
            }
            if (dr & (UART_UARTDR_FE_BITS | UART_UARTDR_BE_BITS))
            {
                ++m_decoder.stats().frameErrors;
            }
            receiveByte(static_cast<uint8_t>(dr & 0xFF));
            received = true;
        }
    }
    else if (m_transport == Transport::Pio)
    {
        // FIFO plein : des octets ont été perdus depuis le dernier passage
        const uint32_t stallMask = 1u << (PIO_FDEBUG_RXSTALL_LSB + m_sm);
        if (m_pio->fdebug & stallMask)
        {
            m_pio->fdebug = stallMask;
            ++m_decoder.stats().uartOverflows;
        }
        while (!pio_sm_is_rx_fifo_empty(m_pio, m_sm))
        {
            // Décalage vers la droite : l'octet est dans les 8 bits de poids fort
            receiveByte(static_cast<uint8_t>(pio_sm_get(m_pio, m_sm) >> 24));
            received = true;
        }
    }

    if (received)
    {
        m_lastByteUs = nowUs;
    }
    else if ((m_burstLen > 0) && (nowUs - m_lastByteUs > BURST_GAP_US))
    {
        flushBurst(nowUs);
    }

    if (nowUs - m_lastPublishUs >= PUBLISH_PERIOD_US)
    {
        m_decoder.tick(nowMs());
        publish();
        m_lastPublishUs = nowUs;
    }
}

void LocoAddrReader::receiveByte(uint8_t b)
{
    if (m_burstLen < MAX_BURST)
    {
        m_burst[m_burstLen++] = b;
    }
    else
    {
        // Rafale anormalement longue : on garde le début, on jette le reste.
        m_burstOversized = true;
    }
}

void LocoAddrReader::flushBurst(uint32_t)
{
    // DIR est lu après le cutout : la bascule a été horlogée par ses octets
    // et ne bouge plus avant le cutout suivant.
    const int dirLevel = (m_dirPin >= 0) ? (gpio_get(m_dirPin) ? 1 : 0) : -1;

    if (m_burstOversized)
    {
        ++m_decoder.stats().oversizedBursts;
    }

    if (m_callback != nullptr)
    {
        m_callback(m_burst, m_burstLen, m_callbackContext);
    }

    m_decoder.processBurst(m_burst, m_burstLen, dirLevel, nowMs());

    m_burstLen = 0;
    m_burstOversized = false;
    publish();
}

void LocoAddrReader::publish()
{
    const uint32_t packed = static_cast<uint32_t>(m_decoder.address()) |
                            (static_cast<uint32_t>(static_cast<uint8_t>(m_decoder.direction())) << 16);
    m_published = packed; // écriture 32 bits : atomique sur Cortex-M

    // Compteur impair pendant l'écriture : stats() recommence sa lecture.
    // Pas de verrou ici : poll() s'exécute déjà sous le verrou de la liste,
    // et deux verrous imbriqués peuvent partager le même verrou matériel.
    ++m_statsSeq;
    __dmb();
    m_statsCopy = m_decoder.stats();
    __dmb();
    ++m_statsSeq;
}

// -----------------------------------------------------------------------------
// Lecture
// -----------------------------------------------------------------------------
uint16_t LocoAddrReader::address() const
{
    return static_cast<uint16_t>(m_published & 0xFFFF);
}

LocoDirection LocoAddrReader::direction() const
{
    return static_cast<LocoDirection>(static_cast<int8_t>((m_published >> 16) & 0xFF));
}

LocoAddrStats LocoAddrReader::stats() const
{
    LocoAddrStats s;
    for (;;)
    {
        const uint32_t before = m_statsSeq;
        __dmb();
        if ((before & 1u) == 0)
        {
            s = *const_cast<const LocoAddrStats *>(&m_statsCopy);
            __dmb();
            if (m_statsSeq == before)
            {
                return s;
            }
        }
        tight_loop_contents();
    }
}

#endif // ARDUINO_ARCH_RP2040
