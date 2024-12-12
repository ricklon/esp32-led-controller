// main.cpp
#include <Arduino.h>
#include <FastLED.h>
#include <ESPAsyncWebServer.h>
#include <AsyncTCP.h>
#include <ArduinoOTA.h>
#include <Preferences.h>
#include <ArduinoBLE.h>
#include <qrcode.h>
#include <SPIFFS.h>

// Pin Definitions
#define LED_PIN 5
#define MAX_LEDS 300

// Global Variables
CRGB leds[MAX_LEDS];
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");
Preferences preferences;

// Configuration variables
int numLeds = 50;  // Default value
float animationSpeed = 1.0;
enum AnimationType { VERTICAL_WAVE, SPIRAL };
AnimationType currentAnimation = VERTICAL_WAVE;

// Function declarations
void setupWiFi();
void setupOTA();
void setupWebServer();
void setupBLE();
void handleWebSocketMessage(void *arg, uint8_t *data, size_t len);
void generateQRCode();
void updateAnimation();
void verticalWave();
void spiral();

void setup() {
    Serial.begin(115200);
    
    // Initialize SPIFFS
    if(!SPIFFS.begin(true)) {
        Serial.println("SPIFFS Mount Failed");
        return;
    }
    
    // Initialize preferences
    preferences.begin("led-config", false);
    numLeds = preferences.getInt("num_leds", 50);
    
    // Initialize FastLED
    FastLED.addLeds<WS2812B, LED_PIN, GRB>(leds, numLeds);
    FastLED.setBrightness(128);
    
    setupWiFi();
    setupOTA();
    setupWebServer();
    setupBLE();
    generateQRCode();
}

void loop() {
    ArduinoOTA.handle();
    updateAnimation();
    FastLED.show();
    delay(20);  // ~50fps
}

void setupWiFi() {
    WiFi.mode(WIFI_AP_STA);
    WiFi.softAP("ESP32-LED-Controller", "password123");
    WiFi.begin("dogeden-5g", "rumi1234mayim");
    
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    
    Serial.println("\nConnected to WiFi");
    Serial.println(WiFi.localIP());
}

void setupOTA() {
    ArduinoOTA.setHostname("esp32-s3");
    ArduinoOTA.setPassword("admin123");
    
    ArduinoOTA.onStart([]() {
        FastLED.clear();
        FastLED.show();
    });
    
    ArduinoOTA.begin();
}

void setupWebServer() {
    ws.onEvent([](AsyncWebSocket *server, AsyncWebSocketClient *client, 
                  AwsEventType type, void *arg, uint8_t *data, size_t len) {
        if (type == WS_EVT_DATA) {
            handleWebSocketMessage(arg, data, len);
        }
    });
    
    server.addHandler(&ws);
    
    // Serve static files from SPIFFS
    server.serveStatic("/", SPIFFS, "/").setDefaultFile("index.html");
    
    // API endpoints
    server.on("/set_leds", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (request->hasParam("num")) {
            int newNum = request->getParam("num")->value().toInt();
            if (newNum > 0 && newNum <= MAX_LEDS) {
                numLeds = newNum;
                preferences.putInt("num_leds", numLeds);
                FastLED.addLeds<WS2812B, LED_PIN, GRB>(leds, numLeds);
                request->send(200, "text/plain", "Updated LED count");
            } else {
                request->send(400, "text/plain", "Invalid LED count");
            }
        }
    });
    
    server.on("/set_speed", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (request->hasParam("value")) {
            animationSpeed = request->getParam("value")->value().toFloat();
            request->send(200, "text/plain", "Updated speed");
        }
    });
    
    server.on("/reset", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send(200, "text/plain", "Restarting...");
        delay(1000);
        ESP.restart();
    });
    
    server.begin();
}

void setupBLE() {
    if (!BLE.begin()) {
        Serial.println("Failed to initialize BLE!");
        return;
    }
    
    BLE.setLocalName("ESP32-S3 LED Controller");
    BLE.setAdvertisedService("LED Control");
    BLE.advertise();
}

void handleWebSocketMessage(void *arg, uint8_t *data, size_t len) {
    AwsFrameInfo *info = (AwsFrameInfo*)arg;
    if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT) {
        String message = String((char*)data);
        if (message == "VERTICAL_WAVE") {
            currentAnimation = VERTICAL_WAVE;
        } else if (message == "SPIRAL") {
            currentAnimation = SPIRAL;
        }
    }
}

void generateQRCode() {
    QRCode qrcode;
    uint8_t qrcodeData[qrcode_getBufferSize(3)];
    String url = "http://esp32-s3.local";
    
    qrcode_initText(&qrcode, qrcodeData, 3, ECC_LOW, url.c_str());
    
    Serial.println("\nQR Code for " + url + ":");
    for (int y = 0; y < qrcode.size; y++) {
        for (int x = 0; x < qrcode.size; x++) {
            Serial.print(qrcode_getModule(&qrcode, x, y) ? "##" : "  ");
        }
        Serial.println();
    }
}

void updateAnimation() {
    switch (currentAnimation) {
        case VERTICAL_WAVE:
            verticalWave();
            break;
        case SPIRAL:
            spiral();
            break;
    }
}

void verticalWave() {
    static float position = 0;
    for (int i = 0; i < numLeds; i++) {
        float wave = sin(i * 0.2 + position);
        leds[i] = CHSV(wave * 128 + 128, 255, 255);
    }
    position += 0.1 * animationSpeed;
}

void spiral() {
    static float position = 0;
    for (int i = 0; i < numLeds; i++) {
        float angle = (i * 256.0 / numLeds) + position;
        leds[i] = CHSV(angle, 255, 255);
    }
    position += 2 * animationSpeed;
}