// board adafruit feather ESP32-S3 No PSRAM


#include <BLEDevice.h>
#include <BLEServer.h>
#include <Wire.h>

#define SERVICE_UUID "00001ff8-0000-1000-8000-00805f9b34fb"
#define CHAR_UUID_CONFIG "00000005-0000-1000-8000-00805f9b34fb"
#define CHAR_UUID_VALUES "00000006-0000-1000-8000-00805f9b34fb"

#define CMD_TYPE_REMOVE_ALL 0
#define CMD_TYPE_REMOVE 1
#define CMD_TYPE_ADD_INCOMPLETE 2
#define CMD_TYPE_ADD 3
#define CMD_TYPE_UPDATE_ALL 4
#define CMD_TYPE_UPDATE 5

#define CMD_RESULT_OK 0
#define CMD_RESULT_PAYLOAD_OUT_OF_SEQUENCE 1
#define CMD_RESULT_EQUATION_EXCEPTION 2

#define MAX_REMAINING_PAYLOAD 2048
#define MAX_PAYLOAD_PART 17
#define MONITOR_NAME_MAX 32
#define MONITORS_MAX 255

#define I2C_SLAVE_ADDRESS 0x08 // Adresse I2C du Feather M4 CAN (esclave)

static const int32_t INVALID_VALUE = 0x7fffffff;

// Structure pour regrouper les données à envoyer via I2C
struct RaceData {
  uint8_t day;
  uint8_t month;
  uint8_t year;
  uint8_t hours;
  uint8_t minutes;
  float speed;
  int32_t currLap;
  int32_t currTime;
  int32_t prevLap;
  int32_t prevTime;
  int32_t bestLap;
  int32_t bestTime;
  bool newLapFlag;
};

// Variables globales
BLEServer* pServer = NULL;
BLECharacteristic* pMonitorConfigChar = NULL;
BLECharacteristic* pMonitorValuesChar = NULL;

char monitorNames[MONITORS_MAX][MONITOR_NAME_MAX + 1];
float monitorMultipliers[MONITORS_MAX];
int32_t monitorValues[MONITORS_MAX];
int nextMonitorId = 0;

boolean deviceConnected = false;
boolean monitorConfigStarted = false;
boolean initialTimeDisplayed = false;

unsigned long lastConfigTime = 0;
const unsigned long configInterval = 5000;

int32_t lastTimestamp = -1;
int lastDisplayedMinute = -1;
int32_t lastCurrLap = -1;
int32_t lastCurrTime = 0;
int32_t bestTime = INVALID_VALUE;

// Variables pour l'affichage en continu
unsigned long lastDisplayTime = 0;
const unsigned long displayInterval = 100; // Afficher toutes les 100 ms
float lastSpeed = 0.0;
int32_t lastCurrLapValue = -1;
int32_t lastCurrTimeValue = 0;
int lastDay = -1, lastMonth = -1, lastYear = -1;
int lastHours = -1, lastMinutes = -1;

// Variables pour l'envoi I2C
unsigned long lastSendTime = 0;
const unsigned long sendInterval = 100; // Envoyer toutes les 100 ms
bool newLapFlag = false;

class MyServerCallbacks: public BLEServerCallbacks {
  void onConnect(BLEServer* pServer) {
    deviceConnected = true;
    Serial.println("Client connecté (RaceChrono ?)");
    monitorConfigStarted = false;
    initialTimeDisplayed = false;
    lastDisplayedMinute = -1;
    lastCurrLap = -1;
    lastCurrTime = 0;
    bestTime = INVALID_VALUE;
    lastSpeed = 0.0;
    lastCurrLapValue = -1;
    lastCurrTimeValue = 0;
    lastDay = -1;
    lastMonth = -1;
    lastYear = -1;
    lastHours = -1;
    lastMinutes = -1;
    newLapFlag = false;
  }
  void onDisconnect(BLEServer* pServer) {
    deviceConnected = false;
    monitorConfigStarted = false;
    initialTimeDisplayed = false;
    lastDisplayedMinute = -1;
    lastCurrLap = -1;
    lastCurrTime = 0;
    bestTime = INVALID_VALUE;
    lastSpeed = 0.0;
    lastCurrLapValue = -1;
    lastCurrTimeValue = 0;
    lastDay = -1;
    lastMonth = -1;
    lastYear = -1;
    lastHours = -1;
    lastMinutes = -1;
    newLapFlag = false;
    Serial.println("Client déconnecté");
    BLEDevice::startAdvertising();
    Serial.println("Publicité BLE redémarrée");
  }
};

void timestampToDate(int32_t timestamp, int &day, int &month, int &year) {
  timestamp += 2 * 3600;
  const int secondsPerDay = 86400;
  int daysSinceEpoch = timestamp / secondsPerDay;

  year = 1970;
  int daysInYear;
  while (daysSinceEpoch >= (daysInYear = (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0)) ? 366 : 365)) {
    daysSinceEpoch -= daysInYear;
    year++;
  }

  int daysInMonth[] = {31, (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0)) ? 29 : 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  month = 1;
  while (daysSinceEpoch >= daysInMonth[month - 1]) {
    daysSinceEpoch -= daysInMonth[month - 1];
    month++;
  }

  day = daysSinceEpoch + 1;
  year = year % 100;
}

void displayTime(int32_t timeInMs) {
  float adjustedValue = (float)timeInMs * 0.001;
  int minutes = (int)adjustedValue / 60;
  float secondsRemaining = adjustedValue - (minutes * 60);
  int seconds = (int)secondsRemaining;
  int milliseconds = (int)((secondsRemaining - seconds) * 1000.0 + 0.5);
  if (minutes > 0) {
    Serial.print(minutes);
    Serial.print("'");
    Serial.print(seconds);
    Serial.print("\"");
    if (milliseconds < 100) Serial.print("0");
    if (milliseconds < 10) Serial.print("0");
    Serial.print(milliseconds);
  } else {
    Serial.print(seconds);
    Serial.print("\"");
    if (milliseconds < 100) Serial.print("0");
    if (milliseconds < 10) Serial.print("0");
    Serial.print(milliseconds);
  }
}

void displayContinuousData() {
  unsigned long currentTime = millis();
  if (currentTime - lastDisplayTime < displayInterval) {
    return;
  }
  lastDisplayTime = currentTime;

  // Afficher Date
  if (lastDay != -1) {
    if (lastDay < 10) Serial.print("0");
    Serial.print(lastDay);
    Serial.print("/");
    if (lastMonth < 10) Serial.print("0");
    Serial.print(lastMonth);
    Serial.print("/");
    if (lastYear < 10) Serial.print("0");
    Serial.print(lastYear);
  } else {
    Serial.print("N/A");
  }
  Serial.print(" || ");

  // Afficher Heure
  if (lastHours != -1) {
    if (lastHours < 10) Serial.print("0");
    Serial.print(lastHours);
    Serial.print(":");
    if (lastMinutes < 10) Serial.print("0");
    Serial.print(lastMinutes);
  } else {
    Serial.print("N/A");
  }
  Serial.print(" || ");

  // Afficher Speed
  Serial.print("Speed = ");
  if (lastSpeed < 1.0) {
    Serial.print("0.00");
  } else {
    Serial.print(lastSpeed);
  }
  Serial.print(" km/h || ");

  // Afficher Curr lap
  Serial.print("Curr lap ");
  if (lastCurrLapValue != -1) {
    Serial.print(lastCurrLapValue);
  } else {
    Serial.print("N/A");
  }
  Serial.print(" || ");

  // Afficher Curr time
  Serial.print("Curr time = ");
  if (lastCurrTimeValue != 0) {
    displayTime(lastCurrTimeValue);
  } else {
    Serial.print("N/A");
  }

  Serial.println();
}

void sendDataViaI2C() {
  unsigned long currentTime = millis();
  if (currentTime - lastSendTime < sendInterval) {
    return;
  }
  lastSendTime = currentTime;

  RaceData data;
  data.day = (uint8_t)lastDay;
  data.month = (uint8_t)lastMonth;
  data.year = (uint8_t)lastYear;
  data.hours = (uint8_t)lastHours;
  data.minutes = (uint8_t)lastMinutes;
  data.speed = lastSpeed;
  data.currLap = lastCurrLapValue;
  data.currTime = lastCurrTimeValue;
  data.prevLap = lastCurrLap;
  data.prevTime = lastCurrTime;
  data.bestLap = monitorValues[5] != INVALID_VALUE ? (int32_t)(monitorValues[5] * monitorMultipliers[5]) : -1;
  data.bestTime = bestTime;
  data.newLapFlag = newLapFlag;

  Wire.beginTransmission(I2C_SLAVE_ADDRESS);
  Wire.write((uint8_t*)&data, sizeof(RaceData));
  Wire.endTransmission();

  newLapFlag = false; // Réinitialiser le drapeau après l'envoi
}

class MonitorConfigCallbacks: public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic* pCharacteristic) {
    String value = pCharacteristic->getValue();
    if (value.length() >= 1) {
      int result = value[0];
      int monitorId = value[1];
      Serial.print("Réponse de RaceChrono pour Monitor ");
      Serial.print(monitorId);
      Serial.print(" : ");
      switch (result) {
        case CMD_RESULT_OK:
          Serial.println("Succès");
          break;
        case CMD_RESULT_PAYLOAD_OUT_OF_SEQUENCE:
          Serial.println("Erreur - Payload hors séquence");
          break;
        case CMD_RESULT_EQUATION_EXCEPTION:
          Serial.println("Erreur - Exception dans l'équation");
          break;
        default:
          Serial.println("Résultat inconnu");
          break;
      }
    }
  }
};

class MonitorValuesCallbacks: public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic* pCharacteristic) {
    String value = pCharacteristic->getValue();
    int dataPos = 0;
    while (dataPos + 5 <= value.length()) {
      int monitorId = (int)value[dataPos];
      int32_t dataValue = ((uint8_t)value[dataPos + 1] << 24) |
                          ((uint8_t)value[dataPos + 2] << 16) |
                          ((uint8_t)value[dataPos + 3] << 8) |
                          (uint8_t)value[dataPos + 4];
      if (monitorId < nextMonitorId) {
        monitorValues[monitorId] = dataValue;

        if (dataValue != INVALID_VALUE) {
          float adjustedValue = (float)dataValue * monitorMultipliers[monitorId];
          
          if (monitorId == 0) { // Speed
            lastSpeed = adjustedValue;
            displayContinuousData();
            sendDataViaI2C();
          }
          else if (monitorId == 1) { // Curr lap
            lastCurrLapValue = (int)adjustedValue;
            if (lastCurrLap != -1 && lastCurrLap != dataValue) {
              newLapFlag = true;
              // Afficher Prev lap et Prev time
              Serial.print("Prev lap = ");
              if (lastCurrLap != -1) {
                Serial.print(lastCurrLap);
              } else {
                Serial.print("N/A");
              }
              Serial.print(" || Prev time = ");
              if (lastCurrTime != 0) {
                displayTime(lastCurrTime);
              } else {
                Serial.print("N/A");
              }
              Serial.println();

              // Mettre à jour Best time
              if (lastCurrTime != 0) {
                if (bestTime == INVALID_VALUE || lastCurrTime < bestTime) {
                  bestTime = lastCurrTime;
                }
              }

              // Afficher Best lap et Best time
              Serial.print("Best lap = ");
              if (monitorValues[5] != INVALID_VALUE) {
                Serial.print((int)(monitorValues[5] * monitorMultipliers[5]));
              } else {
                Serial.print("N/A");
              }
              Serial.print(" || Best time = ");
              if (bestTime != INVALID_VALUE) {
                displayTime(bestTime);
              } else {
                Serial.print("N/A");
              }
              Serial.println();
              sendDataViaI2C();
            }
            lastCurrLap = dataValue;
          }
          else if (monitorId == 2) { // Curr time
            lastCurrTime = dataValue;
            lastCurrTimeValue = dataValue;
            displayContinuousData();
            sendDataViaI2C();
          }
          else if (monitorId == 7) { // Timestamp
            lastTimestamp = (int32_t)dataValue;
            lastTimestamp += 2 * 3600;
            lastMinutes = (lastTimestamp / 60) % 60;
            lastHours = (lastTimestamp / 3600) % 24;
            timestampToDate(lastTimestamp, lastDay, lastMonth, lastYear);
            displayContinuousData();
            sendDataViaI2C();
          }
        }
      }
      dataPos += 5;
    }
  }
};

boolean sendConfigCommand(int cmdType, int monitorId, const char* payload, int payloadSequence = 0) {
  cmdType = cmdType == CMD_TYPE_ADD_INCOMPLETE ? CMD_TYPE_ADD : cmdType;

  char payloadPart[MAX_PAYLOAD_PART + 1];
  char* remainingPayload = NULL;
  if (payload && cmdType == CMD_TYPE_ADD) {
    strncpy(payloadPart, payload, MAX_PAYLOAD_PART);
    payloadPart[MAX_PAYLOAD_PART] = '\0';

    int payloadLen = strlen(payload);
    if (payloadLen > MAX_PAYLOAD_PART) {
      int remainingPayloadLen = payloadLen - MAX_PAYLOAD_PART;
      remainingPayload = (char*)malloc(remainingPayloadLen + 1);
      strncpy(remainingPayload, payload + MAX_PAYLOAD_PART, remainingPayloadLen);
      remainingPayload[remainingPayloadLen] = '\0';
      cmdType = CMD_TYPE_ADD_INCOMPLETE;
    }
  } else {
    payloadPart[0] = '\0';
  }

  byte bytes[20];
  bytes[0] = (byte)cmdType;
  bytes[1] = (byte)monitorId;
  bytes[2] = (byte)payloadSequence;
  memcpy(bytes + 3, payloadPart, strlen(payloadPart));

  pMonitorConfigChar->setValue(bytes, 3 + strlen(payloadPart));
  pMonitorConfigChar->indicate();

  if (remainingPayload) {
    boolean result = sendConfigCommand(CMD_TYPE_ADD, monitorId, remainingPayload, payloadSequence + 1);
    free(remainingPayload);
    return result;
  }
  return true;
}

boolean addMonitor(const char* monitorName, const char* filterDef, float multiplier) {
  if (nextMonitorId < MONITORS_MAX) {
    if (!sendConfigCommand(CMD_TYPE_ADD, nextMonitorId, filterDef)) {
      Serial.print("Échec de l'ajout du moniteur : ");
      Serial.println(monitorName);
      return false;
    }
    strncpy(monitorNames[nextMonitorId], monitorName, MONITOR_NAME_MAX);
    monitorNames[nextMonitorId][MONITOR_NAME_MAX] = '\0';
    monitorMultipliers[nextMonitorId] = multiplier;
    nextMonitorId++;
    Serial.print("Moniteur ajouté : ");
    Serial.print(monitorName);
    Serial.print(" (ID ");
    Serial.print(nextMonitorId - 1);
    Serial.println(")");
  }
  return true;
}

boolean configureMonitors() {
  nextMonitorId = 0;
  if (!addMonitor("Speed", "channel(device(gps), speed)*100.0", 0.036) ||
      !addMonitor("Curr lap", "channel(device(lap), lap_number)", 1.0) ||
      !addMonitor("Curr time", "channel(device(lap), lap_time)*1000.0", 0.001) ||
      !addMonitor("Prev lap", "channel(device(lap), previous_lap_number)", 1.0) ||
      !addMonitor("Prev time", "channel(device(lap), previous_lap_time)*1000.0", 0.001) ||
      !addMonitor("Best lap", "channel(device(lap), best_lap_number)", 1.0) ||
      !addMonitor("Best time", "channel(device(lap), best_lap_time)*1000.0", 0.001) ||
      !addMonitor("Timestamp", "channel(device(gps), timestamp)", 1.0)) {
    return false;
  }
  return true;
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("Serveur BLE démarré");

  // Initialiser I2C comme maître
  Wire.begin(); // SDA et SCL par défaut (GPIO 21 et 22 sur ESP32-S3)

  BLEDevice::init("ESP32-S3 DIY");
  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());
  BLEService *pService = pServer->createService(SERVICE_UUID);

  pMonitorConfigChar = pService->createCharacteristic(
    CHAR_UUID_CONFIG,
    BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_INDICATE
  );
  pMonitorConfigChar->setCallbacks(new MonitorConfigCallbacks());
  BLEDescriptor *pDescriptor = new BLEDescriptor(BLEUUID((uint16_t)0x2902));
  uint8_t descValue[] = {0x02, 0x00};
  pDescriptor->setValue(descValue, 2);
  pMonitorConfigChar->addDescriptor(pDescriptor);

  pMonitorValuesChar = pService->createCharacteristic(
    CHAR_UUID_VALUES,
    BLECharacteristic::PROPERTY_WRITE_NR | BLECharacteristic::PROPERTY_NOTIFY
  );
  pMonitorValuesChar->setCallbacks(new MonitorValuesCallbacks());

  pService->start();
  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->setScanResponse(true);
  pAdvertising->setMinPreferred(0x06);
  pAdvertising->setMinPreferred(0x12);
  pAdvertising->start();
  Serial.println("Publicité BLE démarrée");
}

void loop() {
  unsigned long currentTime = millis();

  if (deviceConnected && !monitorConfigStarted && (currentTime - lastConfigTime >= configInterval)) {
    for (int i = 0; i < MONITORS_MAX; i++) {
      monitorValues[i] = INVALID_VALUE;
    }
    monitorConfigStarted = true;
    Serial.println("Configuration des moniteurs...");
    if (configureMonitors()) {
      Serial.println("Configuration terminée");
    } else {
      Serial.println("Échec de la configuration");
    }
    lastConfigTime = currentTime;
  }
}