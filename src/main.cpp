#include "Arduino.h"
#include <ESP8266WiFi.h>
#include <PubSubClient.h>

// Hardware GPIO pins
#define PIN_LED_1 2
#define PIN_LED_2 0
#define PIN_DETECTOR 5
#define PIN_ACTUATOR_A 14
#define PIN_ACTUATOR_B 12
#define PIN_ACTUATOR_EN 13

#define WIFI_SSID "a"
#define WIFI_PASSWORD "a"

#define MQTT_SERVER "192.168.111.10"
#define MQTT_USER "guest"
#define MQTT_PASSWORD "guest"

// Event to dispatch when the dishwasher finished
#define MQTT_TOPIC_FINISHED "home.dishwasher/evt.finished"

// Commands to force open/retract the actuator
#define MQTT_TOPIC_OPEN "home.dishwasher/cmd.open"
#define MQTT_TOPIC_RETRACT "home.dishwasher/cmd.retract"

int detector_count = 0;
int recent_detection_event_count = 0;
unsigned long millis_at_last_detection = 0;
unsigned long millis_at_opening_start = 0;
unsigned long millis_at_retracting_start = 0;

enum states {
  state_listening,
  state_opening,
  state_retracting,
};

int state = state_listening;
char message_buff[100];

WiFiClient esp_client;
PubSubClient mqtt_client(esp_client);

void mqtt_callback(char *topic, const byte *payload, unsigned int length) {
    int i;

    for (i = 0; i < length; i++) {
        message_buff[i] = payload[i];
    }
    message_buff[i] = '\0';
    String msgString = String(message_buff);
    Serial.println("mqtt> " + msgString);

    if (strcmp(topic, MQTT_TOPIC_OPEN) == 0) {
        state = state_opening;
    }

    if (strcmp(topic, MQTT_TOPIC_RETRACT) == 0) {
        state = state_retracting;
    }
}

void setup_wifi() {
    delay(10);
    Serial.println();
    Serial.print("Connecting to '");
    Serial.print(WIFI_SSID);
    Serial.print("' .");

    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    while (WiFi.status() != WL_CONNECTED) {
        delay(100);
        digitalWrite(PIN_LED_1, !digitalRead(PIN_LED_1));
        Serial.print(".");
    }

    Serial.println("OK");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
}
void reconnect() {
    while (!mqtt_client.connected()) {
        Serial.print("Connecting to MQTT broker...");
        if (mqtt_client.connect("dishwasher", MQTT_USER, MQTT_PASSWORD)) {
            digitalWrite(PIN_LED_1, LOW);
            Serial.println("OK");
            mqtt_client.subscribe(MQTT_TOPIC_RETRACT);
            mqtt_client.subscribe(MQTT_TOPIC_OPEN);
        } else {
            digitalWrite(PIN_LED_1, HIGH);
            Serial.print("FAILED: ");
            Serial.print(mqtt_client.state());
            Serial.println(" Retrying...");
            delay(1000);
        }
    }
}
void listen() {
    if (!digitalRead(PIN_DETECTOR)) {
        detector_count++;
        if (detector_count > 500) {
            unsigned long elapsed_millis = millis() - millis_at_last_detection;
            Serial.print("detected: ");
            Serial.println(detector_count);
            Serial.print("    elapsed: ");
            Serial.println(elapsed_millis);
            detector_count = 0;
            if (elapsed_millis > 5000 && elapsed_millis < 7000) {
                recent_detection_event_count++;
                Serial.println("    strike");
            }
            millis_at_last_detection = millis();
        }
    } else {
        detector_count--;
        if (detector_count <= 0) {
            detector_count = 0;
        }
    }

    if (millis() - millis_at_last_detection > 10000) {
        recent_detection_event_count--;
        if (recent_detection_event_count <= 0) {
            recent_detection_event_count = 0;
        }
    }

    if (recent_detection_event_count > 3) {
        state = state_opening;
        recent_detection_event_count = 0;
    }
}

void update_actuator() {
    switch (state) {
        case state_opening:
            digitalWrite(PIN_ACTUATOR_EN, HIGH);
            digitalWrite(PIN_ACTUATOR_A, LOW);
            digitalWrite(PIN_ACTUATOR_B, HIGH);
            break;
        case state_retracting:
            digitalWrite(PIN_ACTUATOR_EN, HIGH);
            digitalWrite(PIN_ACTUATOR_A, HIGH);
            digitalWrite(PIN_ACTUATOR_B, LOW);
            break;
        default:
            listen();
            digitalWrite(PIN_ACTUATOR_EN, LOW);
            digitalWrite(PIN_ACTUATOR_A, LOW);
            digitalWrite(PIN_ACTUATOR_B, LOW);
    }
}

void print_state() {
    switch (state) {
        case state_listening:
            Serial.println("state_listening");
            break;
        case state_opening:
            Serial.println("state_opening");
            break;
        case state_retracting:
            Serial.println("state_retracting");
            break;
        default:
            Serial.println("state_?");
    }
}

void setup() {
    pinMode(PIN_LED_1, OUTPUT);
    pinMode(PIN_LED_2, OUTPUT);
    pinMode(PIN_ACTUATOR_A, OUTPUT);
    pinMode(PIN_ACTUATOR_B, OUTPUT);
    pinMode(PIN_ACTUATOR_EN, OUTPUT);
    pinMode(PIN_DETECTOR, INPUT);

    digitalWrite(PIN_LED_1, HIGH);
    digitalWrite(PIN_LED_2, LOW);

    Serial.begin(9600);

    setup_wifi();
    mqtt_client.setServer(MQTT_SERVER, 1883);
    mqtt_client.setCallback(mqtt_callback);
    state = state_listening;
    update_actuator();
    print_state();
}

void loop() {
    if (!mqtt_client.connected()) {
        reconnect();
    }
    mqtt_client.loop();

    delay(1);

    int previous_state = state;

    switch (state) {
        case state_listening:
            listen();
            break;
        case state_opening:
            if (millis() - millis_at_opening_start > 5000) {
                state = state_retracting;
            }
            break;
        case state_retracting:
            if (millis() - millis_at_retracting_start > 10000) {
                state = state_listening;
            }
            break;
    }

    if (previous_state != state) {
        update_actuator();
        print_state();
        if (state == state_opening) {
            millis_at_opening_start = millis();
        }
        if (state == state_retracting) {
            millis_at_retracting_start = millis();
        }
    }

}
