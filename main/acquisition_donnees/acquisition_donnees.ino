#include <Wire.h>
#include <DHT.h>
#include <HX711.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <Adafruit_MMA8451.h>
#include <Adafruit_Sensor.h>

// --- IDENTIFIANTS LORAWAN ---
const char *devEui = "CDEADAEEACFADEEA"; 
const char *appEui = "0000000000000000";
const char *appKey = "0FF6C98BEE426A6DD4C1CBE40804A4C3"; 

// --- CONFIGURATION DES PINS ---
#define PIN_BAT 35 
#define DHT_INT_PIN 26    // Avant: 27
#define DHT_EXT_PIN 5     // Avant: 2
#define ONE_WIRE_BUS 27   // Avant: 14
#define IA_XIAO_SIGNAL 14 // Avant: 26
#define HX711_DOUT 33     // Avant: 32
#define HX711_SCK 32      // Avant: 33
#define SDA_PIN 21
#define SCL_PIN 22

// --- CALIBRATION ---
const float MON_FACTEUR = -29126.0; 
const long MA_TARE_FIXE = -95280; 

// Instances
DHT dht_int(DHT_INT_PIN, DHT22);
DHT dht_ext(DHT_EXT_PIN, DHT22);
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);
HX711 scale;
Adafruit_MMA8451 mma = Adafruit_MMA8451();

// Variables globales pour le signal
int8_t real_rssi = -100; 
int8_t real_snr = -10;

void sendAT(String command) {
  Serial.println(command); 
  delay(1000); 
}

void updateSignalQuality() {
  Serial.println("AT+CSQ"); 
  delay(500);
  if (Serial.available()) {
    String response = Serial.readString();
    int rssiPos = response.indexOf("RSSI ");
    int snrPos = response.indexOf("SNR ");
    if (rssiPos != -1 && snrPos != -1) {
      real_rssi = response.substring(rssiPos + 5, response.indexOf(",", rssiPos)).toInt();
      real_snr = response.substring(snrPos + 4).toInt();
    }
  }
}

void setup() {
  Serial.begin(9600); 
  delay(2000);
  
  Wire.begin(SDA_PIN, SCL_PIN);
  dht_int.begin();
  dht_ext.begin();
  sensors.begin();
  
  scale.begin(HX711_DOUT, HX711_SCK);
  scale.set_scale(MON_FACTEUR);
  scale.set_offset(MA_TARE_FIXE);

  if (!mma.begin()) Serial.println("MMA Fail");
  else mma.setRange(MMA8451_RANGE_2_G);

  // Configuration LoRa
  sendAT("AT+ID=DevEui,\"" + String(devEui) + "\"");
  sendAT("AT+ID=AppEui,\"" + String(appEui) + "\"");
  sendAT("AT+KEY=APPKEY,\"" + String(appKey) + "\"");
  sendAT("AT+MODE=LWOTAA");
  sendAT("AT+JOIN"); 
  delay(15000); 
}

void loop() {
  updateSignalQuality();

  // 1. Batterie
  int raw_bat = analogRead(PIN_BAT);
  float v_bat = (raw_bat * 3.3 / 4095.0) * 2.0; 
  uint8_t bat = (uint8_t)constrain(map(v_bat * 100, 320, 415, 0, 100), 0, 100);

  // 2. Accéléromètre & IA
  sensors_event_t event; 
  mma.getEvent(&event);
  uint8_t orient = mma.getOrientation();
  int8_t accel_z = (int8_t)event.acceleration.z;
  uint8_t alerte_ia = digitalRead(IA_XIAO_SIGNAL);

  // 3. Poids
  float p_kg = scale.get_units(5);
  if (p_kg < 0) p_kg = 0; 
  uint16_t poids_val = (uint16_t)(p_kg * 100); 

  // 4. Températures & Environnement
  sensors.requestTemperatures();
  int16_t ts1 = (int16_t)(sensors.getTempCByIndex(0) * 100);
  int16_t ts2 = (int16_t)(sensors.getTempCByIndex(1) * 100);
  int16_t ti = (int16_t)(dht_int.readTemperature() * 10);
  uint16_t hi = (uint16_t)(dht_int.readHumidity() * 10);
  int16_t te = (int16_t)(dht_ext.readTemperature() * 10);
  uint8_t he = (uint8_t)dht_ext.readHumidity();
  uint16_t lux = readSEN0390();

  // 5. Construction Payload (Sécurisée à 100 chars pour éviter le crash)
  char payload[100]; 
  sprintf(payload, "%02X%02X%02X%02X%04X%04X%04X%04X%04X%04X%02X%04X%02X%02X",
          bat, alerte_ia, orient, (uint8_t)accel_z, 
          poids_val, (uint16_t)ts1, (uint16_t)ts2, (uint16_t)ti, hi, (uint16_t)te, he, lux, 
          (uint8_t)real_rssi, (uint8_t)real_snr);

  // 6. Envoi
  Serial.print("AT+MSGHEX=\"");
  Serial.print(payload);
  Serial.print("\"\r\n");

  delay(20000); // 20 secondes entre chaque envoi
}

uint16_t readSEN0390() {
  uint16_t level = 0;
  Wire.beginTransmission(0x23);
  Wire.write(0x10); 
  if (Wire.endTransmission() != 0) return 0;
  delay(180);
  Wire.requestFrom(0x23, 2);
  if (Wire.available() == 2) level = Wire.read() << 8 | Wire.read();
  return (uint16_t)(level / 1.2);
}
