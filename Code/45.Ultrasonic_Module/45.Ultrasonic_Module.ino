int echoPin = 4; // ultrasonic signal receiver pin ECHO to D4 
int trigPin = 5; // ultrasonic signal transmitter pin TRIG to D5

void setup() {
  Serial.begin(9600); 
  pinMode(echoPin, INPUT); 
  pinMode(trigPin, OUTPUT);
}

void loop() {
  digitalWrite(trigPin, LOW); 
  delayMicroseconds(2);
  digitalWrite(trigPin, HIGH); // Pulse for 10us to trigger ultrasonic detection
  delayMicroseconds(10); 
  digitalWrite(trigPin, LOW);

  // 30ms timeout corresponds to approx 5 meters max distance
  unsigned long duration = pulseIn(echoPin, HIGH, 30000); 

  if (duration == 0) {
    Serial.println("Out of range / No echo");
  } else {
    float distance = (float)duration / 58.0; // Transform pulse time (us) to cm
    Serial.print("Distance: ");
    Serial.print(distance, 1);
    Serial.println(" cm");
  }

  delay(60); // 60ms between pings prevents ultrasonic echo confusion
}
