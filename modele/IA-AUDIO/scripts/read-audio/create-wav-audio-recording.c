#include <PDM.h>
#include "BeeSound_inferencing.h"

// Configuration
#define SAMPLE_RATE 16000
#define BUFFER_SIZE 512
short sampleBuffer[BUFFER_SIZE];
volatile int samplesRead;

void onPDMdata() {
  int bytesAvailable = PDM.available();
  PDM.read(sampleBuffer, bytesAvailable);
  samplesRead = bytesAvailable / 2; // 2 bytes par échantillon
}

void setup() {
  Serial.begin(115200);
  while (!Serial);
  
  pinMode(LED_BUILTIN, OUTPUT);
  
  // Configure le callback PDM
  PDM.onReceive(onPDMdata);
  PDM.setBufferSize(BUFFER_SIZE * 2);
  
  // Démarre le micro (1 canal, 16kHz)
  if (!PDM.begin(1, SAMPLE_RATE)) {
    while (1) {
      digitalWrite(LED_BUILTIN, HIGH);
      delay(100);
      digitalWrite(LED_BUILTIN, LOW);
      delay(100);
    }
  }
  
  PDM.setGain(80); // Volume maximum (0-80)
  
  // Attend une commande pour démarrer
  digitalWrite(LED_BUILTIN, HIGH);
  while (Serial.available() == 0);
  Serial.read();
  digitalWrite(LED_BUILTIN, LOW);
}

void loop() {
  if (samplesRead) {
    // Envoie les échantillons audio bruts via Serial
    Serial.write((byte*)sampleBuffer, samplesRead * 2);
    samplesRead = 0;
    
    // LED clignote pendant l'enregistrement
    static unsigned long lastBlink = 0;
    if (millis() - lastBlink > 200) {
      digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));
      lastBlink = millis();
    }
  }
}