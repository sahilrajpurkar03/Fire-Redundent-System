#include <Wire.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <Adafruit_MLX90640.h>
#include "secrets.h"  // WiFi credentials and fire alert URL

// ------------------------
// Pin Definitions
// ------------------------
#define RELAY_PIN 34            // GPIO pin connected to relay module

// ------------------------
// System Configuration
// ------------------------
#define FIRE_THRESHOLD 60.0     // Temperature threshold in Celsius
#define ACTUATOR_DURATION 120000 // Actuator run time in milliseconds (2 minutes)

// ------------------------
// Global Variables
// ------------------------
Adafruit_MLX90640 mlx;
float frame[32 * 24];            // 32x24 thermal image frame
bool fireTriggered = false;      // Tracks if fire response is active
bool messageSent = false;        // Tracks if alert message was sent
unsigned long fireStartTime = 0; // Timestamp when fire was detected

// ------------------------
// Setup Function
// ------------------------
void setup() {
  Serial.begin(115200);

  // Configure relay pin
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, LOW);  // Ensure relay is off at startup

  // Initialize I2C for thermal sensor
  Wire.begin();
  if (!mlx.begin(MLX90640_I2CADDR_DEFAULT, &Wire)) {
    Serial.println("Error: MLX90640 sensor not found.");
    while (true); // Halt execution if sensor not found
  }

  // Configure MLX90640 sensor settings
  mlx.setMode(MLX90640_CHESS);
  mlx.setResolution(MLX90640_ADC_18BIT);
  mlx.setRefreshRate(MLX90640_4_HZ); // Approx. 4 frames per second

  // Connect to WiFi network
  connectToWiFi();
}

// ------------------------
// Connect to WiFi
// ------------------------
void connectToWiFi() {
  Serial.print("Connecting to WiFi");
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  int retries = 0;
  while (WiFi.status() != WL_CONNECTED && retries < 20) {
    delay(500);
    Serial.print(".");
    retries++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi connected");
    Serial.print("IP Address: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("\nFailed to connect to WiFi");
  }
}

// ------------------------
// Send HTTP Fire Alert
// ------------------------
void sendFireAlert() {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    http.begin(FIRE_ALERT_URL);
    http.addHeader("Content-Type", "application/json");

    String payload = "{\"status\": \"fire_detected\", \"device\": \"ESP32-S3\", \"temp\": 60}";
    int responseCode = http.POST(payload);

    if (responseCode > 0) {
      Serial.print("Fire alert sent successfully. HTTP response code: ");
      Serial.println(responseCode);
    } else {
      Serial.print("Error sending alert: ");
      Serial.println(http.errorToString(responseCode));
    }

    http.end();
  } else {
    Serial.println("WiFi not connected. Cannot send fire alert.");
  }
}

// ------------------------
// Main Loop
// ------------------------
void loop() {
  // Attempt to read a frame from the thermal sensor
  if (!mlx.getFrame(frame)) {
    Serial.println("Failed to read thermal frame.");
    return;
  }

  // Check if any pixel exceeds the fire temperature threshold
  bool fireDetected = false;
  for (int i = 0; i < 32 * 24; i++) {
    if (frame[i] >= FIRE_THRESHOLD) {
      fireDetected = true;
      break;
    }
  }

  // Handle fire detection event
  if (fireDetected && !fireTriggered) {
    Serial.println("Fire detected. Activating relay.");
    digitalWrite(RELAY_PIN, HIGH);  // Turn on actuator
    fireTriggered = true;
    fireStartTime = millis();

    if (!messageSent) {
      sendFireAlert();             // Send HTTP notification
      messageSent = true;
    }
  }

  // Turn off actuator after timeout
  if (fireTriggered) {
    if (millis() - fireStartTime >= ACTUATOR_DURATION) {
      Serial.println("Actuator timeout reached. Deactivating relay.");
      digitalWrite(RELAY_PIN, LOW); // Turn off actuator
      fireTriggered = false;
      messageSent = false;
    }
  }

  delay(250); // Wait between frames (matches 4Hz senso
