/*
   DCC-LocoAddr - Exemple Arduino Mega 2560

   Lecture simultanée de trois détecteurs LocoAddr v3.1.

   Câblage :
     Détecteur 1 OUT -> RX1 / broche 19 / Serial1
     Détecteur 2 OUT -> RX2 / broche 17 / Serial2
     Détecteur 3 OUT -> RX3 / broche 15 / Serial3

   Serial est conservé pour le moniteur série USB.

   Chaque détecteur dispose de sa propre instance LocoAddrReader.
   poll() doit être appelée aussi régulièrement que possible.

   © Christophe BOBILLE - Locoduino - GPL-3.0
*/

#include <DCC_LocoAddr.h>

LocoAddrReader detector1(Serial1);
LocoAddrReader detector2(Serial2);
LocoAddrReader detector3(Serial3);

void setup()
{
    Serial.begin(115200);

    Serial.println();
    Serial.println(F("DCC-LocoAddr - Mega 2560 - 3 detecteurs"));
    Serial.println(F("D1 : Serial1 / RX1 broche 19"));
    Serial.println(F("D2 : Serial2 / RX2 broche 17"));
    Serial.println(F("D3 : Serial3 / RX3 broche 15"));

    detector1.setLossTimeoutMs(1000);
    detector2.setLossTimeoutMs(1000);
    detector3.setLossTimeoutMs(1000);

    detector1.begin();
    detector2.begin();
    detector3.begin();

    Serial.println(F("En attente des locomotives..."));
}

void loop()
{
    detector1.poll();
    detector2.poll();
    detector3.poll();

    static uint16_t previous1 = 0;
    static uint16_t previous2 = 0;
    static uint16_t previous3 = 0;

    const uint16_t address1 = detector1.address();
    const uint16_t address2 = detector2.address();
    const uint16_t address3 = detector3.address();

    if (address1 != previous1)
    {
        Serial.print(F("D1 : "));

        if (address1 == 0)
        {
            Serial.print(F("locomotive "));
            Serial.print(previous1);
            Serial.println(F(" partie"));
        }
        else
        {
            Serial.print(F("locomotive "));
            Serial.println(address1);
        }

        previous1 = address1;
    }

    if (address2 != previous2)
    {
        Serial.print(F("D2 : "));

        if (address2 == 0)
        {
            Serial.print(F("locomotive "));
            Serial.print(previous2);
            Serial.println(F(" partie"));
        }
        else
        {
            Serial.print(F("locomotive "));
            Serial.println(address2);
        }

        previous2 = address2;
    }

    if (address3 != previous3)
    {
        Serial.print(F("D3 : "));

        if (address3 == 0)
        {
            Serial.print(F("locomotive "));
            Serial.print(previous3);
            Serial.println(F(" partie"));
        }
        else
        {
            Serial.print(F("locomotive "));
            Serial.println(address3);
        }

        previous3 = address3;
    }
}
