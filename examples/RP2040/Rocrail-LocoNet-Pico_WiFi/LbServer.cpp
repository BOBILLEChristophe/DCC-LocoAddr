/*
   LbServer.cpp - serveur "LocoNet over TCP" (protocole LbServer)

   © Christophe BOBILLE - Locoduino - GPL-3.0
*/

#include "LbServer.h"

#if defined(ARDUINO_ARCH_ESP32)
#include <lwip/sockets.h> // SO_KEEPALIVE, TCP_KEEPIDLE...
#endif

LbServer::LbServer(uint16_t port) : server_(port), port_(port)
{
}

void LbServer::begin()
{
  server_.begin();
  server_.setNoDelay(true); // chaque message part tout de suite (pas d'algorithme de Nagle)
}

void LbServer::onConnect(void (*callback)())
{
  onConnect_ = callback;
}

void LbServer::setKeepalive(int idleSec, int intervalSec, int count)
{
  kaIdle_ = idleSec;
  kaInterval_ = intervalSec;
  kaCount_ = count;
}

bool LbServer::isConnected()
{
  return client_ && client_.connected();
}

void LbServer::update()
{
  acceptClient();

  if (!client_)
  {
    return;
  }
  if (!client_.connected())
  {
    if (verbose_)
    {
      Serial.println("Client deconnecte");
    }
    client_.stop();
    return;
  }
  readLines();
}

// Une nouvelle connexion remplace l'éventuelle précédente
void LbServer::acceptClient()
{
  WiFiClient incoming = server_.accept(); // client vide (false) si personne de nouveau
  if (!incoming)
  {
    return;
  }

  if (isConnected())
  {
    if (verbose_)
    {
      Serial.println("Nouveau client : l'ancien est deconnecte");
    }
    client_.stop();
  }
  client_ = incoming;
  client_.setNoDelay(true);
  enableKeepalive();
  used_ = 0; // oublie un morceau de ligne laissé par l'ancienne connexion

  if (verbose_)
  {
    Serial.printf("\nClient connecte depuis %s\n", client_.remoteIP().toString().c_str());
  }
  sendLine("VERSION DCC-LocoAddr LbServer");

  if (onConnect_ != nullptr)
  {
    onConnect_();
  }
}

// Reconstitue les lignes, quel que soit le découpage des données à l'arrivée
void LbServer::readLines()
{
  while (client_.available())
  {
    const char c = client_.read();
    if ((c == '\n') || (c == '\r'))
    {
      if (used_ > 0)
      {
        buf_[used_] = '\0';
        handleLine(buf_);
        used_ = 0;
      }
    }
    else if (used_ < sizeof(buf_) - 1)
    {
      buf_[used_++] = c;
    }
  }
}

// "SEND xx xx ..." -> "SENT OK" (+ écho "RECEIVE xx xx ...", comme un vrai bus)
void LbServer::handleLine(char *line)
{
  if (verbose_)
  {
    Serial.printf("<- %s\n", line);
  }
  if (strncmp(line, "SEND", 4) != 0)
  {
    return;
  }

  uint8_t msg[32];
  size_t len = 0;
  char *p = line + 4;
  while ((*p != '\0') && (len < sizeof(msg)))
  {
    char *end;
    const long v = strtol(p, &end, 16);
    if (end == p)
    {
      break;
    }
    msg[len++] = (uint8_t)v;
    p = end;
  }

  sendLine("SENT OK");
  if (echo_ && (len > 0))
  {
    sendMessage(msg, len);
  }
}

void LbServer::sendMessage(const uint8_t *msg, size_t len)
{
  char line[8 + 3 * 32];
  size_t pos = snprintf(line, sizeof(line), "RECEIVE");
  for (size_t i = 0; (i < len) && (pos < sizeof(line) - 4); ++i)
  {
    pos += snprintf(line + pos, sizeof(line) - pos, " %02X", msg[i]);
  }
  sendLine(line);
}

// La ligne et sa fin "\r\n" partent en une seule écriture
void LbServer::sendLine(const char *line)
{
  if (verbose_)
  {
    Serial.printf("   -> %s\n", line);
  }
  if (!isConnected())
  {
    return;
  }
  char out[160];
  const int n = snprintf(out, sizeof(out), "%s\r\n", line);
  client_.write((const uint8_t *)out, (n < (int)sizeof(out)) ? n : sizeof(out) - 1);
}

void LbServer::enableKeepalive()
{
#if defined(ARDUINO_ARCH_ESP32)
  const int on = 1;
  client_.setSocketOption(SOL_SOCKET, SO_KEEPALIVE, &on, sizeof(on));
  client_.setSocketOption(IPPROTO_TCP, TCP_KEEPIDLE, &kaIdle_, sizeof(kaIdle_));
  client_.setSocketOption(IPPROTO_TCP, TCP_KEEPINTVL, &kaInterval_, sizeof(kaInterval_));
  client_.setSocketOption(IPPROTO_TCP, TCP_KEEPCNT, &kaCount_, sizeof(kaCount_));
#elif defined(ARDUINO_ARCH_RP2040)
  client_.keepAlive(kaIdle_, kaInterval_, kaCount_);
#endif
}
