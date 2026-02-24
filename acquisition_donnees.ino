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

// --- PINS (uPesy Low Power) ---
#define PIN_BAT 35      
#define DHT_INT_PIN 27
#define DHT_EXT_PIN 2
#define ONE_WIRE_BUS 14
#define IA_XIAO_SIGNAL 26
#define HX711_DOUT 33
#define HX711_SCK 32
#define SDA_PIN 21
#define SCL_PIN 22

// Instances
DHT dht_int(DHT_INT_PIN, DHT22);
DHT dht_ext(DHT_EXT_PIN, DHT22);
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);
HX711 scale;
Adafruit_MMA8451 mma = Adafruit_MMA8451();

void sendAT(String command) {
  Serial.println(command); 
  delay(1000); 
}

void setup() {
  Serial.begin(9600); 
  delay(2000);
  
  Wire.begin(SDA_PIN, SCL_PIN);
  dht_int.begin();
  dht_ext.begin();
  sensors.begin();
  
  // On initialise quand même l'objet scale au cas où Adam branche les fils
  scale.begin(HX711_DOUT, HX711_SCK);

  // Init Accel
  if (!mma.begin()) Serial.println("MMA Fail");
  else mma.setRange(MMA8451_RANGE_2_G);

  // CONFIGURATION LORA
  sendAT("AT+ID=DevEui,\"" + String(devEui) + "\"");
  sendAT("AT+ID=AppEui,\"" + String(appEui) + "\""); // AppEUI configuré
  sendAT("AT+KEY=APPKEY,\"" + String(appKey) + "\"");
  sendAT("AT+MODE=LWOTAA");
  sendAT("AT+JOIN"); 
  delay(15000);
}

void loop() {
  // 1. Batterie (uPesy internal bridge)
  int raw_bat = analogRead(PIN_BAT);
  float v_bat = (raw_bat * 3.3 / 4095.0) * 2.0;
  uint8_t bat = (uint8_t)constrain(map(v_bat * 100, 320, 415, 0, 100), 0, 100);

  // 2. Accéléromètre & Orientation
  sensors_event_t event; 
  mma.getEvent(&event);
  uint8_t orient = mma.getOrientation();
  int8_t accel_z = (int8_t)event.acceleration.z;

  // 3. Poids : Forcé à 0 pour le test
  uint16_t poids_val = 0; 

  // 4. Températures MOB
  sensors.requestTemperatures();
  int16_t ts1 = (int16_t)(sensors.getTempCByIndex(0) * 100);
  int16_t ts2 = (int16_t)(sensors.getTempCByIndex(1) * 100);

  // 5. DHT & Lux
  int16_t ti = (int16_t)(dht_int.readTemperature() * 10);
  uint16_t hi = (uint16_t)(dht_int.readHumidity() * 10);
  int16_t te = (int16_t)(dht_ext.readTemperature() * 10);
  uint8_t he = (uint8_t)dht_ext.readHumidity();
  uint16_t lux = readSEN0390();

  // 6. Construction Payload (19 octets)
  char payload[40];
  sprintf(payload, "%02X%02X%02X%02X%04X%04X%04X%04X%04X%04X%02X%04X",
          bat, digitalRead(IA_XIAO_SIGNAL), orient, (uint8_t)accel_z, 
          poids_val, ts1, ts2, ti, hi, te, he, lux);

  // 7. Envoi
  Serial.print("AT+MSGHEX=\"");
  Serial.print(payload);
  Serial.print("\"\r\n");

  delay(10000); 
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
