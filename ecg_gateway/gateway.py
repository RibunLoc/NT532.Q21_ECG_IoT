import json
import ssl
import time
import paho.mqtt.client as mqtt

# ── Cấu hình Local (ESP32 → Laptop) ─────────────────────
LOCAL_BROKER = "127.0.0.1"
LOCAL_PORT = 1883
LOCAL_TOPICS = [
    ("ecg/result", 0),
    ("ecg/raw", 0)
]

# ── Cấu hình AWS IoT Core ────────────────────────────────
AWS_ENPOINT = "a2blv28aii5w0c-ats.iot.ap-southeast-1.amazonaws.com"
AWS_PORT    = 8883
AWS_CLIENT_ID = "ESP32_ECG_Device"

CERT_PATH = "./certs/cef1c3163b3d49df655f3daf80bc3accb19be785e9d215ffd83ba9efba8fc2e3-certificate.pem.crt"
KEY_PATH = "./certs/cef1c3163b3d49df655f3daf80bc3accb19be785e9d215ffd83ba9efba8fc2e3-private.pem.key"
CA_PATH  = "./certs/AmazonRootCA1.pem"

# ── AWS IoT Client ───────────────────────────────────────
def create_AWS_client():
    client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id=AWS_CLIENT_ID)

    ssl_ctx = ssl.create_default_context()
    ssl_ctx.load_verify_locations(CA_PATH)
    ssl_ctx.load_cert_chain(certfile=CERT_PATH, keyfile=KEY_PATH)
    client.tls_set_context(ssl_ctx)

    def on_connect(client, userdata, flags, reason_code, properties):
        if reason_code == 0:
            print("✅ Kết nối AWS IoT Core thành công!")
        else:
            print(f"❌ Lỗi kết nối AWS: rc={reason_code}")

    def on_publish(client, userdata, mid, reason_code, properties):
        print(f"   ↑ Đã gửi lên AWS (mid={mid})")

    client.on_connect = on_connect
    client.on_publish = on_publish
    return client


# ── Local MQTT Client (nhận từ ESP32) ───────────────────
def create_local_client(aws_client):

    def on_connect(client, userdata, flags, reason_code, properties):
        if reason_code == 0:
            print("✅ Local broker sẵn sàng nhận từ ESP32!")
            client.subscribe(LOCAL_TOPICS)
            print(f"   Đang lắng nghe topics: {[t[0] for t in LOCAL_TOPICS]}")
        else:
            print(f"❌ Lỗi local broker: rc={reason_code}")

    def on_message(client, userdata, msg):
        topic   = msg.topic
        payload = msg.payload.decode('utf-8')

        # Thêm server timestamp thật
        import time, json
        try:
            data = json.loads(payload)
            data['server_time'] = int(time.time() * 1000)  # Unix ms thật
            payload = json.dumps(data)
        except:
            pass

        print(f"\n[LOCAL ←] Topic: {topic}")
        aws_client.publish(topic, payload, qos=1)

    def on_disconnect(client, userdata, flags, reason_code, properties):
        print(f"⚠️ Local broker mất kết nối (rc={reason_code}), đang reconnect...")

    client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id="ECG_Local_Broker")
    client.on_connect    = on_connect
    client.on_message    = on_message
    client.on_disconnect = on_disconnect
    return client

if __name__ == "__main__":
    print("=" * 50)
    print("  ECG IoT Gateway")
    print("  ESP32 → Laptop → AWS IoT Core")
    print("=" * 50)

    # kết nôi AWS IoT Core
    aws = create_AWS_client()
    print(f"⏳ Đang kết nối tới {AWS_ENPOINT}:{AWS_PORT} ...")
    try:
        aws.connect_async(AWS_ENPOINT, AWS_PORT, keepalive=60)
    except Exception as e:
        print(f"❌ Không thể connect AWS: {e}")
        exit(1)
    aws.loop_start()
    for i in range(10):
        time.sleep(1)
        print(f"   ... chờ AWS ({i+1}s), connected={aws.is_connected()}")
        if aws.is_connected():
            break
    if not aws.is_connected():
        print("❌ AWS connect timeout — kiểm tra cert, endpoint, hoặc firewall")
        exit(1)

    local = create_local_client(aws)
    local.connect(LOCAL_BROKER, LOCAL_PORT, keepalive=60)

    print("\n Gateway đang chạy... (Ctrl+C để dừng\n)")

    try:
        local.loop_forever()
    except KeyboardInterrupt:
        print("\n⛔ Dừng gateway...")
        local.disconnect()
        aws.loop_stop()
        aws.disconnect()
        print("✅ Đã dừng sạch sẽ.")