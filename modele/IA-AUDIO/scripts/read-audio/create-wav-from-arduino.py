import serial
import wave
import time

# --- CONFIGURATION ---
PORT = 'COM7'  # <--- Vérifie bien ton port dans l'IDE Arduino
BAUD = 115200
SECONDS = 5    # Durée de l'enregistrement
SAMPLE_RATE = 16000
OUTPUT_FILE = "enregistrement_audio.wav"

# Calcul du nombre d'octets attendus (16kHz * 2 octets par échantillon * secondes)
TOTAL_BYTES = SAMPLE_RATE * 2 * SECONDS

try:
    # Connexion au port série
    ser = serial.Serial(PORT, BAUD, timeout=1)
    time.sleep(2)  # Attente que l'Arduino se réinitialise
    
    # Vide le buffer
    ser.reset_input_buffer()
    
    print(f"✓ Connexion établie sur {PORT}")
    print(f"⏺ Début de l'enregistrement ({SECONDS} secondes)...")
    print("🎤 PARLEZ MAINTENANT !")
    
    # Envoie un caractère pour démarrer l'enregistrement
    ser.write(b's')
    time.sleep(0.1)
    
    raw_data = bytearray()
    start_time = time.time()

    # Capture des données
    while len(raw_data) < TOTAL_BYTES:
        if ser.in_waiting > 0:
            raw_data.extend(ser.read(ser.in_waiting))
            
            # Barre de progression
            progress = len(raw_data) / TOTAL_BYTES
            bars = int(progress * 30)
            print(f"\rCapture : [{'█'*bars}{'·'*(30-bars)}] {progress*100:.1f}%", end='')
        
        # Timeout si rien ne se passe
        if time.time() - start_time > SECONDS + 5:
            print("\n⚠ Timeout : vérifiez la connexion Arduino")
            break

    print(f"\n✓ Capture terminée ({len(raw_data)} octets reçus)")
    ser.close()

    # Génération du fichier WAV
    with wave.open(OUTPUT_FILE, 'wb') as f:
        f.setnchannels(1)           # Mono
        f.setsampwidth(2)           # 16-bit
        f.setframerate(SAMPLE_RATE) # 16kHz
        f.writeframes(raw_data[:TOTAL_BYTES])

    print(f"✓ Fichier WAV créé : {OUTPUT_FILE}")
    print(f"  Durée : {SECONDS}s | Taille : {len(raw_data[:TOTAL_BYTES])} octets")
    print("\n💡 Vous pouvez maintenant écouter le fichier avec n'importe quel lecteur audio !")

except serial.SerialException as e:
    print(f"\n❌ Erreur de connexion : {e}")
    print("   → Vérifiez que le Moniteur Série d'Arduino est bien FERMÉ")
    print(f"   → Vérifiez que le port {PORT} est correct")
except Exception as e:
    print(f"\n❌ Erreur : {e}")