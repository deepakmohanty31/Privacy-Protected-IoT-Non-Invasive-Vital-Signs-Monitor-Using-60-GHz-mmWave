#include <Arduino.h>
#include <ArduinoJson.h>
#include <DFRobot_HumanDetection.h>
#include <LittleFS.h>
#include <WebServer.h>
#include <WebSocketsServer.h>
#include <WiFi.h>
#include <cmath>

namespace {

constexpr char kApSsid[] = "C1001-Monitor";
constexpr char kApPassword[] = "radar12345";
constexpr uint8_t kSensorRxPin = 16;
constexpr uint8_t kSensorTxPin = 17;
constexpr uint32_t kSensorBaud = 115200;
constexpr size_t kHistoryCapacity = 720;
constexpr uint32_t kBroadcastIntervalMs = 1000;
constexpr uint32_t kHistorySampleIntervalMs = 2000;
constexpr uint32_t kLogSampleIntervalMs = 5000;
constexpr uint32_t kSensorPollIntervalMs = 1000;
constexpr size_t kLogRotateBytes = 1024 * 1024;

enum class SensorMode : uint8_t {
  Fall = 1,
  Sleep = 2,
};

HardwareSerial radarSerial(1);
DFRobot_HumanDetection humanRadar(&radarSerial);
WebServer server(80);
WebSocketsServer ws(81);

struct VitalSample {
  uint32_t millisStamp = 0;
  float breathRate = NAN;
  float heartRate = NAN;
  float motionScore = NAN;
  bool present = false;
  uint8_t movementState = 0;
  uint8_t confidence = 0;
};

VitalSample history[kHistoryCapacity];
size_t historyCount = 0;
size_t historyHead = 0;

VitalSample latest;
VitalSample lastLogged;
uint32_t lastBroadcastAt = 0;
uint32_t lastHistoryAt = 0;
uint32_t lastLogAt = 0;
uint32_t lastHistorySourceMillis = 0;
uint32_t lastSensorPollAt = 0;
uint32_t lastDebugPrintAt = 0;
bool hasNewSensorData = false;
bool websocketDirty = true;
bool filesystemReady = false;
bool sensorReady = false;
uint32_t sensorReads = 0;
uint32_t sensorErrors = 0;
SensorMode desiredMode = SensorMode::Sleep;
uint8_t currentWorkMode = 0;
uint16_t fallState = 0;
uint16_t fallBreakHeight = 0;
uint16_t staticResidencyState = 0;
uint16_t lastHumanDistance = 0;
uint8_t lastBreatheState = 0;
uint16_t lastSleepState = 0;
uint16_t lastInBedState = 0;
uint8_t lastAverageRespiration = 0;
uint8_t lastAverageHeartbeat = 0;

String uptimeString(uint32_t ms) {
  const uint32_t seconds = ms / 1000;
  const uint32_t hours = seconds / 3600;
  const uint32_t minutes = (seconds % 3600) / 60;
  const uint32_t secs = seconds % 60;
  char buffer[16];
  snprintf(buffer, sizeof(buffer), "%02lu:%02lu:%02lu",
           static_cast<unsigned long>(hours),
           static_cast<unsigned long>(minutes),
           static_cast<unsigned long>(secs));
  return String(buffer);
}

bool isFiniteInRange(float value, float minValue, float maxValue) {
  return isfinite(value) && value >= minValue && value <= maxValue;
}

const char *movementStateLabel(uint8_t state) {
  switch (state) {
    case 0: return "none";
    case 1: return "still";
    case 2: return "active";
    default: return "unknown";
  }
}

const char *workModeLabel(uint8_t mode) {
  switch (mode) {
    case 1: return "fall";
    case 2: return "sleep";
    default: return "unknown";
  }
}

const char *desiredModeLabel() {
  return desiredMode == SensorMode::Fall ? "fall" : "sleep";
}

DFRobot_HumanDetection::eWorkMode desiredModeLibraryValue() {
  return desiredMode == SensorMode::Fall ? humanRadar.eFallingMode : humanRadar.eSleepMode;
}

const char *fallStateLabel(uint16_t state) {
  switch (state) {
    case 0: return "normal";
    case 1: return "suspected";
    case 2: return "confirmed";
    default: return "unknown";
  }
}

const char *breatheStateLabel(uint8_t state) {
  switch (state) {
    case 1: return "normal";
    case 2: return "fast";
    case 3: return "slow";
    case 4: return "none";
    default: return "unknown";
  }
}

const char *sleepStateLabel(uint16_t state) {
  switch (state) {
    case 0: return "none";
    case 1: return "awake";
    case 2: return "light";
    case 3: return "deep";
    default: return "unknown";
  }
}

void updatePresenceConfidence() {
  uint8_t confidence = 0;
  if (latest.present) confidence += 35;
  if (desiredMode == SensorMode::Sleep) {
    if (isFiniteInRange(latest.breathRate, 10.0f, 25.0f)) confidence += 35;
    if (isFiniteInRange(latest.heartRate, 60.0f, 100.0f)) confidence += 20;
    if (latest.movementState == 1 || latest.movementState == 2) confidence += 10;
  } else {
    if (fallState > 0) confidence += 45;
    if (staticResidencyState > 0) confidence += 20;
  }
  latest.confidence = confidence;
}

void markSensorUpdate() {
  latest.millisStamp = millis();
  updatePresenceConfidence();
  hasNewSensorData = true;
  websocketDirty = true;
}

void pushHistorySample() {
  history[historyHead] = latest;
  historyHead = (historyHead + 1) % kHistoryCapacity;
  if (historyCount < kHistoryCapacity) {
    historyCount++;
  }
}

bool shouldLogSample() {
  if (!latest.present && !isfinite(latest.breathRate) && !isfinite(latest.heartRate) && !isfinite(latest.motionScore)) {
    return false;
  }

  if (!lastLogged.present && !isfinite(lastLogged.breathRate) && !isfinite(lastLogged.heartRate) && !isfinite(lastLogged.motionScore)) {
    return true;
  }

  const auto changedEnough = [](float current, float previous, float threshold) {
    if (isfinite(current) != isfinite(previous)) {
      return true;
    }
    if (!isfinite(current) && !isfinite(previous)) {
      return false;
    }
    return fabsf(current - previous) >= threshold;
  };

  return changedEnough(latest.breathRate, lastLogged.breathRate, 0.1f) ||
         changedEnough(latest.heartRate, lastLogged.heartRate, 0.1f) ||
         changedEnough(latest.motionScore, lastLogged.motionScore, 1.0f) ||
         latest.present != lastLogged.present ||
         latest.movementState != lastLogged.movementState ||
         latest.confidence != lastLogged.confidence;
}

void rotateLogIfNeeded() {
  if (!filesystemReady) {
    return;
  }

  File current = LittleFS.open("/vitals.csv", "r");
  if (!current) {
    return;
  }
  const size_t size = current.size();
  current.close();
  if (size < kLogRotateBytes) {
    return;
  }

  if (LittleFS.exists("/vitals_prev.csv")) {
    LittleFS.remove("/vitals_prev.csv");
  }
  LittleFS.rename("/vitals.csv", "/vitals_prev.csv");
}

void ensureLogHeader() {
  if (!filesystemReady || LittleFS.exists("/vitals.csv")) {
    return;
  }

  File file = LittleFS.open("/vitals.csv", "w");
  if (!file) {
    return;
  }

  file.println("millis,uptime,breath_bpm,heart_bpm,motion_score,present,movement_state,confidence");
  file.close();
}

String csvFloat(float value, uint8_t digits = 2) {
  if (!isfinite(value)) {
    return "";
  }
  return String(value, static_cast<unsigned int>(digits));
}

void appendLogSample() {
  if (!filesystemReady) {
    return;
  }

  rotateLogIfNeeded();
  ensureLogHeader();

  File file = LittleFS.open("/vitals.csv", "a");
  if (!file) {
    return;
  }

  file.print(latest.millisStamp);
  file.print(',');
  file.print(uptimeString(latest.millisStamp));
  file.print(',');
  file.print(csvFloat(latest.breathRate));
  file.print(',');
  file.print(csvFloat(latest.heartRate));
  file.print(',');
  file.print(csvFloat(latest.motionScore));
  file.print(',');
  file.print(latest.present ? "1" : "0");
  file.print(',');
  file.print(latest.movementState);
  file.print(',');
  file.println(latest.confidence);
  file.close();

  lastLogged = latest;
}

String contentTypeForPath(const String &path) {
  if (path.endsWith(".html")) return "text/html";
  if (path.endsWith(".css")) return "text/css";
  if (path.endsWith(".js")) return "application/javascript";
  if (path.endsWith(".json")) return "application/json";
  if (path.endsWith(".svg")) return "image/svg+xml";
  return "text/plain";
}

void serveStaticFile(const String &path) {
  if (!filesystemReady) {
    server.send(503, "text/plain", "Filesystem unavailable");
    return;
  }

  String resolved = path;
  if (resolved.endsWith("/")) resolved += "index.html";
  if (!LittleFS.exists(resolved)) {
    server.send(404, "text/plain", "Not found");
    return;
  }

  File file = LittleFS.open(resolved, "r");
  server.streamFile(file, contentTypeForPath(resolved));
  file.close();
}

void appendSummaryJson(JsonDocument &doc) {
  doc["uptime"] = uptimeString(millis());
  doc["ap_ssid"] = kApSsid;
  doc["ap_ip"] = WiFi.softAPIP().toString();
  doc["ws_url"] = String("ws://") + WiFi.softAPIP().toString() + ":81/";
  doc["sensor_baud"] = kSensorBaud;
  doc["history_points"] = historyCount;
  doc["sensor_reads"] = sensorReads;
  doc["sensor_errors"] = sensorErrors;
  doc["sensor_ready"] = sensorReady;
  doc["work_mode"] = workModeLabel(currentWorkMode);
  doc["desired_mode"] = desiredModeLabel();
  doc["fall_state"] = fallState;
  doc["fall_label"] = fallStateLabel(fallState);
  doc["fall_break_height"] = fallBreakHeight;
  doc["static_residency_state"] = staticResidencyState;

  JsonObject current = doc["current"].to<JsonObject>();
  current["millis"] = latest.millisStamp;
  current["uptime"] = uptimeString(latest.millisStamp);
  if (isfinite(latest.breathRate)) {
    current["breath_bpm"] = latest.breathRate;
  } else {
    current["breath_bpm"] = nullptr;
  }
  if (isfinite(latest.heartRate)) {
    current["heart_bpm"] = latest.heartRate;
  } else {
    current["heart_bpm"] = nullptr;
  }
  if (isfinite(latest.motionScore)) {
    current["motion_score"] = latest.motionScore;
  } else {
    current["motion_score"] = nullptr;
  }
  current["present"] = latest.present;
  current["movement_state"] = latest.movementState;
  current["movement_label"] = movementStateLabel(latest.movementState);
  current["confidence"] = latest.confidence;
  current["fall_state"] = fallState;
  current["fall_label"] = fallStateLabel(fallState);
  current["fall_break_height"] = fallBreakHeight;
  current["static_residency_state"] = staticResidencyState;
}

void handleStatus() {
  JsonDocument doc;
  appendSummaryJson(doc);
  String response;
  serializeJson(doc, response);
  server.send(200, "application/json", response);
}

void handleHistory() {
  JsonDocument doc;
  appendSummaryJson(doc);
  JsonArray samples = doc["samples"].to<JsonArray>();

  const size_t start = (historyCount == kHistoryCapacity) ? historyHead : 0;
  for (size_t i = 0; i < historyCount; ++i) {
    const size_t idx = (start + i) % kHistoryCapacity;
    const VitalSample &sample = history[idx];
    JsonObject row = samples.add<JsonObject>();
    row["millis"] = sample.millisStamp;
    if (isfinite(sample.breathRate)) {
      row["breath_bpm"] = sample.breathRate;
    } else {
      row["breath_bpm"] = nullptr;
    }
    if (isfinite(sample.heartRate)) {
      row["heart_bpm"] = sample.heartRate;
    } else {
      row["heart_bpm"] = nullptr;
    }
    if (isfinite(sample.motionScore)) {
      row["motion_score"] = sample.motionScore;
    } else {
      row["motion_score"] = nullptr;
    }
    row["present"] = sample.present;
    row["movement_state"] = sample.movementState;
    row["movement_label"] = movementStateLabel(sample.movementState);
    row["confidence"] = sample.confidence;
  }

  String response;
  serializeJson(doc, response);
  server.send(200, "application/json", response);
}

void handleLogDownload() {
  if (!filesystemReady) {
    server.send(503, "text/plain", "Filesystem unavailable");
    return;
  }

  ensureLogHeader();
  File file = LittleFS.open("/vitals.csv", "r");
  if (!file) {
    server.send(404, "text/plain", "Log file not found");
    return;
  }

  server.sendHeader("Content-Disposition", "attachment; filename=vitals.csv");
  server.streamFile(file, "text/csv");
  file.close();
}

void handleLogClear() {
  if (!filesystemReady) {
    server.send(503, "application/json", "{\"error\":\"filesystem_unavailable\"}");
    return;
  }

  if (LittleFS.exists("/vitals.csv")) {
    LittleFS.remove("/vitals.csv");
  }
  ensureLogHeader();
  lastLogged = VitalSample{};
  lastLogAt = 0;
  server.send(200, "application/json", "{\"ok\":true}");
}

void handleModeSet() {
  if (!server.hasArg("mode")) {
    server.send(400, "application/json", "{\"error\":\"mode_required\"}");
    return;
  }

  const String mode = server.arg("mode");
  if (mode == "fall") {
    desiredMode = SensorMode::Fall;
  } else if (mode == "sleep") {
    desiredMode = SensorMode::Sleep;
  } else {
    server.send(400, "application/json", "{\"error\":\"invalid_mode\"}");
    return;
  }

  sensorReady = false;
  websocketDirty = true;
  server.send(200, "application/json", "{\"ok\":true}");
}

void handleHistoryDownload() {
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.sendHeader("Content-Disposition", "attachment; filename=history.csv");
  server.send(200, "text/csv", "");
  WiFiClient client = server.client();
  client.println("millis,uptime,breath_bpm,heart_bpm,motion_score,present,movement_state,confidence");

  const size_t start = (historyCount == kHistoryCapacity) ? historyHead : 0;
  for (size_t i = 0; i < historyCount; ++i) {
    const size_t idx = (start + i) % kHistoryCapacity;
    const VitalSample &sample = history[idx];
    client.print(sample.millisStamp);
    client.print(',');
    client.print(uptimeString(sample.millisStamp));
    client.print(',');
    client.print(csvFloat(sample.breathRate));
    client.print(',');
    client.print(csvFloat(sample.heartRate));
    client.print(',');
    client.print(csvFloat(sample.motionScore));
    client.print(',');
    client.print(sample.present ? "1" : "0");
    client.print(',');
    client.print(sample.movementState);
    client.print(',');
    client.println(sample.confidence);
  }
}

void broadcastState() {
  JsonDocument doc;
  doc["type"] = "snapshot";
  appendSummaryJson(doc);
  String payload;
  serializeJson(doc, payload);
  ws.broadcastTXT(payload);
}

void handleWebSocketEvent(uint8_t clientNum, WStype_t type, uint8_t *payload, size_t length) {
  switch (type) {
    case WStype_CONNECTED: {
      JsonDocument doc;
      doc["type"] = "hello";
      doc["client"] = clientNum;
      appendSummaryJson(doc);
      String message;
      serializeJson(doc, message);
      ws.sendTXT(clientNum, message);
      break;
    }
    case WStype_TEXT: {
      String text = String(reinterpret_cast<const char *>(payload), length);
      if (text == "ping") {
        ws.sendTXT(clientNum, "{\"type\":\"pong\"}");
      }
      break;
    }
    default:
      break;
  }
}

bool initializeSensor() {
  Serial.println("Initializing C1001...");
  if (humanRadar.begin() != 0) {
    Serial.println("C1001 init failed");
    sensorErrors++;
    return false;
  }

  if (humanRadar.configWorkMode(desiredModeLibraryValue()) != 0) {
    Serial.println("C1001 mode switch failed");
    sensorErrors++;
    return false;
  }

  humanRadar.configLEDLight(humanRadar.eHPLed, 1);
  humanRadar.sensorRet();
  delay(200);

  currentWorkMode = humanRadar.getWorkMode();
  Serial.print("C1001 mode: ");
  Serial.println(workModeLabel(currentWorkMode));
  return true;
}

void pollSensor() {
  if (!sensorReady) {
    sensorReady = initializeSensor();
    websocketDirty = true;
    return;
  }

  if (desiredMode == SensorMode::Sleep) {
    const int presence = humanRadar.smHumanData(humanRadar.eHumanPresence);
    const int movement = humanRadar.smHumanData(humanRadar.eHumanMovement);
    const int movementRange = humanRadar.smHumanData(humanRadar.eHumanMovingRange);
    const int humanDistance = humanRadar.smHumanData(humanRadar.eHumanDistance);
    const int breath = humanRadar.getBreatheValue();
    const int heart = humanRadar.getHeartRate();
    const uint8_t breatheState = humanRadar.getBreatheState();
    const uint16_t inBedState = humanRadar.smSleepData(humanRadar.eInOrNotInBed);
    const uint16_t sleepState = humanRadar.smSleepData(humanRadar.eSleepState);
    const sSleepComposite composite = humanRadar.getSleepComposite();

    if (presence < 0 || movement < 0 || movementRange < 0 || humanDistance < 0 || breath < 0 || heart < 0) {
      sensorErrors++;
      return;
    }

    latest.present = presence == 1;
    latest.movementState = static_cast<uint8_t>(movement);
    latest.motionScore = static_cast<float>(movementRange);
    latest.breathRate = (breath > 0) ? static_cast<float>(breath) : NAN;
    latest.heartRate = (heart > 0) ? static_cast<float>(heart) : NAN;
    lastHumanDistance = static_cast<uint16_t>(humanDistance);
    lastBreatheState = breatheState;
    lastInBedState = inBedState;
    lastSleepState = sleepState;
    lastAverageRespiration = composite.averageRespiration;
    lastAverageHeartbeat = composite.averageHeartbeat;
    fallState = 0;
    fallBreakHeight = 0;
    staticResidencyState = 0;
  } else {
    latest.present = humanRadar.dmHumanData(humanRadar.eExistence) == 1;
    latest.movementState = static_cast<uint8_t>(humanRadar.dmHumanData(humanRadar.eMotion));
    latest.motionScore = static_cast<float>(humanRadar.dmHumanData(humanRadar.eBodyMove));
    latest.breathRate = NAN;
    latest.heartRate = NAN;
    lastHumanDistance = 0;
    lastBreatheState = 0;
    lastInBedState = 0;
    lastSleepState = 0;
    lastAverageRespiration = 0;
    lastAverageHeartbeat = 0;
    fallState = humanRadar.getFallData(humanRadar.eFallState);
    fallBreakHeight = humanRadar.getFallData(humanRadar.eFallBreakHeight);
    staticResidencyState = humanRadar.getFallData(humanRadar.estaticResidencyState);
  }

  currentWorkMode = humanRadar.getWorkMode();
  sensorReads++;
  markSensorUpdate();
}

void printDebugTelemetry() {
  Serial.print("[telemetry] ");
  Serial.print("mode=");
  Serial.print(workModeLabel(currentWorkMode));
  Serial.print(" desired=");
  Serial.print(desiredModeLabel());
  Serial.print(" ");
  Serial.print("present=");
  Serial.print(latest.present ? "1" : "0");
  Serial.print(" movement=");
  Serial.print(movementStateLabel(latest.movementState));
  Serial.print(" motion=");
  Serial.print(isfinite(latest.motionScore) ? String(latest.motionScore, 0) : String("NA"));
  Serial.print(" breath=");
  Serial.print(isfinite(latest.breathRate) ? String(latest.breathRate, 1) : String("NA"));
  Serial.print(" heart=");
  Serial.print(isfinite(latest.heartRate) ? String(latest.heartRate, 1) : String("NA"));
  Serial.print(" distance=");
  Serial.print(lastHumanDistance);
  Serial.print(" breatheState=");
  Serial.print(breatheStateLabel(lastBreatheState));
  Serial.print(" inBed=");
  Serial.print(lastInBedState);
  Serial.print(" sleepState=");
  Serial.print(sleepStateLabel(lastSleepState));
  Serial.print(" avgResp=");
  Serial.print(lastAverageRespiration);
  Serial.print(" avgHeart=");
  Serial.print(lastAverageHeartbeat);
  Serial.print(" fall=");
  Serial.print(fallStateLabel(fallState));
  Serial.print(" breakHeight=");
  Serial.print(fallBreakHeight);
  Serial.print(" residency=");
  Serial.print(staticResidencyState);
  Serial.print(" confidence=");
  Serial.print(latest.confidence);
  Serial.print(" sensorReady=");
  Serial.print(sensorReady ? "1" : "0");
  Serial.print(" errors=");
  Serial.println(sensorErrors);
}

void setupRoutes() {
  server.on("/", HTTP_GET, []() { serveStaticFile("/index.html"); });
  server.on("/styles.css", HTTP_GET, []() { serveStaticFile("/styles.css"); });
  server.on("/app.js", HTTP_GET, []() { serveStaticFile("/app.js"); });
  server.on("/api/status", HTTP_GET, handleStatus);
  server.on("/api/history", HTTP_GET, handleHistory);
  server.on("/api/mode", HTTP_POST, handleModeSet);
  server.on("/download/log.csv", HTTP_GET, handleLogDownload);
  server.on("/download/history.csv", HTTP_GET, handleHistoryDownload);
  server.on("/api/log/clear", HTTP_POST, handleLogClear);
  server.onNotFound([]() {
    if (server.uri().startsWith("/api/")) {
      server.send(404, "application/json", "{\"error\":\"not_found\"}");
      return;
    }
    serveStaticFile(server.uri());
  });
}

void setupAccessPoint() {
  WiFi.mode(WIFI_AP);
  WiFi.softAP(kApSsid, kApPassword);
}

void setupFilesystem() {
  filesystemReady = LittleFS.begin(false);
  if (!filesystemReady) {
    Serial.println("LittleFS mount failed. Web assets and CSV logging are unavailable until the filesystem is uploaded again.");
    return;
  }
  Serial.println("LittleFS mounted successfully.");
  ensureLogHeader();
}

void setupRadar() {
  radarSerial.begin(kSensorBaud, SERIAL_8N1, kSensorRxPin, kSensorTxPin);
  Serial.println("Waiting for C1001 boot...");
  delay(20000);
  sensorReady = initializeSensor();
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(200);

  setupFilesystem();
  setupAccessPoint();
  setupRadar();
  setupRoutes();

  ws.begin();
  ws.onEvent(handleWebSocketEvent);
  server.begin();

  latest.millisStamp = millis();
  Serial.println();
  Serial.printf("AP SSID: %s\n", kApSsid);
  Serial.printf("AP IP: %s\n", WiFi.softAPIP().toString().c_str());
  Serial.printf("Web UI: http://%s/\n", WiFi.softAPIP().toString().c_str());
}

void loop() {
  server.handleClient();
  ws.loop();

  const uint32_t now = millis();
  if (now - lastSensorPollAt >= kSensorPollIntervalMs) {
    pollSensor();
    lastSensorPollAt = now;
  }

  if (now - lastDebugPrintAt >= 1000) {
    printDebugTelemetry();
    lastDebugPrintAt = now;
  }

  if (hasNewSensorData &&
      latest.millisStamp != lastHistorySourceMillis &&
      now - lastHistoryAt >= kHistorySampleIntervalMs) {
    pushHistorySample();
    lastHistoryAt = now;
    lastHistorySourceMillis = latest.millisStamp;
  }

  if (hasNewSensorData && now - lastLogAt >= kLogSampleIntervalMs && shouldLogSample()) {
    appendLogSample();
    lastLogAt = now;
  }

  if ((websocketDirty || now - lastBroadcastAt >= kBroadcastIntervalMs) && ws.connectedClients() > 0) {
    broadcastState();
    lastBroadcastAt = now;
    websocketDirty = false;
  }
}
