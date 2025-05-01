#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include "PacketIdInfo.h"

// Définitions pour le débogage
#define debugln Serial.println
#define debug Serial.print

// UUIDs corrects fournis
#define SERVICE_UUID "00001ff8-0000-1000-8000-00805f9b34fb"
#define CANBUS_MAIN_UUID "0001"  // UUID 16 bits pour la caractéristique principale
#define CANBUS_FILTER_UUID "0002"  // UUID 16 bits pour la caractéristique de filtre

// Déclaration globale de pServer et pAdvertising
BLEServer* pServer;
BLEAdvertising* pAdvertising;

// Caractéristiques BLE
BLECharacteristic* canBusMainCharacteristic;
BLECharacteristic* canBusFilterCharacteristic;

// Gestion des filtres CAN
PacketIdInfo canBusPacketIdInfo;
bool canBusAllowUnknownPackets = false;
uint32_t canBusLastNotifyMs = 0;
boolean isCanBusConnected = false;

// Données simulées
uint8_t tempData[20];
float rpm = 0; // Utiliser un float pour des incréments précis
float throttle = 0; // Utiliser un float pour des incréments précis
unsigned long lastSendTime = 0;
unsigned long lastNotifyTime = 0;
const long sendInterval = 20; // 20 ms = 50 Hz
unsigned long lastFrequencyCheck = 0;
unsigned int notifyCount = 0;
bool accelerating = true; // Simuler une accélération/décélération

// Callback pour la caractéristique de filtre
class FilterCallback : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic* pCharacteristic) {
    String value = pCharacteristic->getValue();
    if (value.length() < 1) return;
    Serial.print("Commande de filtre reçue : ");
    for (size_t i = 0; i < value.length(); i++) {
      Serial.printf("%02X ", (uint8_t)value[i]);
    }
    Serial.println();
    uint8_t command = value[0];
    switch (command) {
      case 0x00: // DENY_ALL
        if (value.length() == 1) {
          canBusPacketIdInfo.reset();
          canBusAllowUnknownPackets = false;
          debugln("CAN-Bus command DENY");
        }
        break;
      case 0x01: // ALLOW_ALL
        if (value.length() == 3) {
          canBusPacketIdInfo.reset();
          canBusPacketIdInfo.setDefaultNotifyInterval(sendInterval); // Forcer à 20 ms
          canBusAllowUnknownPackets = true;
          debug("CAN-Bus command ALLOW interval forcé à ");
          debugln(sendInterval);
        }
        break;
      case 0x02: // ADD_PID
        if (value.length() == 7) {
          //uint16_t notifyIntervalMs = value[1] << 8 | value[2];
          uint32_t pid = value[3] << 24 | value[4] << 16 | value[5] << 8 | value[6];
          canBusPacketIdInfo.setNotifyInterval(pid, sendInterval); // Forcer à 20 ms
          debug("CAN-Bus command ADD PID ");
          debug(pid);
          debug(" interval forcé à ");
          debugln(sendInterval);
        }
        break;
    }
  }
};

// Fonction pour démarrer la publicité BLE
void startAdvertising() {
  pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->setScanResponse(true);
  pAdvertising->setMinInterval(0x20); // 20 ms pour une connexion rapide
  pAdvertising->setMaxInterval(0x40); // 40 ms
  pAdvertising->start();
  Serial.println("Publicité BLE démarrée");
}

void setup() {
  Serial.begin(115200);
  while (!Serial);
  Serial.println("Démarrage du dispositif CAN simulé pour RaceChrono");

  // Initialisation BLE
  BLEDevice::init("RC DIY SIM");
  pServer = BLEDevice::createServer();
  BLEService* pService = pServer->createService(SERVICE_UUID);

  // Caractéristique principale CAN bus (UUID 16 bits)
  canBusMainCharacteristic = pService->createCharacteristic(
    CANBUS_MAIN_UUID,
    BLECharacteristic::PROPERTY_NOTIFY | BLECharacteristic::PROPERTY_READ
  );
  canBusMainCharacteristic->addDescriptor(new BLE2902());

  // Caractéristique de filtre (UUID 16 bits)
  canBusFilterCharacteristic = pService->createCharacteristic(
    CANBUS_FILTER_UUID,
    BLECharacteristic::PROPERTY_WRITE
  );
  canBusFilterCharacteristic->setCallbacks(new FilterCallback());

  pService->start();

  // Démarrer la publicité
  startAdvertising();
}

void simulateAndSendCanData() {
  unsigned long currentTime = millis();
  if (currentTime - lastNotifyTime < sendInterval) return; // Respecter l'intervalle de notification
  lastNotifyTime = currentTime;

  // Simuler une accélération/décélération progressive
  if (accelerating) {
    // Augmenter RPM (2000 RPM par seconde)
    rpm += 40; // 2000 RPM/s ÷ 50 cycles/s = 40 RPM par cycle
    // Augmenter throttle (50 % par seconde)
    throttle += 1; // 50 %/s ÷ 50 cycles/s = 1 % par cycle

    if (rpm >= 8000) {
      rpm = 8000;
      throttle = 100; // Limite stricte à 100 %
      accelerating = false; // Passer en décélération
    }
  } else {
    // Diminuer RPM (1000 RPM par seconde)
    rpm -= 20; // 1000 RPM/s ÷ 50 cycles/s = 20 RPM par cycle
    // Diminuer throttle (50 % par seconde)
    throttle -= 1; // 50 %/s ÷ 50 cycles/s = 1 % par cycle

    if (rpm <= 1000) {
      rpm = 1000; // Simuler un ralenti
      throttle = 0; // Throttle minimum
      accelerating = true; // Repasser en accélération
    }
  }

  // S'assurer que throttle reste entre 0 et 100
  if (throttle > 100) throttle = 100;
  if (throttle < 0) throttle = 0;

  // Simuler RPM (CAN ID 0x100)
  uint16_t rpmInt = (uint16_t)rpm; // Convertir en entier pour l'envoi
  uint8_t rpm_high = (rpmInt >> 8) & 0xFF;
  uint8_t rpm_low = rpmInt & 0xFF;
  uint32_t packetId = 0x100;
  ((uint32_t*)tempData)[0] = packetId; // ID 0x100 little-endian
  tempData[4] = rpm_high;
  tempData[5] = rpm_low;
  PacketIdInfoItem* infoItem = canBusPacketIdInfo.findItem(packetId, canBusAllowUnknownPackets);
  if (infoItem && infoItem->shouldNotify()) {
    canBusMainCharacteristic->setValue(tempData, 6);
    canBusMainCharacteristic->notify();
    infoItem->markNotified();
    notifyCount++; // Incrémenter le compteur de notifications
  }

  // Simuler throttle (CAN ID 0x101)
  uint8_t throttleInt = (uint8_t)throttle; // Convertir en entier pour l'envoi
  packetId = 0x101;
  ((uint32_t*)tempData)[0] = packetId; // ID 0x101 little-endian
  tempData[4] = throttleInt;
  infoItem = canBusPacketIdInfo.findItem(packetId, canBusAllowUnknownPackets);
  if (infoItem && infoItem->shouldNotify()) {
    canBusMainCharacteristic->setValue(tempData, 5);
    canBusMainCharacteristic->notify();
    infoItem->markNotified();
    notifyCount++; // Incrémenter le compteur de notifications

    // Log temporaire pour vérifier la valeur de throttle
    Serial.printf("Throttle envoyé : %d%%\n", throttleInt);
  }

  // Calculer et afficher la fréquence toutes les secondes
  if (currentTime - lastFrequencyCheck >= 1000) {
    float frequency = (float)notifyCount * 1000.0 / (currentTime - lastFrequencyCheck);
    Serial.printf("Fréquence d'envoi : %.1f Hz\n", frequency);
    notifyCount = 0;
    lastFrequencyCheck = currentTime;
  }
}

void loop() {
  unsigned long currentTime = millis();
  if (currentTime - lastSendTime >= sendInterval) {
    lastSendTime = currentTime;
    if (isCanBusConnected) {
      simulateAndSendCanData();
    }
  }

  // Simuler la connexion BLE
  if (!isCanBusConnected && pServer->getConnectedCount() > 0) {
    isCanBusConnected = true;
    Serial.println("BLE connecté");
    canBusPacketIdInfo.reset();
  } else if (isCanBusConnected && pServer->getConnectedCount() == 0) {
    isCanBusConnected = false;
    Serial.println("BLE déconnecté");
    // Redémarrer la publicité après déconnexion
    pAdvertising->stop();
    delay(100); // Petit délai pour s'assurer que la pile BLE est prête
    startAdvertising();
  }
}