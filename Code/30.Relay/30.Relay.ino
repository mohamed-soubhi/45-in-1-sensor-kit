int Relay = 8; // Connect Relay IN pin to Arduino digital pin 8

void setup() {
  pinMode(Relay, OUTPUT); // Set relay pin as output
  digitalWrite(Relay, HIGH); // Standard kit relay is Active LOW (HIGH = OFF)
}

void loop() {
  digitalWrite(Relay, LOW);  // Turn ON relay (Active LOW)
  delay(1000);
  digitalWrite(Relay, HIGH); // Turn OFF relay
  delay(1000);
}
