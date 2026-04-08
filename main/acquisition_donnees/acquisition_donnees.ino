#include <Wire.h>
#include <DHT.h>
#include <HX711.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <Adafruit_MMA8451.h>
#include <Adafruit_Sensor.h>
#include "driver/rtc_io.h"
#include "driver/gpio.h"
#include "driver/adc.h"

// --- CONFIGURATION PINS ---
#define PIN_BUZZER 13
#define PIN_XIAO_EN 4      
#define PIN_NANO_EN 25      
#define DHT_INT_PIN 26
#define PIN_BAT 35 
#define DHT_EXT_PIN 5
#define ONE_WIRE_BUS 27
#define IA_SIGNAL_PIN 14   
#define HX711_DOUT 33
#define HX711_SCK 32

// --- CONFIGURATION SYSTÈME ---
#define uS_TO_S_FACTOR 1000000ULL  
#define SEUIL_TEMP_EXT 5.0
#define SEUIL_LUX_JOUR 50

// --- INSTANCES ---
DHT dht_int(DHT_INT_PIN, DHT22);
DHT dht_ext(DHT_EXT_PIN, DHT22);
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);
HX711 scale;
Adafruit_MMA8451 mma = Adafruit_MMA8451();

// --- VARIABLES RTC ---
RTC_DATA_ATTR int bootCount = 0;
RTC_DATA_ATTR float dernier_poids = 0; 
RTC_DATA_ATTR int minutes_entre_envois = 10; 
RTC_DATA_ATTR int envoi_automatique = 0;

RTC_DATA_ATTR float off_poids = 0.0;
RTC_DATA_ATTR float off_ti    = 0.0;
RTC_DATA_ATTR float off_hi    = 0.0;
RTC_DATA_ATTR float off_te    = 0.0;
RTC_DATA_ATTR float off_he    = 0.0;
RTC_DATA_ATTR float off_ts1   = 0.0;
RTC_DATA_ATTR float off_ts2   = 0.0;

RTC_DATA_ATTR int8_t real_rssi = -100; 
RTC_DATA_ATTR int8_t real_snr = 5;

uint8_t alerte_ia = 0; 

void beep(int duree, int repetitions) {
  for(int i=0; i<repetitions; i++) {
    digitalWrite(PIN_BUZZER, HIGH); delay(duree);
    digitalWrite(PIN_BUZZER, LOW);  delay(duree);
  }
}

void envoi_auto(uint8_t bat){
  if (bat < 20){
    minutes_entre_envois = 60;
  }
  else if (bat < 40){
    minutes_entre_envois = 40;
  }
  else if (bat < 60){
    minutes_entre_envois = 20;
  }
  else{
    minutes_entre_envois = 10;
  }
}


void setup() {
  // --- Déverrouillage pins IA ---
  gpio_hold_dis(GPIO_NUM_4);
  gpio_hold_dis(GPIO_NUM_25);

  // 1. INITIALISATION MATÉRIELLE
  digitalWrite(PIN_XIAO_EN, LOW);
  digitalWrite(PIN_NANO_EN, LOW);
  pinMode(PIN_BUZZER, OUTPUT);
  pinMode(PIN_XIAO_EN, OUTPUT);
  pinMode(PIN_NANO_EN, OUTPUT);
  pinMode(IA_SIGNAL_PIN, INPUT_PULLDOWN);
  analogSetAttenuation(ADC_11db);
  scale.begin(HX711_DOUT, HX711_SCK);
  scale.power_up();

  if (bootCount == 0) beep(1000, 1);
  bootCount++;
  
  Serial.begin(9600);    
  Serial2.begin(115200); 
  Wire.begin(21, 22);
  
  dht_int.begin();
  dht_ext.begin();
  sensors.begin();
  delay(2000); 

  // 2. LECTURE MÉTÉO INITIALE
  float te_init = dht_ext.readTemperature();
  float raw_lux_check = 0;
  
  // Petit check lux rapide
  Wire.beginTransmission(0x23); Wire.write(0x10); Wire.endTransmission();
  delay(200);
  Wire.requestFrom(0x23, 2);
  if(Wire.available()==2) raw_lux_check = (Wire.read() << 8 | Wire.read()) / 1.2;

  // 6. BATTERIE (Formule précise uPesy)
  pinMode(PIN_BAT, INPUT);
  delay(50);
  int raw_bat = analogRead(PIN_BAT);
  float v_bat = 1.435 * ((float)raw_bat / 4095.0) * 3.3; 
  uint8_t bat = (uint8_t)constrain(map(v_bat * 100, 320, 420, 0, 100), 0, 100);

  if (bat < 20){
    envoi_automatique = 1;
  }

  if (envoi_automatique == 1){
    envoi_auto(bat);
  }

  // 3. LOGIQUE IA (AUDIO PRIORITAIRE)
  float temp_ia = isnan(te_init) ? 20.0 : te_init;
  // A activer pour le deep sleep
  if (temp_ia > SEUIL_TEMP_EXT && raw_lux_check > SEUIL_LUX_JOUR && bat > 50) {

    digitalWrite(PIN_XIAO_EN, HIGH);
    digitalWrite(PIN_NANO_EN, HIGH);
    delay(3000);
      
    unsigned long startIA = millis();
    while (millis() - startIA < 12000) { 
      // Debug visuel du signal XIAO
      int signalVision = digitalRead(IA_SIGNAL_PIN);

      if (Serial2.available() > 0) {
        uint8_t index = Serial2.read();
        if (index == 4) alerte_ia = 4; 
        else if (index == 2) alerte_ia = 3; 
        else if (index == 0) alerte_ia = 1;
        if (alerte_ia > 0){
          beep(100, 3);
          break;
        }
      }

      if (signalVision == HIGH) { 
          alerte_ia = 1; 
          beep(100, 3);
          break; 
      }
      delay(100); // On ralentit un peu pour ne pas polluer le moniteur
    }
  }

  // --- EXTINCTION ET ISOLATION TOTALE ---

  // 1. Isolement UART (Audio Nano)
  Serial2.end();
  pinMode(16, OUTPUT); digitalWrite(16, LOW);
  pinMode(17, OUTPUT); digitalWrite(17, LOW);

  // 2. Isolement GPIO (Vision XIAO)
  pinMode(IA_SIGNAL_PIN, OUTPUT); 
  digitalWrite(IA_SIGNAL_PIN, LOW);

  // 3. Coupure physique des Pololu
  digitalWrite(PIN_XIAO_EN, LOW);
  digitalWrite(PIN_NANO_EN, LOW);
  delay(10); 
  
  // Verrouillage matériel des Pololu à 0V
  gpio_hold_en(GPIO_NUM_4);
  gpio_hold_en(GPIO_NUM_25);
  gpio_deep_sleep_hold_en();

  // 4. LECTURE DES CAPTEURS AVEC OFFSETS
  sensors.requestTemperatures();
  float ts1 = sensors.getTempCByIndex(0);
  float ts2 = sensors.getTempCByIndex(1);
  ts1 = (ts1 == DEVICE_DISCONNECTED_C ? 0 : ts1 + off_ts1);
  ts2 = (ts2 == DEVICE_DISCONNECTED_C ? 0 : ts2 + off_ts2);
  
  float raw_ti = dht_int.readTemperature();
  float raw_hi = dht_int.readHumidity();
  float ti = (isnan(raw_ti) ? 0 : raw_ti) + off_ti;
  uint16_t hi = (uint16_t)(isnan(raw_hi) ? 0 : raw_hi + off_hi);
  
  float raw_te = dht_ext.readTemperature();
  float raw_he = dht_ext.readHumidity();
  float te = (isnan(raw_te) ? 0 : raw_te) + off_te;
  uint8_t he = (uint8_t)(isnan(raw_he) ? 0 : raw_he + off_he);

  delay(500);
  scale.set_scale(-29126.0); scale.set_offset(-95280);
  float p_kg = scale.get_units(5) + off_poids;
  uint16_t poids_val = (uint16_t)(max(0.0f, p_kg) * 100);

  // 5. ESSAIMAGE
  if (dernier_poids > 0 && (dernier_poids - p_kg) > 0.8) alerte_ia = 2;
  dernier_poids = p_kg;

  
  uint8_t orient = 0;
  if (mma.begin()) {
    delay(100);
    orient = mma.getOrientation();
  }

  // --- 7. ENVOI LORA ---
  if (bootCount == 1) {
    Serial.println("AT+JOIN"); 
    delay(12000); 
  }

  // On purge le buffer pour être propre
  while(Serial.available()) Serial.read();

  char payload[64];
  sprintf(payload, "%02X%02X%02X%04X%04X%04X%04X%04X%04X%02X%04X%02X%02X",
          bat, alerte_ia, (uint8_t)(orient > 0 ? orient - 1 : 0),
          poids_val, (int16_t)(ts1*100), (int16_t)(ts2*100), (int16_t)(ti*10),
          (uint16_t)(hi*10), (int16_t)(te*10), (uint8_t)he, (uint16_t)raw_lux_check, 
          (uint8_t)real_rssi, (uint8_t)real_snr);

  Serial.print("AT+MSGHEX=\""); Serial.print(payload); Serial.print("\"\r\n");

  // --- 8. LOGIQUE DOWNLINK & RSSI (LA MAGIE OPÈRE ICI) ---
  unsigned long startWait = millis();

  while (millis() - startWait < 15000) { 
    if (Serial.available()) {
      String rx = Serial.readStringUntil('\n');
      rx.trim();
      
      // On affiche pour que tu voies le miracle sur ton écran
      if (rx.length() > 0) Serial.println("LORA-E5 : " + rx); 

      // 1. CAPTURE DU RSSI AU VOL (Ex: +MSGHEX: RXWIN1, RSSI -99, SNR 5)
      if (rx.indexOf("RSSI") != -1 && rx.indexOf("SNR") != -1) {
        int rssiIdx = rx.indexOf("RSSI") + 5; // On se place après "RSSI"
        int commaIdx = rx.indexOf(",", rssiIdx);
        if (commaIdx != -1) real_rssi = rx.substring(rssiIdx, commaIdx).toInt();

        int snrIdx = rx.indexOf("SNR") + 3; // On se place après "SNR"
        real_snr = rx.substring(snrIdx).toInt();
      }

      // 2. CAPTURE DU DOWNLINK (Ex: +MSGHEX: PORT: 1; RX: "0564")
      if (rx.indexOf("RX: \"") != -1) {
        int start = rx.indexOf("\"");
        int end = rx.indexOf("\"", start + 1);

        if (start != -1 && end != -1) {

          String hexCmd = rx.substring(start + 1, end);

          if (hexCmd.length() >= 4) {

            uint8_t cmd = strtol(hexCmd.substring(0,2).c_str(),NULL,16);
            int8_t val = strtol(hexCmd.substring(2,4).c_str(),NULL,16);

            // Application de la commande
            if (cmd == 0x01){
              if (val < 1){
                envoi_automatique = 0;
              }
              else if (val == 1){
                envoi_automatique = 1;
              }
              else{
                envoi_automatique = 0;
                minutes_entre_envois = val;
              }
            }
            else if (cmd == 0x02) off_poids = (float)val / 10.0;
            else if (cmd == 0x03) off_ti    = (float)val / 10.0;
            else if (cmd == 0x04) off_hi    = (float)val;
            else if (cmd == 0x05) off_te    = (float)val / 10.0;
            else if (cmd == 0x06) off_he    = (float)val;
            else if (cmd == 0x07) off_ts1   = (float)val / 10.0;
            else if (cmd == 0x08) off_ts2   = (float)val / 10.0;

            beep(100, 5);
          }
        }
      }
    }
  }

  // --- 9. SOMMEIL ---

  scale.power_down();

  Serial.println("AT+LOWPOWER");
  Serial.flush();
  delay(100);

  // Désactivation I2C
  Wire.end();
  pinMode(21, INPUT);
  pinMode(22, INPUT);

  Serial.end();

  // Sleep timer
  esp_sleep_enable_timer_wakeup(minutes_entre_envois * 60 * uS_TO_S_FACTOR);

  // Deep sleep
  esp_deep_sleep_start();
}

void loop() {}
