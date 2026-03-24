#include <Wire.h>
#include <DHT.h>
#include <HX711.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <Adafruit_MMA8451.h>
#include <Adafruit_Sensor.h>

// --- CONFIGURATION PINS (LES BONS) ---
#define PIN_BUZZER 13
#define PIN_XIAO_EN 4      
#define PIN_NANO_EN 0      
#define DHT_INT_PIN 26
#define PIN_BAT 35 
#define DHT_EXT_PIN 5
#define ONE_WIRE_BUS 27
#define IA_SIGNAL_PIN 14   
#define HX711_DOUT 33
#define HX711_SCK 32

// --- CONFIGURATION SYSTÈME ---
#define uS_TO_S_FACTOR 1000000ULL  
#define SEUIL_TEMP_EXT 12.0
#define SEUIL_LUX_JOUR 50

// --- INSTANCES ---
DHT dht_int(DHT_INT_PIN, DHT22);
DHT dht_ext(DHT_EXT_PIN, DHT22);
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);
HX711 scale;
Adafruit_MMA8451 mma = Adafruit_MMA8451();

// --- VARIABLES RTC (Indépendantes pour chaque capteur) ---
RTC_DATA_ATTR int bootCount = 0;
RTC_DATA_ATTR float dernier_poids = 0; 
RTC_DATA_ATTR int minutes_entre_envois = 1; 

// Liste des offsets indépendants
RTC_DATA_ATTR float off_poids = 0.0; // Offset Poids
RTC_DATA_ATTR float off_ti    = 0.0; // Offset Temp Int (DHT)
RTC_DATA_ATTR float off_hi    = 0.0; // Offset Humidité Int (DHT)
RTC_DATA_ATTR float off_te    = 0.0; // Offset Temp Ext (DHT)
RTC_DATA_ATTR float off_he    = 0.0; // Offset Humidité Ext (DHT)
RTC_DATA_ATTR float off_ts1   = 0.0; // Offset Sonde 1 (DS18B20)
RTC_DATA_ATTR float off_ts2   = 0.0; // Offset Sonde 2 (DS18B20)  

uint8_t alerte_ia = 0; 
int8_t real_rssi = -100; 
int8_t real_snr = 5;

void beep(int duree, int repetitions) {
  for(int i=0; i<repetitions; i++) {
    digitalWrite(PIN_BUZZER, HIGH); delay(duree);
    digitalWrite(PIN_BUZZER, LOW);  delay(duree);
  }
}

void updateSignalQuality() {
  Serial.println("AT+CSQ"); 
  delay(1000); 
  if (Serial.available()) {
    String res = Serial.readString();
    int rPos = res.indexOf("RSSI ");
    if (rPos != -1) real_rssi = res.substring(rPos + 5, res.indexOf(",", rPos)).toInt();
  }
}

void setup() {
  // 1. INITIALISATION MATÉRIELLE
  pinMode(PIN_BUZZER, OUTPUT);
  pinMode(PIN_XIAO_EN, OUTPUT);
  pinMode(PIN_NANO_EN, OUTPUT);
  pinMode(IA_SIGNAL_PIN, INPUT_PULLDOWN);

  if (bootCount == 0) beep(200, 1);
  bootCount++;

  // ON ALLUME TOUT TOUT DE SUITE (SÉCURITÉ CAPTEURS)
  digitalWrite(PIN_XIAO_EN, HIGH);
  digitalWrite(PIN_NANO_EN, HIGH);
  
  Serial.begin(9600);    
  Serial2.begin(115200); 
  Wire.begin(21, 22);
  
  // INITIALISATION UNIQUE
  dht_int.begin();
  dht_ext.begin();
  sensors.begin();
  delay(2000); // Important : Temps de stabilisation

  // 2. LECTURE MÉTÉO POUR DÉCISION IA
  float te = dht_ext.readTemperature();
  float he = dht_ext.readHumidity();
  
  // Robustesse : si le capteur échoue une fois, on ne bloque pas l'IA
  if (isnan(te)) te = 20.0; 

  uint16_t lux = 0;
  Wire.beginTransmission(0x23); Wire.write(0x10); Wire.endTransmission();
  delay(200);
  Wire.requestFrom(0x23, 2);
  if(Wire.available()==2) lux = (Wire.read() << 8 | Wire.read()) / 1.2;

  // 3. LOGIQUE IA (AUDIO PRIORITAIRE)
  if (te > SEUIL_TEMP_EXT && lux > SEUIL_LUX_JOUR) {
    unsigned long startIA = millis();
    while (millis() - startIA < 10000) { 
      if (Serial2.available() > 0) {
        uint8_t index = Serial2.read();
        if (index == 4) alerte_ia = 4; 
        else if (index == 2) alerte_ia = 3; 
        else if (index == 0) alerte_ia = 1;
        if (alerte_ia > 0) break;
      }
      if (digitalRead(IA_SIGNAL_PIN) == HIGH) { alerte_ia = 1; break; }
      delay(10);
    }
  }

  // 4. LECTURE DES CAPTEURS AVEC OFFSETS DÉDIÉS
  sensors.requestTemperatures();
  // Sondes température (DS18B20)
  float ts1 = sensors.getTempCByIndex(0);
  float ts2 = sensors.getTempCByIndex(1);
  ts1 = (ts1 == DEVICE_DISCONNECTED_C ? 0 : ts1 + off_ts1);
  ts2 = (ts2 == DEVICE_DISCONNECTED_C ? 0 : ts2 + off_ts2);
  
  // DHT Intérieur
  float raw_ti = dht_int.readTemperature();
  float raw_hi = dht_int.readHumidity();
  float ti = (isnan(raw_ti) ? 0 : raw_ti) + off_ti;
  uint16_t hi = (uint16_t)((isnan(raw_hi) ? 0 : raw_hi + off_hi));
  
  // DHT Extérieur
  float raw_te = dht_ext.readTemperature();
  float raw_he = dht_ext.readHumidity();
  te = (isnan(raw_te) ? 0 : raw_te) + off_te;
  he = (uint8_t)((isnan(raw_he) ? 0 : raw_he + off_he));

  // Balance
  scale.begin(HX711_DOUT, HX711_SCK);
  float p_kg = scale.get_units(5) + off_poids;
  uint16_t poids_val = (uint16_t)(max(0.0f, p_kg) * 100);

  // 5. ESSAIMAGE
  if (dernier_poids > 0 && (dernier_poids - p_kg) > 0.8) alerte_ia = 2;
  dernier_poids = p_kg;

  // 6. BATTERIE / MMA
  int raw_bat = analogRead(PIN_BAT);
  float v_bat = (raw_bat * 3.3 / 4095.0) * 2.0; 
  uint8_t bat = (uint8_t)constrain(map(v_bat * 100, 320, 415, 0, 100), 0, 100);
  mma.begin(); delay(100);
  uint8_t orient = mma.getOrientation();

  // 7. ENVOI
  Serial.println("AT+JOIN"); delay(12000);
  updateSignalQuality();

  char payload[64];
  sprintf(payload, "%02X%02X%02X00%04X%04X%04X%04X%04X%04X%02X%04X%02X%02X",
          bat, alerte_ia, (orient > 0 ? orient - 1 : 0),
          poids_val, (int16_t)(ts1*100), (int16_t)(ts2*100), (int16_t)(ti*10),
          (uint16_t)(hi*10), (int16_t)(te*10), (uint8_t)he, lux, (uint8_t)real_rssi, (uint8_t)real_snr);

  Serial.print("AT+MSGHEX=\""); Serial.print(payload); Serial.print("\"\r\n");

  // 8. LOGIQUE DOWNLINK AVEC IDS DÉDIÉS
  delay(5000); 
  if (Serial.available()) {
      String rx = Serial.readString();
      if (rx.indexOf("DATA: ") != -1) {
          String hexCmd = rx.substring(rx.indexOf("DATA: ") + 6, rx.indexOf("DATA: ") + 10);
          uint8_t cmd = strtol(hexCmd.substring(0, 2).c_str(), NULL, 16);
          int8_t val  = (int8_t)strtol(hexCmd.substring(2, 4).c_str(), NULL, 16);

          switch (cmd) {
              case 0x01: minutes_entre_envois = (val < 2 ? 2 : val); break; // Fréquence
              case 0x02: off_poids = (float)val / 10.0; break;             // Poids (0.1kg)
              case 0x03: off_ti    = (float)val / 10.0; break;             // Temp Int (0.1°C)
              case 0x04: off_hi    = (float)val;        break;             // Humidité Int (1%)
              case 0x05: off_te    = (float)val / 10.0; break;             // Temp Ext (0.1°C)
              case 0x06: off_he    = (float)val;        break;             // Humidité Ext (1%)
              case 0x07: off_ts1   = (float)val / 10.0; break;             // Sonde 1 (0.1°C)
              case 0x08: off_ts2   = (float)val / 10.0; break;             // Sonde 2 (0.1°C)
          }
          beep(100, 2);
      }
  }

  digitalWrite(PIN_XIAO_EN, LOW);
  digitalWrite(PIN_NANO_EN, LOW);
  esp_sleep_enable_timer_wakeup(minutes_entre_envois * 60 * uS_TO_S_FACTOR);
  esp_deep_sleep_start();
}

void loop() {}
