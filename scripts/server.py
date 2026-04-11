import os
import sqlite3
import socket
import smtplib
from datetime import datetime, timedelta
from email.mime.text import MIMEText
from email.mime.multipart import MIMEMultipart
from fastapi import FastAPI, Request
from apscheduler.schedulers.background import BackgroundScheduler

app = FastAPI()

# --- CONFIGURATION ---
DB_PATH = "ruche_data.db"
SENDER_EMAIL = os.getenv("SENDER_EMAIL")
SENDER_PASSWORD = os.getenv("SENDER_PASSWORD")
CLIENT_EMAIL = os.getenv("CLIENT_EMAIL")
PROF_EMAIL = os.getenv("PROF_EMAIL")

# --- INITIALISATION DB ---
def init_db():
    conn = sqlite3.connect(DB_PATH)
    conn.execute('''CREATE TABLE IF NOT EXISTS daily_alerts 
                 (id INTEGER PRIMARY KEY AUTOINCREMENT, type TEXT, timestamp DATETIME)''')
    conn.execute('''CREATE TABLE IF NOT EXISTS states (key TEXT PRIMARY KEY, value INTEGER)''')
    conn.commit()
    conn.close()

init_db()

# --- FONCTIONS UTILITAIRES ---
def get_state(key):
    conn = sqlite3.connect(DB_PATH)
    res = conn.execute("SELECT value FROM states WHERE key=?", (key,)).fetchone()
    conn.close()
    return res[0] if res else 0

def set_state(key, value):
    conn = sqlite3.connect(DB_PATH)
    conn.execute("INSERT OR REPLACE INTO states (key, value) VALUES (?, ?)", (key, value))
    conn.commit()
    conn.close()

def log_alert(alert_type):
    conn = sqlite3.connect(DB_PATH)
    conn.execute("INSERT INTO daily_alerts (type, timestamp) VALUES (?, ?)", 
                 (alert_type, datetime.now().strftime("%Y-%m-%d %H:%M:%S")))
    conn.commit()
    conn.close()

def log_alert_once(alert_type):
    conn = sqlite3.connect(DB_PATH)
    today_start = datetime.now().strftime("%Y-%m-%d 00:00:00")
    res = conn.execute("SELECT COUNT(*) FROM daily_alerts WHERE type=? AND timestamp >= ?", 
                       (alert_type, today_start)).fetchone()
    if res[0] == 0:
        conn.execute("INSERT INTO daily_alerts (type, timestamp) VALUES (?, ?)", 
                     (alert_type, datetime.now().strftime("%Y-%m-%d %H:%M:%S")))
        conn.commit()
    conn.close()

# --- MOTEUR D'ENVOI EMAIL HTML ---
def send_html_mail(subject, html_content):
    try:
        remote_ip = socket.gethostbyname("smtp.gmail.com")
        msg = MIMEMultipart("alternative")
        msg['Subject'] = subject
        msg['From'] = f"Open Ruche System <{SENDER_EMAIL}>"
        
        # NOUVEAU : On crée une liste avec les emails valides et on les sépare par une virgule
        recipients = [email for email in [CLIENT_EMAIL, PROF_EMAIL] if email]
        msg['To'] = ", ".join(recipients)
        
        msg.attach(MIMEText(html_content, "html"))
        
        with smtplib.SMTP_SSL(remote_ip, 465, timeout=15) as server:
            server.login(SENDER_EMAIL, SENDER_PASSWORD)
            server.send_message(msg)
        print(f"✅ Mail envoyé à {len(recipients)} destinataire(s) : {subject}")
    except Exception as e:
        print(f"❌ Erreur Mail : {e}")

# --- TEMPLATES HTML ---
def get_alert_template(title, message, color="#e74c3c"):
    return f"""
    <html>
    <body style="font-family: Arial, sans-serif; color: #333; line-height: 1.6;">
        <div style="max-width: 600px; margin: 20px auto; border: 1px solid #ddd; border-radius: 8px; overflow: hidden;">
            <div style="background-color: {color}; color: white; padding: 20px; text-align: center;">
                <h1 style="margin: 0; font-size: 24px;">🚨 {title}</h1>
            </div>
            <div style="padding: 20px;">
                <p>Bonjour,</p>
                <p>Le système de surveillance <strong>Open Ruche</strong> vient de détecter un événement critique :</p>
                <div style="background-color: #f9f9f9; border-left: 5px solid {color}; padding: 15px; margin: 20px 0;">
                    {message}
                </div>
                <p><strong>Action recommandée :</strong> Veuillez vous rendre sur place ou vérifier vos caméras de surveillance dès que possible.</p>
                <hr style="border: 0; border-top: 1px solid #eee; margin: 20px 0;">
                <p style="font-size: 12px; color: #888;">ID Ruche : Ruche P7 - G2_FISE_2026_BeeLive | Généré à {datetime.now().strftime('%H:%M:%S')}</p>
            </div>
        </div>
    </body>
    </html>
    """

# --- TÂCHE PLANIFIÉE (BILAN MATINAL) ---
def morning_briefing():
    yesterday_str = (datetime.now() - timedelta(days=1)).strftime("%Y-%m-%d %H:%M:%S")
    conn = sqlite3.connect(DB_PATH)
    
    alerts = conn.execute("""
        SELECT type, COUNT(*), GROUP_CONCAT(strftime('%H:%M', timestamp), ', ') 
        FROM daily_alerts 
        WHERE timestamp > ? 
        GROUP BY type
    """, (yesterday_str,)).fetchall()
    
    if alerts:
        rows = "".join([f"<li style='margin-bottom: 8px;'><strong>{a[0]}</strong> : {a[1]} détection(s) <br><span style='color: #7f8c8d; font-size: 0.9em;'>🕒 Heure(s) : {a[2]}</span></li>" for a in alerts])
        html = f"""
        <html>
        <body style="font-family: Arial, sans-serif;">
            <div style="max-width: 600px; margin: 20px auto; border: 1px solid #ddd; border-radius: 8px;">
                <div style="background-color: #2980b9; color: white; padding: 20px; text-align: center;">
                    <h1 style="margin: 0;">📊 Bilan Quotidien</h1>
                </div>
                <div style="padding: 20px;">
                    <p>Voici le résumé des incidents détectés ces dernières 24h :</p>
                    <ul style="list-style-type: square;">{rows}</ul>
                    <p>Aucune action urgente n'est requise, mais une vérification lors de votre prochaine visite est conseillée.</p>
                </div>
            </div>
        </body>
        </html>
        """
        send_html_mail("[BILAN MATINAL] Rapport d'activité Open Ruche", html)
        conn.execute("DELETE FROM daily_alerts WHERE timestamp < ?", (yesterday_str,))
        conn.commit()
    conn.close()

scheduler = BackgroundScheduler()
scheduler.add_job(morning_briefing, 'cron', hour=6, minute=0)
scheduler.start()

@app.post("/lora-uplink")
async def receive_uplink(request: Request):
    payload = await request.json()
    data = payload.get("uplink_message", {}).get("decoded_payload", {})
    if not data: return {"status": "no_data"}

    ia_code = data.get("t_4", 0)
    orientation = data.get("orientation_id", 0)
    poids = data.get("weight_kg", 0)

    # 1. ALERTES TEMPS RÉEL (Urgence Rouge)
    if ia_code == 2 and get_state("essaimage") == 0:
        msg = f"<strong>Essaimage en cours !</strong><br>Poids actuel : {poids} kg."
        send_html_mail("🚀 [URGENT] Essaimage détecté", get_alert_template("Essaimage", msg))
        set_state("essaimage", 1)
    elif ia_code != 2: set_state("essaimage", 0)

    if ia_code == 4 and get_state("piping") == 0:
        msg = "<strong>Chant de la Reine (Piping) !</strong><br>Un essaimage est imminent."
        send_html_mail("🎶 [URGENT] Piping détecté", get_alert_template("Piping Reine", msg, "#9b59b6"))
        set_state("piping", 1)
    elif ia_code != 4: set_state("piping", 0)

    # 2. LOGS POUR LE BILAN (Sans mail immédiat)
    if ia_code == 1: log_alert("Présence Frelon Asiatique")
    if ia_code == 3: log_alert("Absence de Reine")
    
    # Logique de chute réaliste avec protection d'initialisation
    prev_orientation = get_state("orientation")
    
    if prev_orientation == 0:
        if orientation != 0:
            set_state("orientation", orientation)
    elif orientation != prev_orientation:
        log_alert_once("Mouvement ou chute de la ruche")
        set_state("orientation", orientation)

    if poids < 1.0: 
        log_alert_once("Suspicion de Vol")

    return {"status": "processed"}
