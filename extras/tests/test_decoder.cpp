// Tests du décodeur LocoAddr sur PC (sans ESP32) :
//   g++ -std=c++11 -Wall -Wextra -I../../src test_decoder.cpp ../../src/LocoAddrDecoder.cpp -o t && ./t
#include <cstdio>
#include <cstdlib>
#include <vector>
#include "LocoAddrDecoder.h"

static const uint8_t ENC[64] = {
    0xAC, 0xAA, 0xA9, 0xA5, 0xA3, 0xA6, 0x9C, 0x9A, 0x99, 0x95, 0x93, 0x96, 0x8E, 0x8D, 0x8B, 0xB1,
    0xB2, 0xB4, 0xB8, 0x74, 0x72, 0x6C, 0x6A, 0x69, 0x65, 0x63, 0x66, 0x5C, 0x5A, 0x59, 0x55, 0x53,
    0x56, 0x4E, 0x4D, 0x4B, 0x47, 0x71, 0xE8, 0xE4, 0xE2, 0xD1, 0xC9, 0xC5, 0xD8, 0xD4, 0xD2, 0xCA,
    0xC6, 0xCC, 0x78, 0x17, 0x1B, 0x1D, 0x1E, 0x2E, 0x36, 0x3A, 0x27, 0x2B, 0x2D, 0x35, 0x39, 0x33};
static const uint8_t ACK = 0xF0;

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("ECHEC ligne %d : %s\n", __LINE__, #c); ++failures; } } while (0)

typedef std::vector<uint8_t> Bytes;

// Datagramme de 12 bits (ID 4 bits + 8 bits) -> 2 octets 4/8
static Bytes dg12(uint8_t id, uint8_t data)
{
    return Bytes{ENC[(id << 2) | (data >> 6)], ENC[data & 0x3F]};
}
// Datagramme de 18 bits (ID 4 bits + 14 bits) -> 3 octets
static Bytes dg18(uint8_t id, uint16_t data)
{
    return Bytes{ENC[(id << 2) | ((data >> 12) & 3)], ENC[(data >> 6) & 0x3F], ENC[data & 0x3F]};
}
static Bytes cat(Bytes a, const Bytes &b) { a.insert(a.end(), b.begin(), b.end()); return a; }

struct Sim
{
    LocoAddrDecoder d;
    uint32_t t = 0;
    void burst(const Bytes &b, int dir = -1) { t += 8; d.processBurst(b.data(), b.size(), dir, t); d.tick(t); }
    // n cutouts d'une loco : ADR1 et ADR2 alternés, canal 2 optionnel
    void loco(uint8_t a1, uint8_t a2, int n, int dir = -1, const Bytes &ch2 = Bytes())
    {
        for (int i = 0; i < n; ++i)
            burst(cat((i & 1) ? dg12(2, a2) : dg12(1, a1), ch2), dir);
    }
    void silence(uint32_t ms) { t += ms; d.tick(t); }
};

int main()
{
    // 1. Table : chaque code décode vers sa valeur, 70 octets valides au total
    int valid = 0;
    for (int i = 0; i < 64; ++i) CHECK(LocoAddrDecoder::decode4of8(ENC[i]) == i);
    for (int i = 0; i < 256; ++i) valid += LocoAddrDecoder::decode4of8(i) != 255;
    CHECK(valid == 67);

    // 2. buildAddress
    uint16_t a;
    CHECK(LocoAddrDecoder::buildAddress(0x00, 12, a) && a == 12);
    CHECK(!LocoAddrDecoder::buildAddress(0x00, 0x8C, a));
    CHECK(!LocoAddrDecoder::buildAddress(0x00, 0, a));
    CHECK(LocoAddrDecoder::buildAddress(0x60, 0x85, a) && a == 5);        // consist inversé
    CHECK(LocoAddrDecoder::buildAddress(0x80 | 0x19, 0x00, a) && a == 6400);
    CHECK(!LocoAddrDecoder::buildAddress(0x80 | 0x3F, 0xFF, a));           // 16383 > 10239
    CHECK(!LocoAddrDecoder::buildAddress(0x80, 0x05, a));                  // longue < 128
    CHECK(!LocoAddrDecoder::buildAddress(0x20, 0x05, a));                  // ADR1 invalide

    // 3. Adresse courte : validée après 5 reconstructions (6 cutouts)
    { Sim s; s.loco(0, 3, 5); CHECK(s.d.address() == 0); s.loco(0, 3, 1); CHECK(s.d.address() == 3); }

    // 4. Adresse longue avec canal 2 rempli d'ACK
    { Sim s; s.loco(0x80 | 0x19, 0x00, 8, -1, Bytes(6, ACK)); CHECK(s.d.address() == 6400); }

    // 5. Consist : le bit de sens est ignoré
    { Sim s; s.loco(0x60, 0x85, 8); CHECK(s.d.address() == 5); }

    // 6. Canal 2 seul (DYN, ID 7) dont les octets suivants ressemblent à ADR1/ADR2 :
    //    l'ancien parseur en flux continu aurait pu y lire une adresse.
    {
        Sim s;
        // données choisies pour que les 2e/3e symboles aient un "ID" 1 puis des données
        Bytes ch2 = cat(dg18(7, (1 << 10) | 5), Bytes(3, ACK)); // 6 octets
        for (int i = 0; i < 50; ++i) s.burst(ch2);
        CHECK(s.d.address() == 0);
        CHECK(s.d.stats().rejectedBursts == 50);
    }

    // 7. Perte après 1 s, puis réapparition
    {
        Sim s; s.loco(0, 7, 8); CHECK(s.d.address() == 7);
        s.silence(990); CHECK(s.d.address() == 7);
        s.silence(20); CHECK(s.d.address() == 0);
        s.loco(0, 7, 6); CHECK(s.d.address() == 7);
    }

    // 8. Une adresse validée est maintenue par chaque reconstruction correcte
    { Sim s; s.loco(0, 9, 8); for (int i = 0; i < 300; ++i) s.loco(0, 9, 2); CHECK(s.d.address() == 9); }

    // 9. Changement de loco : la nouvelle adresse remplace l'ancienne après 5 confirmations
    { Sim s; s.loco(0, 9, 8); s.loco(0, 44, 7); CHECK(s.d.address() == 44); }

    // 10. Moitiés trop éloignées : pas d'association (cutouts perdus entre ADR1 et ADR2)
    {
        Sim s;
        for (int i = 0; i < 20; ++i) { s.burst(dg12(1, 0)); s.burst(Bytes{0x00}); s.burst(Bytes{0x00}); s.burst(dg12(2, 3)); s.burst(Bytes{0x00}); s.burst(Bytes{0x00}); }
        CHECK(s.d.address() == 0);
    }

    // 11. Octet parasite en tête de cutout : toléré
    { Sim s; for (int i = 0; i < 8; ++i) s.burst(cat(Bytes{0x00}, (i & 1) ? dg12(2, 3) : dg12(1, 0))); CHECK(s.d.address() == 3); }

    // 12. Sens de pose
    {
        Sim s;
        s.loco(0, 3, 8, 0); CHECK(s.d.direction() == LocoDirection::Normal);
        s.loco(0, 3, 2, 1); CHECK(s.d.direction() == LocoDirection::Normal);   // 2 < 3 lectures
        s.loco(0, 3, 1, 1); CHECK(s.d.direction() == LocoDirection::Reversed);
        s.silence(1100); CHECK(s.d.direction() == LocoDirection::Unknown);       // loco partie
    }
    { Sim s; s.d.setDirectionInverted(true); s.loco(0, 3, 8, 0); CHECK(s.d.direction() == LocoDirection::Reversed); }
    { Sim s; s.loco(0, 3, 8, -1); CHECK(s.d.direction() == LocoDirection::Unknown); } // option absente

    // 12b. ADR1 en canal 1 et ADR2 en tête du canal 2, dans chaque cutout
    {
        Sim s;
        for (int i = 0; i < 4; ++i) s.burst(cat(cat(dg12(1, 0), dg12(2, 12)), Bytes(4, ACK)));
        CHECK(s.d.address() == 0);
        s.burst(cat(cat(dg12(1, 0), dg12(2, 12)), Bytes(4, ACK)));
        CHECK(s.d.address() == 12);
    }
    // 12c. Même cas en adresse longue, sans ACK de remplissage
    { Sim s; for (int i = 0; i < 6; ++i) s.burst(cat(dg12(1, 0x80 | 0x19), dg12(2, 0x00))); CHECK(s.d.address() == 6400); }

    // 12d. ADR2 dans le canal 2 après des ACK, puis après un DYN
    { Sim s; for (int i = 0; i < 6; ++i) s.burst(cat(cat(dg12(1, 0), Bytes{ACK, 0x0F}), cat(dg12(2, 3), Bytes{ACK, ACK}))); CHECK(s.d.address() == 3); }
    { Sim s; for (int i = 0; i < 6; ++i) s.burst(cat(cat(dg12(1, 0), dg18(7, 0x123)), cat(dg12(2, 5), Bytes{ACK}))); CHECK(s.d.address() == 5); }

    // 13. Parasites aléatoires : jamais d'adresse
    {
        Sim s; std::srand(1);
        for (int i = 0; i < 20000; ++i) { Bytes b(1 + std::rand() % 8); for (auto &x : b) x = std::rand() & 0xFF; s.burst(b); }
        std::printf("parasites : adresse=%u, datagrammes=%u, rejetées=%u\n", s.d.address(), s.d.stats().ch1Datagrams, s.d.stats().rejectedBursts);
        CHECK(s.d.address() == 0);
    }

    std::printf(failures ? "%d ECHEC(S)\n" : "Tous les tests passent\n", failures);
    return failures ? 1 : 0;
}
