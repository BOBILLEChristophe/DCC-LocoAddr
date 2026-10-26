/*
   LbServer.h - serveur "LocoNet over TCP" (protocole LbServer)

   Rocrail (ou JMRI) se connecte à ce serveur comme client. Le protocole est
   textuel, une ligne par message, fin de ligne CR LF :
     serveur -> client : VERSION texte | RECEIVE D0 20 00 32 00 3D | SENT OK
     client -> serveur : SEND 82 7D
   Les octets sont en hexadécimal, séparés par des espaces.

   La classe ne s'occupe que du réseau : elle ne connaît ni les locos ni les
   cantons. Elle accepte un seul client à la fois ("le dernier arrivé gagne").

   Fonctionne sur ESP32 et sur Raspberry Pi Pico W (cœur d'Earle Philhower).

   © Christophe BOBILLE - Locoduino - GPL-3.0
*/

#ifndef LBSERVER_H
#define LBSERVER_H

#include <Arduino.h>
#include <WiFi.h>

class LbServer
{
public:
  explicit LbServer(uint16_t port);

  // Démarre l'écoute du port (après la connexion Wi-Fi)
  void begin();

  // À appeler à chaque tour de loop() : nouvelles connexions, lecture des lignes
  void update();

  // Envoie un message LocoNet au client, sous la forme "RECEIVE xx xx ..."
  void sendMessage(const uint8_t *msg, size_t len);

  // Fonction appelée à chaque nouvelle connexion (pour renvoyer l'état complet)
  void onConnect(void (*callback)());

  bool isConnected();

  // Réglages facultatifs, à appeler avant begin()
  void setEcho(bool echo) { echo_ = echo; }    // renvoie au client ce qu'il envoie
  void setVerbose(bool verbose) { verbose_ = verbose; } // affiche les échanges
  void setKeepalive(int idleSec, int intervalSec, int count);

private:
  void acceptClient();
  void readLines();
  void handleLine(char *line);
  void sendLine(const char *line);
  void enableKeepalive();

  WiFiServer server_;
  WiFiClient client_;
  uint16_t port_;

  char buf_[128];   // ligne en cours de réception : elle peut arriver en plusieurs morceaux
  size_t used_ = 0; // nombre de caractères déjà reçus dans buf_

  void (*onConnect_)() = nullptr;
  bool echo_ = true;
  bool verbose_ = true;

  // Keepalive TCP : après kaIdle_ s de silence, une sonde toutes les kaInterval_ s ;
  // sans réponse après kaCount_ sondes, la connexion est déclarée morte.
  int kaIdle_ = 5;
  int kaInterval_ = 2;
  int kaCount_ = 3;
};

#endif
