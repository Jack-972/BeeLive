# 🐝 BeeLive - Système de Surveillance de Ruche Connectée

**BeeLive** est une solution IoT avancée de monitoring apicole développée dans le cadre du projet **Open Ruche Polytech 2026 (G2_FISE_2026_BeeLive)**.

---

## 1. Architecture et Accès aux Plateformes

Le système repose sur une architecture décentralisée garantissant la sécurité des données et une faible consommation énergétique.

| Plateforme | Rôle | Login | Mot de passe |
| :--- | :--- | :--- | :--- |
| **BEEP.nl** | Standard de données mondial | confidentiel | confidentiel |
| **TTN** | Réseau LoRaWAN | Accès partagé via invitation | Voir console TTN |
| **VPS** | API FastAPI & Alertes | Accès SSH | Clé RSA privée |

> **Note :** Nous avons développé notre propre serveur backend (FastAPI sur VPS) pour une maîtrise totale des données et des alertes, s'affranchissant des solutions cloud propriétaires.

---

## 2. Configuration du Nœud ESP32

### 2.1 Identifiants LoRaWAN (OTAA)
Les identifiants sont persistants dans le modem LoRa-E5

### 2.2 Installation
1. Cloner le dépôt : `git clone https://github.com/Jack-972/BeeLive.git`
2. Ouvrir le projet **acquisition_donnees** (ESP32).
3. Installer les bibliothèques : DHT, DallasTemperature, HX711, Adafruit MMA8451.
4. Flasher la carte. Un Bip sonore confirme le succès.

---

## 3. Modules d'Intelligence Artificielle (Edge AI)

Flasher les cartes auxiliaires via les codes sources présents sur le GitHub avant l'assemblage :
- **Vision (XIAO) :** Détection de frelons asiatiques.
- **Audio (Nano 33) :** Détection d'absence de reine et Piping (chant de la reine).

---

## 4. Configuration Réseau (TTN & BEEP)

### 4.1 Payload Formatter
Utiliser le script `scripts/payloadFormatters.json` sur la console TTN.

### 4.2 Webhooks
Configurer deux webhooks JSON :
- **VPS BeeLive :** `http://[Lien_VPS]/lora-uplink`
- **API BEEP :** `https://[Lien_BEEP]/api/yann`

### 4.3 Liaison BEEP.nl
Dans le décodeur TTN, insérer votre Hardware ID : `data.key = "VOTRE_ID_BEEP";`

---

## 5. Téléconfiguration Indépendante (Downlinks) - Fiche Technique

Le système BeeLive permet un étalonnage métrologique précis à distance sans aucune intervention physique sur la ruche.

### 5.1 Protocole de communication
- **Port LoRaWAN :** FPort 1.
- **Mode :** Unconfirmed Downlink.
- **Structure de trame :** 2 octets Hexadécimaux (`[ID_Commande (1 octet)] [Valeur signée (1 octet)]`).
- **Encodage :** Entier signé 8-bit (`int8_t`).
- **Logique mathématique :** Les valeurs négatives doivent être envoyées en **complément à deux**.

### 5.2 Référentiel complet des commandes (Tableau 1)

| Paramètre | ID | Échelle | Cible souhaitée | Valeur (Hex) | Code Downlink |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **Fréquence** | **01** | 1 min | Envoi toutes les 15 min | 0F | **010F** |
| | | | Envoi toutes les 60 min | 3C | **013C** |
| **Poids** | **02** | 0.1 kg | Ajouter +1.0 kg | 0A | **020A** |
| | | | Retirer -0.5 kg | FB | **02FB** |
| | | | Retirer -2.0 kg | EC | **02EC** |
| **Temp. Int** | **03** | 0.1 °C | Ajouter +0.5 °C | 05 | **0305** |
| | | | Retirer -1.5 °C | F1 | **03F1** |
| **Hum. Int** | **04** | 1% | Ajouter +10% | 0A | **040A** |
| | | | Retirer -5% | FB | **04FB** |
| **Temp. Ext** | **05** | 0.1 °C | Ajouter +2.0 °C | 14 | **0514** |
| | | | Retirer -0.2 °C | FE | **05FE** |
| **Hum. Ext** | **06** | 1% | Ajouter +5% | 05 | **0605** |
| | | | Retirer -10% | F6 | **06F6** |
| **Sonde 1** | **07** | 0.1 °C | Ajouter +0.3 °C | 03 | **0703** |
| | | | Retirer -0.1 °C | FF | **07FF** |
| **Sonde 2** | **08** | 0.1 °C | Retirer -1.0 °C | F6 | **08F6** |

### 5.3 Modes Spéciaux et Énergie
- **Désactivation IA (0100) :** Désactive le cycle d'alimentation des cartes XIAO et Nano 33 pour économiser l'énergie.
- **Mode Automatique (0101) :** Réactive la gestion adaptative (Edge AI activé).
- **Persistance :** Les paramètres sont stockés dans la mémoire RTC (`RTC_DATA_ATTR`) de l'ESP32, ce qui permet de conserver les offsets même après un Deep Sleep ou un redémarrage.

---

## 6. Sécurité et Mode Survie

- **Isolation Physique :** Utilisation d'interrupteurs Pololu pour couper totalement l'alimentation des IA et isolation des bus I2C/UART (0V de fuite).
- **Mode Dégradé (< 20%) :** En cas de batterie faible, le système force une fréquence d'envoi de 60 min et coupe les modules IA pour préserver la fonction vitale de pesée.
- **Surveillance :** Alertes mail critiques et rapport quotidien SQLite chaque matin à 6h00.

---
