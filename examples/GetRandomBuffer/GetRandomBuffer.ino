#include <SX126x-RAK4630.h>
#include "Sx1262LoRandom.h"

uint32_t myFreq = 868e6;
int mySF = 10;
uint8_t myBW = 0;
uint8_t myCR = 5;
uint8_t TxPower = 20;
static RadioEvents_t RadioEvents;
#define LORA_PREAMBLE_LENGTH 8  // Same for Tx and Rx
#define LORA_SYMBOL_TIMEOUT 0 // Symbols
#define LORA_FIX_LENGTH_PAYLOAD_ON false
#define LORA_IQ_INVERSION_ON false
#define RX_TIMEOUT_VALUE 3000
#define TX_TIMEOUT_VALUE 1000

void setup() {
  Serial.begin(115200);
  delay(5000);
  Serial.println("\n\nHello...");
  //  RadioEvents.TxDone = OnTxDone;
  //  RadioEvents.RxDone = OnRxDone;
  //  RadioEvents.TxTimeout = OnTxTimeout;
  //  RadioEvents.RxTimeout = OnRxTimeout;
  //  RadioEvents.RxError = OnRxError;
  //  RadioEvents.CadDone = OnCadDone;
  // Initialize the Radio
  Radio.Init(&RadioEvents);
  // Set Radio channel
  Radio.SetChannel(myFreq);
  // Set Radio RX configuration
  Serial.println("mySF = " + String(mySF));
  Serial.println("myBW = " + String(myBW));
  Serial.println("myCR = " + String(myCR));
  Serial.println("TxPower = " + String(TxPower));
  Radio.Sleep();
  Radio.SetTxConfig(
    MODEM_LORA, TxPower, 0, myBW, mySF, myCR - 4,
    LORA_PREAMBLE_LENGTH, LORA_FIX_LENGTH_PAYLOAD_ON,
    true, 0, 0, LORA_IQ_INVERSION_ON, TX_TIMEOUT_VALUE);
  Radio.SetRxConfig(
    MODEM_LORA, myBW, mySF, myCR - 4, 0, LORA_PREAMBLE_LENGTH,
    LORA_SYMBOL_TIMEOUT, LORA_FIX_LENGTH_PAYLOAD_ON,
    0, true, 0, 0, LORA_IQ_INVERSION_ON, true);
  Serial.println("Starting Radio.Rx");
  Radio.Rx(RX_TIMEOUT_VALUE);
  SX126xSetTxParams(TxPower, RADIO_RAMP_40_US);
}

void loop() {
  fillRandom();
  hexDump(randomStock, 256);
  delay(1000);
}
