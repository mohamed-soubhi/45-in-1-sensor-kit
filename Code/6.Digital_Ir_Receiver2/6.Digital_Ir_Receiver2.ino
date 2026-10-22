// Note: This sketch uses IRremote 2.x / 3.x / 4.x compatibility
#include <IRremote.h>

const int RECV_PIN = 11;
const int LED_PIN = 13;

#if defined(IR_RECEIVE_PIN) || defined(DECODE_NEC)
// IRremote 4.x Modern API
void setup() {
  Serial.begin(9600);
  pinMode(LED_PIN, OUTPUT);
  IrReceiver.begin(RECV_PIN, ENABLE_LED_FEEDBACK);
  Serial.println("IR Receiver Ready (IRremote 4.x).");
}

void loop() {
  if (IrReceiver.decode()) {
    if (IrReceiver.decodedIRData.command == 0x1) {
      digitalWrite(LED_PIN, HIGH);
    } else {
      digitalWrite(LED_PIN, LOW);
    }
    Serial.print("Protocol: ");
    Serial.print(IrReceiver.getProtocolString());
    Serial.print(" | Command: 0x");
    Serial.println(IrReceiver.decodedIRData.command, HEX);
    IrReceiver.resume();
  }
}
#else
// Legacy IRremote 2.x API fallback
IRrecv irrecv(RECV_PIN);
decode_results results;

void setup() {
  Serial.begin(9600);
  pinMode(LED_PIN, OUTPUT);
  irrecv.enableIRIn(); // Start the receiver
}

void loop() {
  if (irrecv.decode(&results)) {
    if (results.bits > 0) {
      int state = (results.value == 0x1) ? HIGH : LOW;
      digitalWrite(LED_PIN, state);
    }
    irrecv.resume();
  }
}
#endif
