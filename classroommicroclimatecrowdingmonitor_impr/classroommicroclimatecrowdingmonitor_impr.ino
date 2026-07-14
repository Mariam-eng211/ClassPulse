#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <DHT.h>

#define DHTPIN 23
#define DHTTYPE DHT11
#define OBSTACLE_PIN 19
#define BUZZER_PIN 18 // Pin 18 controls your buzzer!

// Node Identity Configuration
const char* CURRENT_NODE_ID = "Node 01";

// Network Details
const char* ssid = "Mariam";
const char* password = "japozundu";

// Supabase Endpoint Configuration
const char* supabase_url = "https://zbemoaxplwzmjuaythzq.supabase.co/rest/v1/desk_telemetry";
const char* supabase_key = "sb_publishable_Bk5pDl547Wx9tRoRonqCQQ_PbrhgsVN";

DHT dht(DHTPIN, DHTTYPE);

int roomCount = 0;
bool lastSensorState = HIGH;
unsigned long lastTransmissionTime = 0;
const unsigned long transmissionInterval = 5000;

// Settings synced directly from your web dashboard sliders
int webMaxCapacity = 35;
int webTempLimit = 28;

// Forward declaration of hardware update function
void updateDeskHardware(bool alertActive, float t, float h);

void setup() {
  Serial.begin(115200);
  dht.begin();
  pinMode(OBSTACLE_PIN, INPUT);
  pinMode(BUZZER_PIN, OUTPUT); // Sets pin 18 to send electricity out to the buzzer

  // Connect to Wi-Fi
  WiFi.setSleep(false); 
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) { 
    delay(500); 
    Serial.print("."); 
  }
  Serial.println("\nWi-Fi Connected!");
}

void loop() {
  // Capture IR beam breaks continuously using your calibrated sensor
  bool currentSensorState = digitalRead(OBSTACLE_PIN);
  if (currentSensorState == LOW && lastSensorState == HIGH) {
    roomCount++;
    Serial.print("Target detected! Room Count: ");
    Serial.println(roomCount);
    delay(150);
  }
  lastSensorState = currentSensorState;

  // Sync with Supabase every 5 seconds
  if (millis() - lastTransmissionTime >= transmissionInterval) {
    lastTransmissionTime = millis();
    
    float h = dht.readHumidity();
    float t = dht.readTemperature();
    if (isnan(h) || isnan(t)) {
      Serial.println("Failed to read from DHT sensor!");
      return;
    }

    // Calculation to check if room is stuffy or overcrowded
    float crowdingRatio = (float)roomCount / (float)webMaxCapacity;
    int stagnationIndex = (t * 1.4) + (h * 0.4) + (crowdingRatio * 35);
    bool stagnationFlag = (stagnationIndex > 75) || (t > webTempLimit) || (roomCount >= webMaxCapacity);

    Serial.print("Sending Data -> Temp: ");
    Serial.print(t);
    Serial.print("C, PAX: ");
    Serial.println(roomCount);

    if (WiFi.status() == WL_CONNECTED) {
      HTTPClient http;
      http.begin(String(supabase_url) + "?order=id.desc&limit=1");

      http.addHeader("apikey", supabase_key);
      http.addHeader("Authorization", String("Bearer ") + supabase_key);
      http.addHeader("Content-Type", "application/json");
      http.addHeader("Prefer", "return=representation");

      StaticJsonDocument<200> jsonDoc;
      jsonDoc["node_id"] = CURRENT_NODE_ID;
      jsonDoc["temperature"] = t;
      jsonDoc["humidity"] = h;
      jsonDoc["room_count"] = roomCount;
      jsonDoc["stagnation_flag"] = stagnationFlag;

      String requestBody;
      serializeJson(jsonDoc, requestBody);
      int httpResponseCode = http.POST(requestBody);

      if (httpResponseCode > 0) {
        Serial.print("Supabase Response Code: ");
        Serial.println(httpResponseCode);
        Serial.println("Supabase Success Response: " + http.getString());
        
        String responseBody = http.getString();
        StaticJsonDocument<500> responseJson;
        DeserializationError err = deserializeJson(responseJson, responseBody);
        
        if (!err && responseJson.is<JsonArray>() && responseJson.size() > 0) {
          JsonObject latestRow = responseJson[0];
          if(latestRow.containsKey("max_capacity")) webMaxCapacity = latestRow["max_capacity"];
          if(latestRow.containsKey("temp_limit")) webTempLimit = latestRow["temp_limit"];
        }
      } else {
        Serial.print("Error sending POST: ");
        Serial.println(httpResponseCode);
      }
      http.end();
    }

    updateDeskHardware(stagnationFlag, t, h);
  }
}

void updateDeskHardware(bool alertActive, float t, float h) {
  if (alertActive) {
    //  Sound the physical buzzer alert!
    digitalWrite(BUZZER_PIN, HIGH);
    delay(200);
    digitalWrite(BUZZER_PIN, LOW);
    Serial.println("!!! STAGNATION ALERT ACTIVE !!!");
  } else {
    //  Keep the buzzer quiet during normal conditions
    digitalWrite(BUZZER_PIN, LOW);
  }
}