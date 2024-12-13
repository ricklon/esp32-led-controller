#include <Arduino.h>
#include <FastLED.h>
#include <ESPAsyncWebServer.h>
#include <AsyncTCP.h>
#include <ArduinoOTA.h>
#include <Preferences.h>
#include <ArduinoBLE.h>
#include <qrcode.h>
#include <SPIFFS.h>
#include <WiFi.h>

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
enum AnimationType { VERTICAL_WAVE, SPIRAL, RAINBOW, GRADIENT, CHASE, MANUAL };
AnimationType currentAnimation = VERTICAL_WAVE;

// Manual control variables
CRGB currentColor = CRGB::Red;
bool manualLeds[MAX_LEDS] = {0};

// Function declarations
void setupWiFi();
void setupOTA();
void setupWebServer();
void setupBLE();
void handleWebSocketMessage(void *arg, uint8_t *data, size_t len);
void updateAnimation();
void verticalWave();
void spiral();
void rainbow();
void gradient();
void chase();
void updateManualLeds();

// WiFi setup function
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

// OTA setup function
void setupOTA() {
    ArduinoOTA.setHostname("esp32-s3");
    ArduinoOTA.setPassword("admin123");
    
    ArduinoOTA.onStart([]() {
        FastLED.clear();
        FastLED.show();
    });
    
    ArduinoOTA.begin();
}

// Animation functions
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

void rainbow() {
    static uint8_t hue = 0;
    fill_rainbow(leds, numLeds, hue, 255/numLeds);
    hue += 2 * animationSpeed;
}

void gradient() {
    static float position = 0;
    for(int i = 0; i < numLeds; i++) {
        uint8_t hue = map(i, 0, numLeds-1, 0, 255);
        leds[i] = CHSV(hue + position, 255, 255);
    }
    position += 2 * animationSpeed;
}

void chase() {
    static uint8_t position = 0;
    fadeToBlackBy(leds, numLeds, 20);
    int pos = position;
    for(int i = 0; i < 3; i++) {
        leds[(pos + i) % numLeds] = CHSV(position * 5, 255, 255);
    }
    position += 1 * animationSpeed;
}

void updateManualLeds() {
    if (currentAnimation != MANUAL) return;
    
    for(int i = 0; i < numLeds; i++) {
        leds[i] = manualLeds[i] ? currentColor : CRGB::Black;
    }
}

void handleWebSocketMessage(void *arg, uint8_t *data, size_t len) {
    AwsFrameInfo *info = (AwsFrameInfo*)arg;
    if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT) {
        char *message = new char[len + 1];
        if (!message) {
            Serial.println("Failed to allocate memory for message");
            return;
        }
        
        try {
            memcpy(message, data, len);
            message[len] = '\0';
            
            String cleanMessage = String(message);
            cleanMessage.trim();
            Serial.print("WebSocket received message: ");
            Serial.println(cleanMessage);
            
            if (cleanMessage == "VERTICAL_WAVE") currentAnimation = VERTICAL_WAVE;
            else if (cleanMessage == "SPIRAL") currentAnimation = SPIRAL;
            else if (cleanMessage == "RAINBOW") currentAnimation = RAINBOW;
            else if (cleanMessage == "GRADIENT") currentAnimation = GRADIENT;
            else if (cleanMessage == "CHASE") currentAnimation = CHASE;
            else if (cleanMessage == "MANUAL") {
                currentAnimation = MANUAL;
                memset(manualLeds, 0, sizeof(manualLeds));
            }
            
        } catch (...) {
            Serial.println("Error processing WebSocket message");
        }
        
        delete[] message;
    }
}

void setupWebServer() {
    ws.onEvent([](AsyncWebSocket *server, AsyncWebSocketClient *client, 
                  AwsEventType type, void *arg, uint8_t *data, size_t len) {
        switch(type) {
            case WS_EVT_CONNECT:
                Serial.printf("WebSocket client #%u connected from %s\n", client->id(), client->remoteIP().toString().c_str());
                break;
            case WS_EVT_DISCONNECT:
                Serial.printf("WebSocket client #%u disconnected\n", client->id());
                break;
            case WS_EVT_DATA:
                handleWebSocketMessage(arg, data, len);
                break;
            case WS_EVT_ERROR:
                Serial.printf("WebSocket error #%u: %s\n", client->id(), (char*)data);
                break;
        }
    });
    
    server.addHandler(&ws);
    
    server.serveStatic("/", SPIFFS, "/").setDefaultFile("index.html");
    
    server.on("/set_leds", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (request->hasParam("num")) {
            int newNum = request->getParam("num")->value().toInt();
            if (newNum > 0 && newNum <= MAX_LEDS) {
                numLeds = newNum;
                preferences.putInt("num_leds", numLeds);
                FastLED.addLeds<WS2812B, LED_PIN, RGB>(leds, numLeds);
                request->send(200, "text/plain", "Updated LED count");
            } else {
                request->send(400, "text/plain", "Invalid LED count");
            }
        }
    });
    
    server.on("/set_speed", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (request->hasParam("value")) {
            float newSpeed = request->getParam("value")->value().toFloat();
            if (newSpeed >= 0.1 && newSpeed <= 2.0) {
                animationSpeed = newSpeed;
                request->send(200, "text/plain", "Updated speed");
            } else {
                request->send(400, "text/plain", "Invalid speed value");
            }
        }
    });
    
    server.on("/set_color", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (request->hasParam("r") && request->hasParam("g") && request->hasParam("b")) {
            int r = request->getParam("r")->value().toInt();
            int g = request->getParam("g")->value().toInt();
            int b = request->getParam("b")->value().toInt();
            currentColor = CRGB(r, g, b);
            request->send(200, "text/plain", "Color updated");
        } else {
            request->send(400, "text/plain", "Missing RGB values");
        }
    });

    server.on("/set_led", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (request->hasParam("index") && request->hasParam("state")) {
            int index = request->getParam("index")->value().toInt();
            bool state = request->getParam("state")->value().equals("1");
            
            if (index >= 0 && index < numLeds) {
                manualLeds[index] = state;
                request->send(200, "text/plain", "LED updated");
            } else {
                request->send(400, "text/plain", "Invalid LED index");
            }
        }
    });
    
    server.begin();
    Serial.println("WebServer & WebSocket server started");
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

void updateAnimation() {
    switch (currentAnimation) {
        case VERTICAL_WAVE:
            verticalWave();
            break;
        case SPIRAL:
            spiral();
            break;
        case RAINBOW:
            rainbow();
            break;
        case GRADIENT:
            gradient();
            break;
        case CHASE:
            chase();
            break;
        case MANUAL:
            updateManualLeds();
            break;
    }
}

void setup() {
    Serial.begin(115200);
    delay(1000);  // Give serial time to initialize
    
    Serial.println("\n\nESP32 LED Controller Starting...");
    
    // Initialize SPIFFS
    if(!SPIFFS.begin(true)) {
        Serial.println("SPIFFS Mount Failed");
        return;
    }
    
    // Initialize preferences
    preferences.begin("led-config", false);
    numLeds = preferences.getInt("num_leds", 50);
    
    // Initialize FastLED
    FastLED.addLeds<WS2812B, LED_PIN, RGB>(leds, numLeds);
    FastLED.setBrightness(128);
    
    // Setup all services
    setupWiFi();
    setupOTA();
    setupWebServer();
    setupBLE();
}

void loop() {
    ArduinoOTA.handle();
    updateAnimation();
    FastLED.show();
    delay(20);  // ~50fps
}
