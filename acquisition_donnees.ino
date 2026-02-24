#include <Wire.h>
#include <DHT.h>
#include <OneWire.h>
#include <DallasTemperature.h>

// ==========================================
// 1. IDENTIFIANTS LORAWAN
// ==========================================
const char *devEui = "CDEADAEEACFADEEA"; 
const char *appEui = "0000000000000000";
const char *appKey = "0FF6C98BEE426A6DD4C1CBE40804A4C3"; 

// ==========================================
// 2. CONFIGURATION DES PINS
// ==========================================
#define DHT_INT_PIN 27
#define DHT_EXT_PIN 2
#define ONE_WIRE_BUS 14 // Pin pour les 2 sondes MOB

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
  
  dht_int.begin();
  dht_ext.begin();
  sensors.begin(); // Initialise le bus OneWire

  // CONFIGURATION LORA
  sendAT("AT+ID=DevEui,\"" + String(devEui) + "\"");
  sendAT("AT+ID=AppEui,\"" + String(appEui) + "\"");
  sendAT("AT+KEY=APPKEY,\"" + String(appKey) + "\"");
  sendAT("AT+MODE=LWOTAA");
  
  Serial.println("AT+JOIN"); 
  delay(15000); 
}

void loop() {
  // --- LECTURE DHT22 ---
  float ti = dht_int.readTemperature();
  float hi = dht_int.readHumidity();
  float te = dht_ext.readTemperature();
  float he = dht_ext.readHumidity();

  // --- LECTURE SONDES MOB (DS18B20) ---
  sensors.requestTemperatures(); 
  float t_m1 = sensors.getTempCByIndex(0); // Première sonde trouvée
  float t_m2 = sensors.getTempCByIndex(1); // Deuxième sonde trouvée

  // --- SECURITÉ ET CONVERSION ---
  if (isnan(ti)) ti = 0; if (isnan(hi)) hi = 0;
  if (isnan(te)) te = 0; if (isnan(he)) he = 0;
  if (t_m1 == DEVICE_DISCONNECTED_C) t_m1 = 0;
  if (t_m2 == DEVICE_DISCONNECTED_C) t_m2 = 0;

  int16_t ti_val = (int16_t)(ti * 10);
  uint16_t hi_val = (uint16_t)(hi * 10);
  int16_t te_val = (int16_t)(te * 10);
  uint8_t he_val = (uint8_t)he;
  
  int16_t tm1_val = (int16_t)(t_m1 * 100); // MOB en x100 pour la précision
  int16_t tm2_val = (int16_t)(t_m2 * 100);

  // --- CONSTRUCTION DE LA TRAME (17 octets) ---
  // Ordre : Bat(1), Alert(1), Poids(2), MOB1(2), MOB2(2), DHT_I_T(2), DHT_I_H(2), DHT_E_T(2), DHT_E_H(1), Lux(2)
  char payload[35];
  sprintf(payload, "64000000%04X%04X%04X%04X%04X%02X0000",
          (uint16_t)tm1_val, (uint16_t)tm2_val, (uint16_t)ti_val, hi_val, (uint16_t)te_val, he_val);

  // --- ENVOI ---
  Serial.print("AT+MSGHEX=\"");
  Serial.print(payload);
  Serial.print("\"\r\n");

  delay(10000); 
}
