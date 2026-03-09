#include <HX711.h>

#define HX711_DOUT 32 
#define HX711_SCK 33  

HX711 scale;

void setup() {
  Serial.begin(9600);
  Serial.println("--- DEMARRAGE CALIBRATION STATIQUE ---");

  scale.begin(HX711_DOUT, HX711_SCK);
  
  scale.set_scale(1.f); 

  Serial.println("ETAPE 1 : VIDEZ LE PLATEAU !");
  Serial.println("Début de la tare dans 5 secondes...");
  delay(5000); 

  scale.tare(); // Définit le zéro actuel
  Serial.println("TARE TERMINEE.");
  Serial.println("---------------------------------------");
  Serial.println("ETAPE 2 : POSEZ LE TELEPHONE (206g)");
  Serial.println("Attente de 5 secondes pour stabilisation...");
  delay(5000);
}

void loop() {
  if (scale.is_ready()) {
    // On lit la moyenne de 10 mesures
    float valeur_brute = scale.get_units(10);
    
    Serial.print("VALEUR BRUTE (A NOTER) : ");
    Serial.println(valeur_brute);
  } else {
    Serial.println("Erreur : HX711 non prêt.");
  }
  delay(1000); 
}
