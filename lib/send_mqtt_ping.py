"""
Simple MQTT publisher to Adafruit IO to trigger the beeper.

Topic: <ADA_USERNAME>/feeds/<FEED_KEY>
Port: 1883 (non-TLS) or 8883 (TLS). We'll use TLS (8883).
"""

import ssl
import time
from paho.mqtt import client as mqtt_client

ADA_USERNAME = ""
ADA_KEY = ""
FEED_KEY = "beeper"

BROKER = "io.adafruit.com"
PORT = 8883  # TLS port

topic = f"{ADA_USERNAME}/feeds/{FEED_KEY}"
client_id = f"py-trigger-{int(time.time())}"

def on_connect(client, userdata, flags, rc):
    if rc == 0:
        print("Connected to MQTT broker")
    else:
        print("Failed to connect, rc:", rc)

def main():
    client = mqtt_client.Client(client_id=client_id)
    client.username_pw_set(ADA_USERNAME, ADA_KEY)
    client.tls_set()      # Use system CA certs
    client.tls_insecure_set(False)  # change to True if cert issues
    client.on_connect = on_connect
    client.connect(BROKER, PORT)
    client.loop_start()
    time.sleep(1)
    print("Publishing 'true' to", topic)
    client.publish(topic, "true")
    time.sleep(1)
    client.loop_stop()
    client.disconnect()
    print("Done.")

if __name__ == "__main__":
    main()
