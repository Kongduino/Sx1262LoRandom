#ifndef SX1262_LORANDOM_H
#define SX1262_LORANDOM_H

#include <Arduino.h>
#include <SX126x-Arduino.h>

/*
  Random numbers from the SX126x RandomNumberGen register (0x0819), read with the radio in
  continuous receive: in standby the register does not change. Same settings as Semtech's
  SX126xGetRandom() in LoRaMac-node.

  Measured on a RAK4631 (SX1262): in receive the register holds a new value on every read, but
  the output has about 1% more 0 bits than 1 bits. Hash it before using it as key material.

  Everything below is static inline, so this header can be included from several source files.
*/

#ifndef REG_ANA_LNA
#define REG_ANA_LNA 0x08E2
#endif
#ifndef REG_ANA_MIXER
#define REG_ANA_MIXER 0x08E5
#endif

static uint8_t randomStock[256];
static uint8_t randomIndex = 0;

// Fills buf with len random bytes. The radio is put in continuous receive for the duration,
// then back in the mode it was in: receive is resumed with Radio.Rx(0), which keeps the app's
// receive configuration. Returns false if the register stops changing (the radio did not enter
// receive); the rest of buf is then left as it was.
static inline bool fillRandom(uint8_t *buf, size_t len) {
  RadioOperatingModes_t mode = SX126xGetOperatingMode();
  uint8_t regAnaLna = Radio.Read(REG_ANA_LNA);
  uint8_t regAnaMixer = Radio.Read(REG_ANA_MIXER);
  Radio.Write(REG_ANA_LNA, regAnaLna & ~(1 << 0));
  Radio.Write(REG_ANA_MIXER, regAnaMixer & ~(1 << 7));
  // Continuous receive, without Radio.Rx()'s software timeout timer and receive interrupts.
  SX126xSetRx(0xFFFFFF);

  uint8_t word[4], prev[4];
  bool havePrev = false, ok = true;
  uint16_t sameInARow = 0;
  size_t i = 0;
  while (i < len) {
    SX126xReadRegisters(RANDOM_NUMBER_GENERATORBASEADDR, word, 4);
    // A word equal to the previous one was read before the register changed: drop it.
    if (havePrev && memcmp(word, prev, 4) == 0) {
      if (++sameInARow >= 1000) {
        ok = false;
        break;
      }
      continue;
    }
    sameInARow = 0;
    memcpy(prev, word, 4);
    havePrev = true;
    for (uint8_t j = 0; j < 4 && i < len; j++) buf[i++] = word[j];
  }

  Radio.Standby();
  Radio.Write(REG_ANA_LNA, regAnaLna);
  Radio.Write(REG_ANA_MIXER, regAnaMixer);
  if (mode == MODE_RX) Radio.Rx(0);
  else if (mode == MODE_SLEEP) Radio.Sleep();
  memset(word, 0, sizeof(word));
  memset(prev, 0, sizeof(prev));
  return ok;
}

// Fills all of randomStock and resets randomIndex.
static inline bool fillRandom() {
  randomIndex = 0;
  return fillRandom(randomStock, sizeof(randomStock));
}

#ifndef KONGDUINO_HEXDUMP
#define KONGDUINO_HEXDUMP
// Something similar to the Unix/Linux hexdump -C command
// Pretty-prints the contents of a buffer, 16 bytes a row
static inline void hexDump(uint8_t* buf, uint16_t len) {
  char alphabet[17] = "0123456789abcdef";
  uint16_t i, index;
  Serial.print(F("   +------------------------------------------------+ +----------------+\n"));
  Serial.print(F("   |.0 .1 .2 .3 .4 .5 .6 .7 .8 .9 .a .b .c .d .e .f | |      ASCII     |\n"));
  for (i = 0; i < len; i += 16) {
    if (i % 128 == 0) Serial.print(F("   +------------------------------------------------+ +----------------+\n"));
    char s[] = "|                                                | |                |\n";
    // pre-formated line. We will replace the spaces with text when appropriate.
    uint8_t ix = 1, iy = 52, j;
    for (j = 0; j < 16; j++) {
      if (i + j < len) {
        uint8_t c = buf[i + j];
        // fastest way to convert a byte to its 2-digit hex equivalent
        s[ix++] = alphabet[(c >> 4) & 0x0F];
        s[ix++] = alphabet[c & 0x0F];
        ix++;
        if (c > 31 && c < 128) s[iy++] = c;
        else s[iy++] = '.'; // display ASCII code 0x20-0x7F or a dot.
      }
    }
    index = i / 16;
    // display line number then the text
    if (i < 256) Serial.write(' ');
    Serial.print(index, HEX); Serial.write('.');
    Serial.print(s);
  }
  Serial.print(F("   +------------------------------------------------+ +----------------+\n"));
}
#endif // KONGDUINO_HEXDUMP

#endif // SX1262_LORANDOM_H
