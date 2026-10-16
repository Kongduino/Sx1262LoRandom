// Measures the SX1262 random number register (0x0819) on a RAK4631: how often it changes,
// how often back-to-back reads repeat, whether the first read after SetRx is stale, and bit
// bias, with and without Semtech's LNA/mixer settings. Receive only: nothing is transmitted.
//
// Needs RadioLib (https://github.com/jgromes/RadioLib, tested with 7.7.1) for raw register
// access; Sx1262LoRandom itself uses SX126x-Arduino. For other boards, change the pins below.
//
// Results on a RAK4631, October 2026:
// - power-on REG_ANA_LNA 0x0C, REG_ANA_MIXER 0x20 (LNA bit 0 and mixer bit 7 already 0)
// - standby: the register never changes (999/999 repeats)
// - receive: a new value on every read (one 4-byte read every 166 us), no repeats at any
//   spacing from 0 to 1000 us, and the first read after SetRx is never stale
// - about 1% more 0 bits than 1 bits (0.490-0.494 ones over 131072 bits), with or without
//   the LNA/mixer settings
#include <Arduino.h>
#include <Adafruit_TinyUSB.h>  // Serial on nRF52 cores based on Adafruit's
#include <RadioLib.h>

// RAK4631 SX1262 pins (Meshtastic variants/nrf52840/rak4631/variant.h).
#define PIN_NSS 42
#define PIN_DIO1 47
#define PIN_RESET 38
#define PIN_BUSY 46
#define PIN_POWER_EN 37
#define REG_RANDOM 0x0819
#define REG_ANA_LNA 0x08E2
#define REG_ANA_MIXER 0x08E5

// readRegister()/writeRegister() are protected in RadioLib.
class ProbeSX1262 : public SX1262 {
public:
  using SX1262::SX1262;
  uint8_t read8(uint16_t addr) {
    uint8_t v = 0;
    readRegister(addr, &v, 1);
    return v;
  }
  void write8(uint16_t addr, uint8_t v) {
    writeRegister(addr, &v, 1);
  }
  uint32_t readRandom() {
    uint8_t b[4];
    readRegister(REG_RANDOM, b, 4);
    return ((uint32_t)b[0] << 24) | ((uint32_t)b[1] << 16) | ((uint32_t)b[2] << 8) | b[3];
  }
};

ProbeSX1262 radio = new Module(PIN_NSS, PIN_DIO1, PIN_RESET, PIN_BUSY);
static uint32_t words[4096];

static void waitMicros(uint32_t us) {
  if (us) delayMicroseconds(us);
}

// Consecutive identical words among n reads spaced by `spacing` microseconds.
static uint32_t repeats(uint32_t spacing, uint32_t n, uint32_t* elapsedUs) {
  uint32_t prev = radio.readRandom(), same = 0, t0 = micros();
  for (uint32_t i = 1; i < n; i++) {
    waitMicros(spacing);
    uint32_t v = radio.readRandom();
    same += (v == prev);
    prev = v;
  }
  if (elapsedUs) *elapsedUs = micros() - t0;
  return same;
}

static void bias(const char* label, uint32_t spacing) {
  for (uint32_t i = 0; i < 4096; i++) {
    waitMicros(spacing);
    words[i] = radio.readRandom();
  }
  uint32_t perBit[32] = { 0 }, total = 0, dup = 0;
  for (uint32_t i = 0; i < 4096; i++) {
    for (int b = 0; b < 32; b++) perBit[b] += (words[i] >> b) & 1;
    total += __builtin_popcount(words[i]);
    if (i && words[i] == words[i - 1]) dup++;
  }
  uint32_t lo = 4096, hi = 0;
  int loBit = 0, hiBit = 0;
  for (int b = 0; b < 32; b++) {
    if (perBit[b] < lo) {
      lo = perBit[b];
      loBit = b;
    }
    if (perBit[b] > hi) {
      hi = perBit[b];
      hiBit = b;
    }
  }
  Serial.printf(
    "  %-34s spacing %4lu us: ones %.4f; per bit min %.3f (bit %d) max %.3f (bit %d); repeats %lu/4095\n",
    label, (unsigned long)spacing, total / (32.0 * 4096), lo / 4096.0, loBit, hi / 4096.0, hiBit,
    (unsigned long)dup
  );
}

void setup() {
  Serial.begin(115200);
  while (!Serial) delay(10);
  delay(500);
  Serial.println("--- SX1262 random register measurement (RAK4631) ---");
  pinMode(PIN_POWER_EN, OUTPUT);
  digitalWrite(PIN_POWER_EN, HIGH);
  delay(10);
  int16_t st = radio.begin(868.0, 125.0, 7, 5, RADIOLIB_SX126X_SYNC_WORD_PRIVATE, 2, 8, 1.8, false);
  if (st != RADIOLIB_ERR_NONE) {
    Serial.printf("radio.begin failed: %d\n", st);
    return;
  }
  radio.setDio2AsRfSwitch(true);
  uint8_t lna = radio.read8(REG_ANA_LNA), mixer = radio.read8(REG_ANA_MIXER);
  Serial.printf("\nPower-on REG_ANA_LNA 0x%02X, REG_ANA_MIXER 0x%02X\n", lna, mixer);
  // 1. Standby: does the register change at all?
  radio.standby();
  uint32_t standbyRepeats = repeats(0, 1000, nullptr);
  Serial.printf("\n1. Standby, 1000 back-to-back reads: %lu/999 repeats\n", (unsigned long)standbyRepeats);
  // 2. Semtech's settings: LNA bit 0 and mixer bit 7 cleared, continuous receive.
  radio.write8(REG_ANA_LNA, lna & ~0x01);
  radio.write8(REG_ANA_MIXER, mixer & ~0x80);
  radio.startReceive();
  delay(10);
  uint32_t t0 = micros();
  for (int i = 0; i < 1000; i++) radio.readRandom();
  Serial.printf(
    "\n2. Receive, Semtech settings. One 4-byte register read: %.1f us\n", (micros() - t0) / 1000.0
  );
  // How often does the value change when polled flat out?
  uint32_t prev = radio.readRandom(), changes = 0, reads = 0, start = micros();
  while (micros() - start < 2000000) {
    uint32_t v = radio.readRandom();
    reads++;
    if (v != prev) changes++;
    prev = v;
  }
  Serial.printf(
    "   Polled for 2 s: %lu reads, %lu changes -> one change every %.1f us (%.2f reads)\n",
    (unsigned long)reads, (unsigned long)changes, 2000000.0 / changes, (double)reads / changes
  );
  Serial.println("\n   Consecutive repeats by spacing (2000 reads each):");
  const uint32_t spacings[] = { 0, 5, 10, 20, 50, 100, 200, 500, 1000 };
  for (uint32_t s : spacings) {
    uint32_t el = 0, r = repeats(s, 2000, &el);
    Serial.printf("   spacing %4lu us: %4lu/1999 repeats (%.1f%%), %.1f us per word\n", (unsigned long)s,
                  (unsigned long)r, 100.0 * r / 1999, el / 1999.0);
  }
  // Is the first read after SetRx stale (left over from standby)?
  Serial.println("\n   First read after startReceive() vs the last read in standby (50 trials each):");
  const uint32_t waits[] = { 0, 20, 100, 1000 };
  for (uint32_t w : waits) {
    uint32_t stale = 0;
    for (int i = 0; i < 50; i++) {
      radio.standby();
      uint32_t s = radio.readRandom();
      radio.startReceive();
      waitMicros(w);
      stale += (radio.readRandom() == s);
    }
    Serial.printf("   wait %4lu us: %2lu/50 equal to the standby value\n", (unsigned long)w, (unsigned long)stale);
  }
  radio.startReceive();
  delay(10);
  Serial.println("\n3. Bias over 4096 words:");
  bias("Semtech LNA/mixer settings", 0);
  bias("Semtech LNA/mixer settings", 100);
  bias("Semtech LNA/mixer settings", 1000);
  // Same, with the power-on LNA/mixer values.
  radio.standby();
  radio.write8(REG_ANA_LNA, lna);
  radio.write8(REG_ANA_MIXER, mixer);
  radio.startReceive();
  delay(10);
  bias("power-on LNA/mixer values", 0);
  bias("power-on LNA/mixer values", 1000);
  uint32_t el = 0, r = repeats(0, 2000, &el);
  Serial.printf("  power-on values, back-to-back repeats: %lu/1999\n", (unsigned long)r);  radio.standby();
  Serial.printf(
    "\nRestored: REG_ANA_LNA 0x%02X, REG_ANA_MIXER 0x%02X\n", radio.read8(REG_ANA_LNA),
    radio.read8(REG_ANA_MIXER)
  );
  Serial.println("DONE");
}

void loop() {}
