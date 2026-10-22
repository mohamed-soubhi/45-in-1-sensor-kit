int ThermistorPin = A0;
int Vo;
float R1 = 10000.0; // value of R1 (10k resistor on module)
float logR2, R2, T;
float c1 = 0.001129148, c2 = 0.000234125, c3 = 0.0000000876741; // Steinhart-Hart coefficients

void setup() {
  Serial.begin(9600);
}

void loop() {
  Vo = analogRead(ThermistorPin);
  if (Vo <= 0) Vo = 1;
  if (Vo >= 1023) Vo = 1022;

  R2 = R1 / ((1023.0 / (float)Vo) - 1.0);
  logR2 = log(R2);
  T = (1.0 / (c1 + c2 * logR2 + c3 * logR2 * logR2 * logR2)); // temperature in Kelvin
  T = T - 273.15; // convert Kelvin to Celsius

  Serial.print("Temperature: "); 
  Serial.print(T, 1);
  Serial.println(" C"); 
  delay(500);
}
