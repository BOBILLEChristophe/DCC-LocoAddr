/*
   Transponder.h - transpondage LocoNet (Digitrax OPC_MULTI_SENSE, 0xD0)

   Retient la loco présente dans chaque canton et envoie, à chaque changement,
   "loco absente" pour l'ancienne puis "loco présente" pour la nouvelle.
   Rocrail en tire le code de la loco (code = adresse DCC).

   La classe ne s'occupe que des cantons et des messages LocoNet : l'envoi
   passe par un LbServer.

   Message OPC_MULTI_SENSE (6 octets) :
     [0] D0
     [1] 0x20 présente / 0x00 absente  +  bits 0-4 = (canton - 1) / 128
     [2] (canton - 1) % 128
     [3] adresse loco, partie haute : 0x7D pour une adresse courte (< 128),
         sinon adresse / 128
     [4] adresse loco, partie basse : adresse courte, ou adresse % 128
     [5] somme de contrôle : XOR de tous les octets = 0xFF
   Rocrail ajoute 1 à la zone : le canton 1 correspond au capteur d'adresse 1.

   © Christophe BOBILLE - Locoduino - GPL-3.0
*/

#ifndef TRANSPONDER_H
#define TRANSPONDER_H

#include <Arduino.h>
#include "LbServer.h"

class Transponder
{
public:
  Transponder(LbServer &server, uint16_t nbCantons);
  ~Transponder();

  // Nouvelle loco dans le canton (0 = canton libre). N'envoie rien si rien ne change.
  void setCanton(uint16_t canton, uint16_t address);

  // Adresse de la loco présente dans le canton (0 = libre)
  uint16_t locoIn(uint16_t canton) const;

  // Renvoie "présente" pour tous les cantons occupés (après une connexion)
  void resendAll();

  // Affiche chaque changement sur le moniteur série (défaut true)
  void setVerbose(bool verbose) { verbose_ = verbose; }

private:
  void send(uint16_t canton, uint16_t address, bool present);

  LbServer &server_;
  uint16_t nbCantons_;
  uint16_t *cantons_; // indices 1 à nbCantons_ (l'indice 0 n'est pas utilisé)
  bool verbose_ = true;
};

#endif
