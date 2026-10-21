/*
   LocoAddrDecoder.h - DCC-LocoAddr v5.0.0

   Décodage RailCom (canal 1 : adresse de la locomotive) et sens de pose,
   indépendant du matériel : aucune dépendance à l'ESP32 ni à Arduino.
   La classe LocoAddrReader (DCC_LocoAddr.h) lui fournit les octets reçus,
   regroupés par cutout ("rafales").

   © Christophe BOBILLE - Locoduino - GPL-3.0
*/

#ifndef LOCOADDR_DECODER_H
#define LOCOADDR_DECODER_H

#include <stddef.h>
#include <stdint.h>

// Sens de pose de la locomotive, lu sur la sortie DIR de la carte v3.1
// équipée de l'option "sens de pose".
//  - Unknown  : pas de locomotive validée, option absente ou lecture instable
//  - Normal   : DIR au niveau bas (bascule 74HC74 à 1)
//  - Reversed : DIR au niveau haut
// La correspondance avec le sens physique dépend du branchement de J20/J21 :
// elle se règle une fois pour toutes avec setDirectionInverted().
enum class LocoDirection : int8_t
{
    Reversed = -1,
    Unknown = 0,
    Normal = 1
};

struct LocoAddrStats
{
    uint32_t bursts;            // rafales reçues (une par cutout)
    uint32_t ch1Datagrams;      // datagrammes ADR1/ADR2 valides (canal 1 ou canal 2)
    uint32_t adr1Count;         // dont ADR1 (partie haute)
    uint32_t adr2Count;         // dont ADR2 (partie basse)
    uint16_t lastAddressRead;   // dernière adresse reconstruite, validée ou non
    uint32_t rejectedBursts;    // rafales sans canal 1 exploitable
    uint32_t invalidAddresses;  // ADR1/ADR2 incohérents
    uint32_t oversizedBursts;   // rafales trop longues (cutouts fusionnés, parasites)
    uint32_t uartOverflows;     // débordements FIFO / tampon du pilote UART
    uint32_t frameErrors;       // erreurs de trame UART
};

class LocoAddrDecoder
{
public:
    // Valeurs particulières renvoyées par decode4of8()
    static constexpr uint8_t SYMBOL_ACK1 = 64;     // 0x0F
    static constexpr uint8_t SYMBOL_ACK2 = 65;     // 0xF0
    static constexpr uint8_t SYMBOL_BUSY = 66;     // 0xE1
    static constexpr uint8_t SYMBOL_INVALID = 255;

    LocoAddrDecoder();

    // Réglages (à faire avant begin() de la classe LocoAddrReader)
    void setAddressConfirmations(uint8_t n);   // défaut 5
    void setLossTimeoutMs(uint32_t ms);        // défaut 1000
    void setDirectionConfirmations(uint8_t n); // défaut 3
    void setDirectionInverted(bool inverted);  // défaut false

    void reset();

    // Traite une rafale complète (tous les octets reçus pendant un cutout).
    //   dirLevel : niveau lu sur DIR après la rafale (0 ou 1), -1 si non câblé
    //   nowMs    : horloge en millisecondes
    void processBurst(const uint8_t *data, size_t len, int dirLevel, uint32_t nowMs);

    // À appeler régulièrement : détecte la disparition de la locomotive.
    void tick(uint32_t nowMs);

    uint16_t address() const { return m_validatedAddress; }
    LocoDirection direction() const;

    LocoAddrStats &stats() { return m_stats; }
    const LocoAddrStats &stats() const { return m_stats; }

    // Outils publics (utilisés aussi par les tests)
    static uint8_t decode4of8(uint8_t raw);
    static bool buildAddress(uint8_t adr1, uint8_t adr2, uint16_t &address);

private:
    // Nombre maximal de rafales entre ADR1 et ADR2 pour les associer.
    // Le décodeur alterne ADR1 et ADR2 d'un cutout au suivant.
    static constexpr uint32_t MAX_PAIR_GAP = 2;

    // Octets invalides tolérés en tête de rafale (parasite au début du cutout)
    static constexpr size_t MAX_LEADING_GARBAGE = 2;

    bool takeAddressDatagram(const uint8_t *data, size_t len, size_t pos);
    void scanChannel2(const uint8_t *data, size_t len, size_t pos);
    void acceptAddress(uint16_t address, uint32_t nowMs);
    void rejectAddress();
    void sampleDirection(int dirLevel);
    void clearDirection();

    uint8_t m_addressConfirmations;
    uint32_t m_lossTimeoutMs;
    uint8_t m_directionConfirmations;
    bool m_directionInverted;

    uint32_t m_burstSeq;

    uint8_t m_adr1;
    uint8_t m_adr2;
    uint32_t m_adr1Seq;
    uint32_t m_adr2Seq;
    bool m_hasAdr1;
    bool m_hasAdr2;

    uint16_t m_candidateAddress;
    uint8_t m_candidateCount;

    uint16_t m_validatedAddress;
    uint32_t m_lastSeenMs;

    LocoDirection m_direction;
    LocoDirection m_directionCandidate;
    uint8_t m_directionCount;

    LocoAddrStats m_stats;
};

#endif
