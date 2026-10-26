/*
   DCC_LocoAddr.h - DCC-LocoAddr v5.1.0

   Lecture de l'adresse DCC des locomotives avec un détecteur Locoduino
   LocoAddr (carte v3.1) : décodeurs compatibles RailCom®, canal 1.

   Microcontrôleurs pris en charge :
     - ESP32 (classique, S2, S3, C3, C6, H2) : un UART matériel par détecteur ;
     - RP2040 et RP2350 (Raspberry Pi Pico, Pico W, Pico 2) : UART matériels
       et récepteurs PIO, jusqu'à 10 détecteurs (RP2040) ou 14 (RP2350) ;
     - AVR : Arduino Mega 2560, jusqu'à 3 détecteurs sur Serial1,
       Serial2 et Serial3.

   La classe s'appelle LocoAddrReader sur toutes les cartes. Seul son
   constructeur change, car les broches et les UART se désignent différemment.

   RailCom® est une marque déposée de Lenz Elektronik GmbH.

   © Christophe BOBILLE - Locoduino - GPL-3.0
   https://github.com/BOBILLEChristophe/DCC-LocoAddr
*/

#ifndef DCC_LOCOADDR_H
#define DCC_LOCOADDR_H

#define DCC_LOCOADDR_VERSION "5.1.0"

#if defined(ARDUINO_ARCH_ESP32)
#include "LocoAddrReader_ESP32.h"
#elif defined(ARDUINO_ARCH_RP2040)
#include "LocoAddrReader_RP2040.h"
#elif defined(ARDUINO_ARCH_AVR)
#include "LocoAddrReader_AVR.h"
#else
#error "DCC-LocoAddr : carte non prise en charge"
#endif

#endif
