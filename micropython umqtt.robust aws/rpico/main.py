import device, lib.keys as keys, machine, network, ntptime, lib.simple as simple, ssl, time, ubinascii

UTC_OFFSET = -7 * 3600

RESET_TIMES = [(5 * 3600), (16 * 3600)]

led = machine.Pin("LED", machine.Pin.OUT)

def read_pem(file):
    with open(file, "r") as input:
        text = input.read().strip()
        split_text = text.split("\n")
        base64_text = "".join(split_text[1:-1])
        return ubinascii.a2b_base64(base64_text)
        
def led_pulse(n):
    period = 0.2
    for i in range(0,n):
        led.on()
        time.sleep(period / 2.0)
        led.off()
        time.sleep(period / 2.0)
    time.sleep(0.5)

def internet_connect(net):
    print("Connecting to WiFi ...")
    net.active(True)
    net.connect(keys.WIFI_SSID, keys.WIFI_PASSWORD)
    while not net.isconnected():
        led_pulse(2)
    print("Connected to WiFi\n")

def mqtt_callback(topic, msg):
    topic_str = topic.decode()
    msg_str = msg.decode().strip('"')
    print(f"RX: {topic_str}\t{msg_str}")
    if hasattr(device, msg_str):
        led.on()
        getattr(device, msg_str)()

def mqtt_setup():
    mqtt_client = simple.MQTTClient(
        client_id = ubinascii.hexlify(machine.unique_id()),
        server = keys.MQTT_BROKER,
        ssl = True,
        ssl_params = {
            "key": read_pem(keys.MQTT_CLIENT_KEY),
            "cert": read_pem(keys.MQTT_CLIENT_CERT),
            "server_hostname": keys.MQTT_BROKER,
            "cert_reqs": ssl.CERT_REQUIRED,
            "cadata": read_pem(keys.MQTT_BROKER_CA),
        },
    )
    mqtt_client.set_callback(mqtt_callback)
    return mqtt_client

def mqtt_connect(client):
    print("Connecting to MQTT Broker ...")
    led_pulse(3)
    client.connect()
    led_pulse(4)
    client.subscribe(keys.MQTT_TOPIC)
    print("Connected to MQTT Broker\n")

# def mqtt_listen(client):
#     print(f"Listening to {keys.MQTT_TOPIC} ...")
#     while True:
#         client.check_msg()
#         led_pulse(1)

def mqtt_disconnect(client):
    print("Disconnecting from MQTT Broker ...")
    client.disconnect()
    print("Disconnected from MQTT Broker\n")

def time_in_seconds():
    _, _, _, hours, minutes, seconds, _, _ = time.localtime(time.time() + UTC_OFFSET)
    return (hours * 3600) + (minutes * 60) + seconds

def current_minute():
    _, _, _, _, minutes, _, _, _ = time.localtime(time.time() + UTC_OFFSET)
    return minutes

previous_minute = 0
wlan = network.WLAN(network.STA_IF)
while True:
    internet_connect(wlan)
    ntptime.settime()
    mqtt_client = mqtt_setup()
    mqtt_connect(mqtt_client)
    while wlan.isconnected():
        if previous_minute != current_minute():
            mqtt_client.publish(keys.MQTT_TOPIC, "poke", qos = 1)
        previous_minute = current_minute()
        
        if time_in_seconds() in RESET_TIMES:
            mqtt_disconnect(mqtt_client)
            mqtt_connect(mqtt_client)

        mqtt_client.check_msg()
        led_pulse(1)
