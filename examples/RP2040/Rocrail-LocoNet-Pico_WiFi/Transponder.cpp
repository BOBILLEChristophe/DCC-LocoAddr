/*
   Transponder.cpp - transpondage LocoNet (Digitrax OPC_MULTI_SENSE, 0xD0)

   © Christophe BOBILLE - Locoduino - GPL-3.0
*/

#include "Transponder.h"

namespace
{
constexpr uint8_t OPC_MULTI_SENSE = 0xD0;
constexpr uint8_t MULTI_SENSE_PRESENT = 0x20;
constexpr uint8_t MULTI_SENSE_ABSENT = 0x00;
constexpr uint8_t SHORT_ADDRESS_MARK = 0x7D;
}

Transponder::Transponder(LbServer &server, uint16_t nbCantons)
    : server_(server), nbCantons_(nbCantons), cantons_(new uint16_t[nbCantons + 1]())
{
}

Transponder::~Transponder()
{
  delete[] cantons_;
}

uint16_t Transponder::locoIn(uint16_t canton) const
{
  return ((canton >= 1) && (canton <= nbCantons_)) ? cantons_[canton] : 0;
}

void Transponder::setCanton(uint16_t canton, uint16_t address)
{
  if ((canton < 1) || (canton > nbCantons_))
  {
    return;
  }
  const uint16_t previous = cantons_[canton];
  if (previous == address)
  {
    return;
  }
  if (previous != 0)
  {
    send(canton, previous, false); // l'ancienne loco quitte le canton
  }
  cantons_[canton] = address;
  if (address != 0)
  {
    send(canton, address, true);
  }
}

void Transponder::resendAll()
{
  for (uint16_t i = 1; i <= nbCantons_; ++i)
  {
    if (cantons_[i] != 0)
    {
      send(i, cantons_[i], true);
    }
  }
}

void Transponder::send(uint16_t canton, uint16_t address, bool present)
{
  const uint16_t zone = canton - 1; // Rocrail ajoute 1
  uint8_t m[6];
  m[0] = OPC_MULTI_SENSE;
  m[1] = (present ? MULTI_SENSE_PRESENT : MULTI_SENSE_ABSENT) | ((zone >> 7) & 0x1F);
  m[2] = zone & 0x7F;
  if (address < 128)
  {
    m[3] = SHORT_ADDRESS_MARK;
    m[4] = address;
  }
  else
  {
    m[3] = (address >> 7) & 0x7F;
    m[4] = address & 0x7F;
  }
  m[5] = 0xFF ^ m[0] ^ m[1] ^ m[2] ^ m[3] ^ m[4];

  if (verbose_)
  {
    Serial.printf("canton %u : loco %u %s\n", canton, address, present ? "presente" : "absente");
  }
  server_.sendMessage(m, sizeof(m));
}
