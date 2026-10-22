const int xPin = A0;
const int yPin = A1;
const int swPin = 2;

void setup() {
  Serial.begin(9600);
  pinMode(swPin, INPUT_PULLUP);
}

void loop() {
  int xVal = analogRead(xPin);
  int yVal = analogRead(yPin);
  int swVal = digitalRead(swPin);

  Serial.print("X: ");
  Serial.print(xVal);
  Serial.print(" | Y: ");
  Serial.print(yVal);
  Serial.print(" | Button: ");
  Serial.println(swVal == LOW ? "PRESSED" : "RELEASED");

  delay(100);
}
