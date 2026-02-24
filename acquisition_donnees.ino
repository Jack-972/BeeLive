#include <Wire.h>
#include <DHT.h>
#include <OneWire.h>
#include <DallasTemperature.h>

// --- IDENTIFIANTS LORAWAN ---
const char *devEui = "CDEADAEEACFADEEA"; 
const char *appEui = "0000000000000000";
const char *appKey = "0FF6C98BEE426A6DD4C1CBE40804A4C3"; 

// --- PINS ---
#define DHT_INT_PIN 27
#define DHT_EXT_PIN 2
#define ONE_WIRE_BUS 14
#define SDA_PIN 21
#define SCL_PIN 22

DHT dht_int(DHT_INT_PIN, DHT22);
DHT dht_ext(DHT_EXT_PIN, DHT22);
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);

void sendAT(String command) {
  Serial.println(command); 
  delay(1000); 
}

void setup() {
  Serial.begin(9600); 
  delay(2000);
  
  // Initialisation I2C pour le SEN0390
  Wire.begin(SDA_PIN, SCL_PIN);
  
  dht_int.begin();
  dht_ext.begin();
  sensors.begin();

  // CONFIGURATION LORA
  sendAT("AT+MODE=LWOTAA");
  sendAT("AT+JOIN"); 
  delay(15000); 
}

void loop() {
  // 1. LECTURE DHT & MOB
  sensors.requestTemperatures();
  float ti = dht_int.readTemperature();
  float hi = dht_int.readHumidity();
  float te = dht_ext.readTemperature();
  float he = dht_ext.readHumidity();
  float tm1 = sensors.getTempCByIndex(0);
  float tm2 = sensors.getTempCByIndex(1);

  // 2. LECTURE LUX (I2C)
  uint16_t lux_val = readSEN0390();

  // 3. CONVERSIONS
  if (isnan(ti)) ti = 0; if (isnan(hi)) hi = 0;
  if (isnan(te)) te = 0; if (isnan(he)) he = 0;
  if (tm1 == DEVICE_DISCONNECTED_C) tm1 = 0;
  if (tm2 == DEVICE_DISCONNECTED_C) tm2 = 0;

  int16_t ti_v = (int16_t)(ti * 10);
  uint16_t hi_v = (uint16_t)(hi * 10);
  int16_t te_v = (int16_t)(te * 10);
  uint8_t he_v = (uint8_t)he;
  int16_t tm1_v = (int16_t)(tm1 * 100);
  int16_t tm2_v = (int16_t)(tm2 * 100);

  // 4. CONSTRUCTION TRAME (17 octets)
  // Payload format: Bat(1), Alert(1), Poids(2), MOB1(2), MOB2(2), DHTiT(2), DHTiH(2), DHTeT(2), DHTeH(1), Lux(2)
  char payload[35];
  sprintf(payload, "64000000%04X%04X%04X%04X%04X%02X%04X",
          (uint16_t)tm1_v, (uint16_t)tm2_v, (uint16_t)ti_v, hi_v, (uint16_t)te_v, he_v, lux_val);

  // 5. ENVOI
  Serial.print("AT+MSGHEX=\"");
  Serial.print(payload);
  Serial.print("\"\r\n");

  delay(10000); 
}

uint16_t readSEN0390() {
  uint16_t level = 0;
  Wire.beginTransmission(0x23); // Adresse I2C par défaut du BH1750
  Wire.write(0x10);             // Mode Haute Résolution
  if (Wire.endTransmission() != 0) return 0; // Erreur de bus
  
  delay(180);
  Wire.requestFrom(0x23, 2);
  if (Wire.available() == 2) {
    level = Wire.read() << 8 | Wire.read();
  }
  return (uint16_t)(level / 1.2); // Conversion selon datasheet
}
