import serial
import requests

ser = serial.Serial("COM3", 115200)  # change COM port

API_KEY = "09T0WCVX9FPUAWNN"

while True:
    line = ser.readline().decode().strip()
    weight = float(line)

    requests.get(
        "https://api.thingspeak.com/update",
        params={"api_key": API_KEY, "Poids": weight}
    )
