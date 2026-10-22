#include <OneWire.h> 

int DS18B20_Pin = 2; // DS18B20 Signal pin on digital pin 2

OneWire ds(DS18B20_Pin);

void setup(void) {
  Serial.begin(9600);
}

void loop(void) {
  float temperature = getTemp();
  if (temperature != -1000.0) {
    Serial.print("Temperature: ");
    Serial.print(temperature);
    Serial.println(" C");
  }
  delay(1000);
}

float getTemp() {
  byte data[12];
  byte addr[8];

  if (!ds.search(addr)) {
    ds.reset_search();
    return -1000.0;
  }

  if (OneWire::crc8(addr, 7) != addr[7]) {
    Serial.println("CRC is not valid!");
    return -1000.0;
  }

  if (addr[0] != 0x10 && addr[0] != 0x28) {
    Serial.println("Device is not recognized as a DS18x20 family sensor");
    return -1000.0;
  }

  ds.reset();
  ds.select(addr);
  ds.write(0x44, 1); // start conversion, parasite power on
  delay(750);        // 750ms required for 12-bit temperature conversion

  byte present = ds.reset();
  (void)present;
  ds.select(addr);  
  ds.write(0xBE);    // Read Scratchpad

  for (int i = 0; i < 9; i++) {
    data[i] = ds.read();
  }
  ds.reset_search();

  int16_t raw = ((int16_t)data[1] << 8) | data[0];
  if (addr[0] == 0x10) {
    // DS18S20: 9-bit resolution (0.5 C / LSB)
    raw = raw << 3; // 12-bit standard
    if (data[7] == 0x10) {
      raw = (raw & 0xFFF0) + 12 - data[6];
    }
    return (float)raw * 0.0625;
  } else {
    // DS18B20: 12-bit resolution
    return (float)raw / 16.0;
  }
}
