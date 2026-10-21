/*
   DCC_LocoAddr.cpp - DCC-LocoAddr v5.0.0

   © Christophe BOBILLE - Locoduino - GPL-3.0
*/

#include "DCC_LocoAddr.h"
#include "esp_timer.h"
#include "esp_idf_version.h"

LocoAddrReader::LocoAddrReader(uart_port_t uartNum, gpio_num_t rxPin, gpio_num_t dirPin)
    : m_uartNum(uartNum),
      m_rxPin(rxPin),
      m_dirPin(dirPin),
      m_queue(nullptr),
      m_task(nullptr),
      m_started(false),
      m_callback(nullptr),
      m_callbackContext(nullptr),
      m_burstLen(0),
      m_burstOversized(false),
      m_published(0),
      m_statsLock(portMUX_INITIALIZER_UNLOCKED),
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

void LocoAddrReader::setBurstCallback(BurstCallback cb, void *context)
{
    m_callbackContext = context;
    m_callback = cb;
}

// -----------------------------------------------------------------------------
// Démarrage / arrêt
// -----------------------------------------------------------------------------
bool LocoAddrReader::begin(UBaseType_t taskPriority, BaseType_t core)
{
    if (m_started)
    {
        return true;
    }

    m_decoder.reset();
    m_burstLen = 0;
    m_burstOversized = false;
    m_published.store(0);

    if (m_dirPin != GPIO_NUM_NC)
    {
        // DIR est tiré vers VIO par R21 sur la carte : pas de tirage interne.
        gpio_reset_pin(m_dirPin);
        gpio_set_direction(m_dirPin, GPIO_MODE_INPUT);
        gpio_set_pull_mode(m_dirPin, GPIO_FLOATING);
    }

    if (!initUart())
    {
        return false;
    }

    const BaseType_t ok = xTaskCreatePinnedToCore(taskEntry, "LocoAddr", 4096, this,
                                                  taskPriority, &m_task, core);
    if (ok != pdPASS)
    {
        uart_driver_delete(m_uartNum);
        m_queue = nullptr;
        m_task = nullptr;
        return false;
    }

    m_started = true;
    return true;
}

void LocoAddrReader::end()
{
    if (!m_started)
    {
        return;
    }

    if (m_task != nullptr)
    {
        vTaskDelete(m_task);
        m_task = nullptr;
    }

    uart_driver_delete(m_uartNum);
    m_queue = nullptr;
    m_published.store(0);
    m_started = false;
}

bool LocoAddrReader::initUart()
{
    uart_config_t cfg = {};
    cfg.baud_rate = BAUD_RATE;
    cfg.data_bits = UART_DATA_8_BITS;
    cfg.parity = UART_PARITY_DISABLE;
    cfg.stop_bits = UART_STOP_BITS_1;
    cfg.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
#if ESP_IDF_VERSION_MAJOR >= 5
    cfg.source_clk = UART_SCLK_DEFAULT; // APB sur ESP32, S2, S3, C3 ; PLL sur C6, H2
#else
    cfg.source_clk = UART_SCLK_APB;
#endif

    // RX seulement : pas de broche TX.
    if ((uart_param_config(m_uartNum, &cfg) != ESP_OK) ||
        (uart_set_pin(m_uartNum, UART_PIN_NO_CHANGE, m_rxPin, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE) != ESP_OK))
    {
        return false;
    }

    if (uart_driver_install(m_uartNum, RX_BUFFER_SIZE, 0, EVENT_QUEUE_SIZE, &m_queue, 0) != ESP_OK)
    {
        m_queue = nullptr;
        return false;
    }

    if ((uart_set_rx_full_threshold(m_uartNum, RX_FIFO_THRESHOLD) != ESP_OK) ||
        (uart_set_rx_timeout(m_uartNum, RX_TIMEOUT_SYMBOLS) != ESP_OK))
    {
        uart_driver_delete(m_uartNum);
        m_queue = nullptr;
        return false;
    }

    // Tirage interne vers le haut sur RX : sécurité si VIO n'est pas câblé.
    // Il n'existe pas sur GPIO34 à 39 de l'ESP32 classique (erreur ignorée).
    // Avec VIO câblé, R17 (2,2 kΩ) l'emporte largement.
    gpio_pullup_en(m_rxPin);

    uart_flush_input(m_uartNum);
    xQueueReset(m_queue);
    return true;
}

// -----------------------------------------------------------------------------
// Tâche de réception
//
// Les événements UART_DATA ne correspondent pas aux datagrammes RailCom : un
// cutout peut arriver en un ou plusieurs événements. On regroupe donc les
// octets tant que les événements se suivent de près, et on traite la rafale
// dès qu'un silence de plus de BURST_GAP_US la termine.
// -----------------------------------------------------------------------------
void LocoAddrReader::taskEntry(void *param)
{
    static_cast<LocoAddrReader *>(param)->taskLoop();
    vTaskDelete(nullptr);
}

void LocoAddrReader::taskLoop()
{
    uart_event_t event;
    int64_t lastEventUs = 0;

    for (;;)
    {
        // Rafale en cours : on attend au plus 2 ms pour la clore.
        // Sinon, réveil toutes les 50 ms pour surveiller la perte.
        const TickType_t wait = (m_burstLen > 0) ? pdMS_TO_TICKS(2) + 1 : pdMS_TO_TICKS(50);

        if (xQueueReceive(m_queue, &event, wait) == pdTRUE)
        {
            const int64_t nowUs = esp_timer_get_time();
            if ((m_burstLen > 0) && (nowUs - lastEventUs > BURST_GAP_US))
            {
                flushBurst();
            }
            lastEventUs = nowUs;

            switch (event.type)
            {
            case UART_DATA:
                appendBytes(event.size);
                break;

            case UART_FIFO_OVF:
            case UART_BUFFER_FULL:
                uart_flush_input(m_uartNum);
                xQueueReset(m_queue);
                m_burstLen = 0;
                m_burstOversized = false;
                ++m_decoder.stats().uartOverflows;
                break;

            case UART_FRAME_ERR:
                ++m_decoder.stats().frameErrors;
                break;

            default:
                break;
            }
        }
        else if (m_burstLen > 0)
        {
            flushBurst();
        }

        m_decoder.tick(static_cast<uint32_t>(esp_timer_get_time() / 1000));
        publish();
    }
}

void LocoAddrReader::appendBytes(size_t count)
{
    while (count > 0)
    {
        uint8_t scratch[32];
        uint8_t *dst;
        size_t room;

        if (m_burstLen < MAX_BURST)
        {
            dst = m_burst + m_burstLen;
            room = MAX_BURST - m_burstLen;
        }
        else
        {
            // Rafale anormalement longue : on garde le début, on jette le reste.
            dst = scratch;
            room = sizeof(scratch);
            m_burstOversized = true;
        }

        const size_t want = (count < room) ? count : room;
        const int got = uart_read_bytes(m_uartNum, dst, want, 0);
        if (got <= 0)
        {
            break;
        }

        if (dst != scratch)
        {
            m_burstLen += static_cast<size_t>(got);
        }
        count -= static_cast<size_t>(got);
    }
}

void LocoAddrReader::flushBurst()
{
    // DIR est lu après le cutout : la bascule a été horlogée par ses octets
    // et ne bouge plus avant le cutout suivant.
    const int dirLevel = (m_dirPin != GPIO_NUM_NC) ? gpio_get_level(m_dirPin) : -1;

    if (m_burstOversized)
    {
        ++m_decoder.stats().oversizedBursts;
    }

    if (m_callback != nullptr)
    {
        m_callback(m_burst, m_burstLen, m_callbackContext);
    }

    m_decoder.processBurst(m_burst, m_burstLen, dirLevel,
                           static_cast<uint32_t>(esp_timer_get_time() / 1000));

    m_burstLen = 0;
    m_burstOversized = false;
}

void LocoAddrReader::publish()
{
    const uint32_t packed = static_cast<uint32_t>(m_decoder.address()) |
                            (static_cast<uint32_t>(static_cast<uint8_t>(m_decoder.direction())) << 16);
    m_published.store(packed);

    portENTER_CRITICAL(&m_statsLock);
    m_statsCopy = m_decoder.stats();
    portEXIT_CRITICAL(&m_statsLock);
}

// -----------------------------------------------------------------------------
// Lecture
// -----------------------------------------------------------------------------
uint16_t LocoAddrReader::address() const
{
    return static_cast<uint16_t>(m_published.load() & 0xFFFF);
}

LocoDirection LocoAddrReader::direction() const
{
    return static_cast<LocoDirection>(static_cast<int8_t>((m_published.load() >> 16) & 0xFF));
}

LocoAddrStats LocoAddrReader::stats() const
{
    portENTER_CRITICAL(&m_statsLock);
    const LocoAddrStats s = m_statsCopy;
    portEXIT_CRITICAL(&m_statsLock);
    return s;
}

const char *LocoAddrReader::directionName(LocoDirection d)
{
    switch (d)
    {
    case LocoDirection::Normal:
        return "normal";
    case LocoDirection::Reversed:
        return "inverse";
    default:
        return "inconnu";
    }
}
