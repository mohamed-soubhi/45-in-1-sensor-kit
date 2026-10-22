#define SERIAL_BAUDRATE 115200
#define CLK_PIN 2
#define DT_PIN 3
#define SW_PIN 4

volatile long count = 0;
volatile bool countChanged = false;
volatile unsigned long lastInterruptTime = 0;
long lastReportedCount = 0;

void rotaryEncoderChanged() {
  unsigned long now = millis();
  if (now - lastInterruptTime < 4) // 4ms contact bounce filter
    return;
  lastInterruptTime = now;

  // DT_PIN state represents forward or reverse rotation
  if (digitalRead(DT_PIN) == HIGH) {
    count++;
  } else {
    count--;
  }
  countChanged = true;
}

void setup() {
  Serial.begin(SERIAL_BAUDRATE);
  pinMode(CLK_PIN, INPUT_PULLUP);
  pinMode(DT_PIN, INPUT_PULLUP);
  pinMode(SW_PIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(CLK_PIN), rotaryEncoderChanged, FALLING);
  Serial.println("Rotary encoder initialized.");
}

void loop() {
  if (countChanged) {
    long currentCount;
    noInterrupts();
    currentCount = count;
    countChanged = false;
    interrupts();
    if (currentCount != lastReportedCount) {
      lastReportedCount = currentCount;
      Serial.print("Count: ");
      Serial.println(currentCount);
    }
  }

  if (digitalRead(SW_PIN) == LOW) {
    delay(20); // debounce switch
    if (digitalRead(SW_PIN) == LOW) {
      noInterrupts();
      count = 0;
      countChanged = false;
      interrupts();
      lastReportedCount = 0;
      Serial.println("Count reset to 0");
      while (digitalRead(SW_PIN) == LOW); // wait for release
      delay(50);
    }
  }
}
