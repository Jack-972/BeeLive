#include <HX711.h>
#define HX711_DOUT 32 
#define HX711_SCK 33  
HX711 scale;

void setup() {
  Serial.begin(9600);
  scale.begin(HX711_DOUT, HX711_SCK);
  Serial.println("--- RECHERCHE DE LA TARE FIXE ---");
  Serial.println("Videz le plateau... mesure dans 5 secondes.");
  delay(5000);
}

void loop() {
  if (scale.is_ready()) {
    long tare_brute = scale.read_average(20); 
    Serial.print("TARE_FIXE A NOTER : ");
    Serial.println(tare_brute);
  }
  delay(2000);
}
