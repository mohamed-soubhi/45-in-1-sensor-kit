int buzzer = 3; // digital IO pin for the buzzer

void setup() { 
  pinMode(buzzer, OUTPUT); 
} 

void loop() { 
  // High pitch tone (approx 500 Hz)
  for (int i = 0; i < 80; i++) { 
    digitalWrite(buzzer, HIGH);
    delay(1); 
    digitalWrite(buzzer, LOW);
    delay(1); 
  } 

  // Lower pitch tone (approx 250 Hz)
  for (int i = 0; i < 100; i++) { 
    digitalWrite(buzzer, HIGH);
    delay(2);
    digitalWrite(buzzer, LOW);
    delay(2); 
  }
}
