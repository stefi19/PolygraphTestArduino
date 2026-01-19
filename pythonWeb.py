import time
from collections import deque
from flask import Flask, render_template_string
from flask_socketio import SocketIO, emit
import serial
import threading

# ---------------- Polygraph Class ----------------
class Polygraph:
    def __init__(self, calibration_time=15):
        self.calibration_time = calibration_time
        self.start_time = None
        self.calibrated = False
        self.baseline = {}
        self.data_history = deque(maxlen=1000)
        self.exceed_count = 0
        self.total_count = 0

    def update(self, data):
        now = time.time()

        if self.start_time is None:
            self.start_time = now
            print("Starting calibration...")

        if not self.calibrated:
            for k in ['ekg', 'gsr', 'sound']:
                if k not in self.baseline:
                    self.baseline[k] = []
                self.baseline[k].append(data[k])

            if now - self.start_time >= self.calibration_time:
                for k in self.baseline:
                    vals = self.baseline[k]
                    if len(vals) == 0:
                        vals = [0]
                    avg = sum(vals) / len(vals)
                    std = (sum((v - avg) ** 2 for v in vals) / len(vals)) ** 0.5
                    self.baseline[k] = {'avg': avg, 'std': std}
                self.calibrated = True
                print("Calibration done:", self.baseline)
            return None

        self.data_history.append(data)
        exceed = 0
        thresholds = {'ekg': 1.2, 'gsr': 1.2, 'sound': 1.3}
        for k in ['ekg', 'gsr', 'sound']:
            if data[k] > self.baseline[k]['avg'] * thresholds[k]:
                exceed += 1
        self.total_count += 1
        self.exceed_count += (exceed > 0)
        maybe_lying_prob = (self.exceed_count / self.total_count) * 100
        return maybe_lying_prob

# ---------------- Flask Setup ----------------
app = Flask(__name__)
app.config['SECRET_KEY'] = 'secret!'
socketio = SocketIO(app, cors_allowed_origins="*", async_mode="threading")

# ---------------- HTML Template ----------------
with open("BLE.html", "r") as f:
    DASHBOARD_HTML = f.read()

@app.route('/')
def index():
    return render_template_string(DASHBOARD_HTML)

# ---------------- Serial Ports ----------------
ARDUINO_PORT = 'COM10'
ARDUINO_BAUD = 9600
ESP32_PORT = 'COM16'
ESP32_BAUD = 9600

# ---------------- Parse Arduino Serial ----------------
def parse_line(line):
    try:
        parts = line.split('|')
        data = {}
        for part in parts:
            key, value = part.split(':')
            key = key.strip().lower()
            value = value.strip()
            if value.isdigit():
                value = int(value)
            elif value.lower() in ['true','false']:
                value = value.lower() == 'true'
            data[key] = value
        return data
    except:
        return None

# ---------------- Serial Reading Thread ----------------
def read_serial():
    try:
        ser = serial.Serial(ARDUINO_PORT, ARDUINO_BAUD, timeout=1)
        time.sleep(2)
        print(f"Connected to Arduino on {ARDUINO_PORT}")
    except Exception as e:
        print("Error connecting to Arduino:", e)
        return

    poly = Polygraph(calibration_time=15)
    last_sent_time = 0

    while True:
        try:
            line = ser.readline().decode('utf-8', errors='ignore').strip()
            if not line:
                continue
            data = parse_line(line)
            if not data:
                continue

            score = poly.update(data)
            if score is not None:
                data['maybe_lying'] = round(score, 1)
            else:
                data['maybe_lying'] = None

            # Send data to dashboard
            socketio.emit('sensor_data', data)

            # Send stress percentage to ESP32 briefly every 3 seconds
            if score is not None and time.time() - last_sent_time > 10:
                stress_percent = int(score)
                try:
                    # Open -> write -> close
                    with serial.Serial(ESP32_PORT, ESP32_BAUD, timeout=1) as esp:
                        esp.write(f"{stress_percent}\n".encode())
                    print(f"Sent {stress_percent}% to ESP32 (briefly)")
                except Exception as e:
                    print("Error writing to ESP32:", e)
                last_sent_time = time.time()

        except Exception as e:
            print("Error in serial loop:", e)
            continue

# ---------------- Start Serial Thread ----------------
serial_thread = threading.Thread(target=read_serial)
serial_thread.daemon = True
serial_thread.start()

# ---------------- SocketIO Event ----------------
@socketio.on('connect')
def on_connect():
    print("Client connected")
    emit('message', {'data': 'Connected to server'})

# ---------------- Run Flask ----------------
if __name__ == '__main__':
    socketio.run(app, host='0.0.0.0', port=5000, allow_unsafe_werkzeug=True)