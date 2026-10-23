/*
   LocoAddrDecoder.cpp - DCC-LocoAddr

   © Christophe BOBILLE - Locoduino - GPL-3.0
*/

#include "LocoAddrDecoder.h"

namespace
{
// -----------------------------------------------------------------------------
// Table de décodage RailCom 4 parmi 8 (RCN-217 / NMRA S-9.3.2)
// octet UART -> valeur 6 bits (0..63)
// 64 = 0x0F, 65 = 0xF0 (ACK), 66 = 0xE1, 255 = octet invalide
// Table vérifiée contre la norme : 64 codes de données + 3 mots de contrôle.
// -----------------------------------------------------------------------------
const uint8_t DECODE_4OF8[256] = {
    255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,  64,
    255, 255, 255, 255, 255, 255, 255,  51, 255, 255, 255,  52, 255,  53,  54, 255,
    255, 255, 255, 255, 255, 255, 255,  58, 255, 255, 255,  59, 255,  60,  55, 255,
    255, 255, 255,  63, 255,  61,  56, 255, 255,  62,  57, 255, 255, 255, 255, 255,
    255, 255, 255, 255, 255, 255, 255,  36, 255, 255, 255,  35, 255,  34,  33, 255,
    255, 255, 255,  31, 255,  30,  32, 255, 255,  29,  28, 255,  27, 255, 255, 255,
    255, 255, 255,  25, 255,  24,  26, 255, 255,  23,  22, 255,  21, 255, 255, 255,
    255,  37,  20, 255,  19, 255, 255, 255,  50, 255, 255, 255, 255, 255, 255, 255,
    255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,  14, 255,  13,  12, 255,
    255, 255, 255,  10, 255,   9,  11, 255, 255,   8,   7, 255,   6, 255, 255, 255,
    255, 255, 255,   4, 255,   3,   5, 255, 255,   2,   1, 255,   0, 255, 255, 255,
    255,  15,  16, 255,  17, 255, 255, 255,  18, 255, 255, 255, 255, 255, 255, 255,
    255, 255, 255, 255, 255,  43,  48, 255, 255,  42,  47, 255,  49, 255, 255, 255,
    255,  41,  46, 255,  45, 255, 255, 255,  44, 255, 255, 255, 255, 255, 255, 255,
    255,  66,  40, 255,  39, 255, 255, 255,  38, 255, 255, 255, 255, 255, 255, 255,
     65, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255
};

// Identifiants du canal 1 (véhicules)
constexpr uint8_t ID_ADR_HIGH = 1;
constexpr uint8_t ID_ADR_LOW = 2;
}

LocoAddrDecoder::LocoAddrDecoder()
    : m_addressConfirmations(5),
      m_lossTimeoutMs(1000),
      m_directionConfirmations(3),
      m_directionInverted(false)
{
    reset();
}

void LocoAddrDecoder::setAddressConfirmations(uint8_t n)
{
    m_addressConfirmations = (n == 0) ? 1 : n;
}

void LocoAddrDecoder::setLossTimeoutMs(uint32_t ms)
{
    m_lossTimeoutMs = ms;
}

void LocoAddrDecoder::setDirectionConfirmations(uint8_t n)
{
    m_directionConfirmations = (n == 0) ? 1 : n;
}

void LocoAddrDecoder::setDirectionInverted(bool inverted)
{
    m_directionInverted = inverted;
}

void LocoAddrDecoder::reset()
{
    m_burstSeq = 0;
    m_adr1 = m_adr2 = 0;
    m_adr1Seq = m_adr2Seq = 0;
    m_hasAdr1 = m_hasAdr2 = false;
    m_candidateAddress = 0;
    m_candidateCount = 0;
    m_validatedAddress = 0;
    m_lastSeenMs = 0;
    clearDirection();
    m_stats = LocoAddrStats();
}

uint8_t LocoAddrDecoder::decode4of8(uint8_t raw)
{
    return DECODE_4OF8[raw];
}

// -----------------------------------------------------------------------------
// Reconstruction de l'adresse à partir de ADR1 (ID 1) et ADR2 (ID 2)
//   ADR1 = 0x00       : adresse courte, ADR2 = 0AAAAAAA (1..127)
//   ADR1 = 0x60       : adresse de consist, ADR2 = RAAAAAAA (R = sens, ignoré)
//   ADR1 = 10AAAAAA   : adresse longue, 128..10239
//   tout le reste     : rejeté
// -----------------------------------------------------------------------------
bool LocoAddrDecoder::buildAddress(uint8_t adr1, uint8_t adr2, uint16_t &address)
{
    if (adr1 == 0x00)
    {
        if (adr2 & 0x80)
        {
            return false;
        }
        address = adr2;
        return address != 0;
    }

    if (adr1 == 0x60)
    {
        address = adr2 & 0x7F;
        return address != 0;
    }

    if ((adr1 & 0xC0) == 0x80)
    {
        address = static_cast<uint16_t>(((adr1 & 0x3F) << 8) | adr2);
        return (address >= 128) && (address <= 10239);
    }

    return false;
}

// -----------------------------------------------------------------------------
// Traitement d'une rafale (= un cutout)
//
// Le canal 1 occupe les deux premiers octets du cutout. Le canal 2 (jusqu'à
// 6 octets) suit, et commence lui aussi sur une frontière de datagramme.
// Selon les décodeurs, ADR1 et ADR2 arrivent en alternance dans le canal 1,
// ou l'une des moitiés arrive dans le canal 2.
// Le canal 2 est donc parcouru datagramme par datagramme, en sautant les ACK,
// à l'aide de la longueur propre à chaque identifiant : on ne lit jamais une
// adresse au milieu d'un datagramme.
// -----------------------------------------------------------------------------
bool LocoAddrDecoder::takeAddressDatagram(const uint8_t *data, size_t len, size_t pos)
{
    if (pos + 2 > len)
    {
        return false;
    }

    const uint8_t s0 = decode4of8(data[pos]);
    const uint8_t s1 = decode4of8(data[pos + 1]);
    if ((s0 > 63) || (s1 > 63))
    {
        return false;
    }

    const uint8_t id = s0 >> 2;
    const uint8_t value = static_cast<uint8_t>(((s0 & 0x03) << 6) | s1);

    if (id == ID_ADR_HIGH)
    {
        ++m_stats.adr1Count;
        m_adr1 = value;
        m_adr1Seq = m_burstSeq;
        m_hasAdr1 = true;
    }
    else if (id == ID_ADR_LOW)
    {
        ++m_stats.adr2Count;
        m_adr2 = value;
        m_adr2Seq = m_burstSeq;
        m_hasAdr2 = true;
    }
    else
    {
        return false;
    }

    ++m_stats.ch1Datagrams;
    return true;
}

// Longueur en octets d'un datagramme selon son identifiant (RCN-217, véhicules),
// 0 si l'identifiant n'est pas connu.
static size_t datagramLength(uint8_t id)
{
    switch (id)
    {
    case 0:  // POM
    case 1:  // ADR1
    case 2:  // ADR2
        return 2;
    case 3:  // EXT
    case 7:  // DYN
        return 3;
    case 8:  // xPOM
    case 9:
    case 10:
    case 11:
        return 6;
    default:
        return 0;
    }
}

void LocoAddrDecoder::scanChannel2(const uint8_t *data, size_t len, size_t pos)
{
    while (pos < len)
    {
        const uint8_t sym = decode4of8(data[pos]);

        if ((sym == SYMBOL_ACK1) || (sym == SYMBOL_ACK2) || (sym == SYMBOL_BUSY))
        {
            ++pos; // ACK de remplissage : un seul octet
            continue;
        }
        if (sym > 63)
        {
            return; // octet invalide : on arrête là
        }

        const size_t n = datagramLength(sym >> 2);
        if (n == 0)
        {
            return; // identifiant inconnu : longueur inconnue, on arrête
        }
        if (n == 2)
        {
            takeAddressDatagram(data, len, pos);
        }
        pos += n;
    }
}

void LocoAddrDecoder::processBurst(const uint8_t *data, size_t len, int dirLevel, uint32_t nowMs)
{
    ++m_burstSeq;
    ++m_stats.bursts;

    size_t i = 0;
    while ((i < len) && (i < MAX_LEADING_GARBAGE) && (decode4of8(data[i]) == SYMBOL_INVALID))
    {
        ++i;
    }

    // Canal 1 en tête : obligatoire pour exploiter la rafale.
    if (!takeAddressDatagram(data, len, i))
    {
        ++m_stats.rejectedBursts;
        return;
    }

    // Canal 2 : facultatif.
    scanChannel2(data, len, i + 2);

    // La bascule du 74HC74 vient d'être horlogée par les octets de ce cutout :
    // DIR reflète le sens de pose de la locomotive qui émet.
    if (dirLevel >= 0)
    {
        sampleDirection(dirLevel);
    }

    // On associe chaque moitié reçue à l'autre moitié la plus récente,
    // à condition qu'elle date des cutouts immédiatement précédents.
    if (m_hasAdr1 && m_hasAdr2)
    {
        const uint32_t gap = (m_adr1Seq > m_adr2Seq) ? (m_adr1Seq - m_adr2Seq)
                                                     : (m_adr2Seq - m_adr1Seq);
        if (gap <= MAX_PAIR_GAP)
        {
            uint16_t address = 0;
            if (buildAddress(m_adr1, m_adr2, address))
            {
                m_stats.lastAddressRead = address;
                acceptAddress(address, nowMs);
            }
            else
            {
                ++m_stats.invalidAddresses;
                rejectAddress();
            }
        }
    }
}

// -----------------------------------------------------------------------------
// Validation de l'adresse (règle unique)
//  - une nouvelle adresse doit être reconstruite N fois de suite (défaut 5) ;
//  - une adresse déjà validée est confirmée par chaque reconstruction correcte ;
//  - elle est perdue après m_lossTimeoutMs sans confirmation (défaut 1 s).
// -----------------------------------------------------------------------------
void LocoAddrDecoder::acceptAddress(uint16_t address, uint32_t nowMs)
{
    if ((m_validatedAddress != 0) && (address == m_validatedAddress))
    {
        m_lastSeenMs = nowMs;
        m_candidateAddress = 0;
        m_candidateCount = 0;
        return;
    }

    if (address != m_candidateAddress)
    {
        m_candidateAddress = address;
        m_candidateCount = 1;
    }
    else if (m_candidateCount < 255)
    {
        ++m_candidateCount;
    }

    if (m_candidateCount >= m_addressConfirmations)
    {
        if ((m_validatedAddress != 0) && (address != m_validatedAddress))
        {
            clearDirection(); // autre locomotive : on réapprend le sens
        }
        m_validatedAddress = address;
        m_lastSeenMs = nowMs;
        m_candidateAddress = 0;
        m_candidateCount = 0;
    }
}

void LocoAddrDecoder::rejectAddress()
{
    m_candidateAddress = 0;
    m_candidateCount = 0;
}

void LocoAddrDecoder::tick(uint32_t nowMs)
{
    if (m_validatedAddress == 0)
    {
        return;
    }

    if (static_cast<uint32_t>(nowMs - m_lastSeenMs) >= m_lossTimeoutMs)
    {
        m_validatedAddress = 0;
        m_hasAdr1 = m_hasAdr2 = false;
        m_candidateAddress = 0;
        m_candidateCount = 0;
        clearDirection();
    }
}

// -----------------------------------------------------------------------------
// Sens de pose
// -----------------------------------------------------------------------------
void LocoAddrDecoder::sampleDirection(int dirLevel)
{
    LocoDirection d = (dirLevel == 0) ? LocoDirection::Normal : LocoDirection::Reversed;

    if (m_directionInverted)
    {
        d = (d == LocoDirection::Normal) ? LocoDirection::Reversed : LocoDirection::Normal;
    }

    if (d == m_directionCandidate)
    {
        if (m_directionCount < 255)
        {
            ++m_directionCount;
        }
    }
    else
    {
        m_directionCandidate = d;
        m_directionCount = 1;
    }

    if (m_directionCount >= m_directionConfirmations)
    {
        m_direction = d;
    }
}

void LocoAddrDecoder::clearDirection()
{
    m_direction = LocoDirection::Unknown;
    m_directionCandidate = LocoDirection::Unknown;
    m_directionCount = 0;
}

LocoDirection LocoAddrDecoder::direction() const
{
    return (m_validatedAddress != 0) ? m_direction : LocoDirection::Unknown;
}
