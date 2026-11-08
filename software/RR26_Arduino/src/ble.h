/**
 * @file  ble.h
 * @brief BLE driver
 *
 */

#pragma once
#include <Arduino.h>
#include <NimBLEDevice.h>
#include <esp_mac.h>
#include <mutex>

#include "../CONFIGURE-ME.h"
#include "pinouts.h"
#include "../RedRobot.h"

extern void ParseBluetoothCommands(char *buf, size_t len);

namespace RobotControl {
void ApplyXboxInput(int turn, int drive, int ramp, int claw);
}

// Set to 1 for one boot to wipe stale bonds (e.g. after changing security
// settings or if a phone refuses to reconnect), then set back to 0.
#define BLE_CLEAR_BONDS_ON_BOOT 0

// Nordic UART Service (NUS) UUIDs
#define SERVICE_UUID "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
#define CHARACTERISTIC_UUID_RX "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"
#define CHARACTERISTIC_UUID_TX "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"

namespace {
constexpr uint16_t NO_CONTROLLER = 0xffff;
constexpr uint32_t XBOX_TIMEOUT_MS = 300;
std::mutex controllerMutex;
uint16_t controllerHandle = NO_CONTROLLER;
bool xboxSession = false;
bool xboxArmed = false;
bool xboxAutonomous = false;
uint32_t lastXboxPacket = 0;
char gDeviceName[22]; // 6 header + up to 15 team name + \0 

void CheckXboxTimeout(uint32_t now) {
  if (xboxArmed && now - lastXboxPacket >= XBOX_TIMEOUT_MS) {
    xboxArmed = false;
    xboxAutonomous = false;
    RedRobot::StartRemoteControl();
    RobotControl::ApplyXboxInput(0, 0, 0, 0);
  }
}

void HandleXboxPacket(const uint8_t *data, size_t len) {
  // !X, turn+100, drive+100, ramp+1, claw+1, optional autonomous flag.
  // Six-byte packets retain the original manual-control behavior.
  if ((len != 6 && len != 7) || data[2] > 200 || data[3] > 200 ||
      data[4] > 2 || data[5] > 2) return;
  if (len == 7 && data[6] > 1) return;
  const bool autonomous = len == 7 && data[6] == 1;

  if (!xboxSession) {
    xboxSession = true;
    RobotControl::ResetOnDisconnect();
    RedRobot::Brake();
  }
  const uint32_t now = millis();
  CheckXboxTimeout(now);
  const int turn = data[2] - 100;
  const int drive = data[3] - 100;
  const int ramp = data[4] - 1;
  const int claw = data[5] - 1;
  // After a timeout, autonomous heartbeats cannot restart the robot.
  // A neutral manual packet (View button) must rearm it first.
  if (!xboxArmed && (turn || drive || ramp || claw || autonomous)) return;
  if (autonomous && (turn || drive || ramp || claw)) return;
  xboxArmed = true;
  lastXboxPacket = now;
  if (autonomous != xboxAutonomous) {
    RedRobot::StartRemoteControl();
    RedRobot::Brake();
    RobotControl::ResetOnDisconnect();
    xboxAutonomous = autonomous;
    if (autonomous) RedRobot::InitAuton();
    Serial.printf("[Xbox] Mode: %s\n", autonomous ? "autonomous" : "manual");
  }
  if (!autonomous) RobotControl::ApplyXboxInput(turn, drive, ramp, claw);
}

class ServerCallbacks : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer *pServer, NimBLEConnInfo &connInfo) override {
    std::lock_guard<std::mutex> lock(controllerMutex);
    if (controllerHandle != NO_CONTROLLER) {
      pServer->disconnect(connInfo);
      return;
    }
    controllerHandle = connInfo.getConnHandle();
    RedRobot::StartRemoteControl();
    RedRobot::Brake();
    Serial.println("[BLE] Client connected");
  }

  void onDisconnect(NimBLEServer *pServer, NimBLEConnInfo &connInfo,
                    int reason) override {
    std::lock_guard<std::mutex> lock(controllerMutex);
    if (connInfo.getConnHandle() != controllerHandle) return;
    controllerHandle = NO_CONTROLLER;
    if (xboxSession) RedRobot::StartRemoteControl();
    RobotControl::ResetOnDisconnect();
    RedRobot::Brake();
    if (!xboxSession) RedRobot::InitAuton();
    xboxSession = false;
    xboxArmed = false;
    xboxAutonomous = false;
    Serial.printf("[BLE] Client disconnected (reason 0x%X)\n", reason);
  }
};

class RxCallbacks : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic *pCharacteristic,
               NimBLEConnInfo &connInfo) override {
    std::lock_guard<std::mutex> lock(controllerMutex);
    if (connInfo.getConnHandle() != controllerHandle) return;
    NimBLEAttValue rxValue = pCharacteristic->getValue();
    size_t len = rxValue.length();
    if (len == 0)
      return;
    const uint8_t *data = rxValue.data();
    if (len >= 2 && data[0] == '!' && data[1] == 'X') {
      HandleXboxPacket(data, len);
      return;
    }
    if (xboxSession) return;

    // Copy into a NUL-terminated buffer so sscanf can't run past the data
    char buf[32];
    if (len >= sizeof(buf))
      len = sizeof(buf) - 1;
    memcpy(buf, rxValue.data(), len);
    buf[len] = '\0';

    ParseBluetoothCommands(buf, len);
  }
};

bool startAdv() {
  NimBLEAdvertising *adv = NimBLEDevice::getAdvertising();
  if (adv->isAdvertising())
    return true;
  bool ok = adv->start();
  Serial.printf("[BLE] adv->start() -> %s, isAdvertising=%d\n",
                ok ? "ok" : "FAILED", (int)adv->isAdvertising());
  return ok;
}

// Backstop: if advertising ever stops while no client is connected, restart it
void advWatchdogTask(void *) {
  uint32_t lastAdvCheck = millis();
  for (;;) {
    vTaskDelay(pdMS_TO_TICKS(25));
    const uint32_t now = millis();
    {
      std::lock_guard<std::mutex> lock(controllerMutex);
      CheckXboxTimeout(millis());
    }
    if (now - lastAdvCheck < 2000) continue;
    lastAdvCheck = now;
    NimBLEServer *srv = NimBLEDevice::getServer();
    bool connected = srv && srv->getConnectedCount() > 0;
    bool advertising = NimBLEDevice::getAdvertising()->isAdvertising();

    digitalWrite(RADIO_LED, (advertising || connected) ? HIGH : LOW);

    if (!connected && !advertising) {
      Serial.println("[BLE] Advertising stopped unexpectedly, restarting");
      startAdv();
    }
  }
}
} // namespace

namespace BLE {
void init() {
  pinMode(RADIO_LED, OUTPUT);
  digitalWrite(RADIO_LED, LOW);

  while (TEAM_NUMBER == 0) Serial.println("[BLE] Error: set TEAM_NUMBER!");
  while (TEAM_NAME[0] == '\0') Serial.println("[BLE] Error: set TEAM_NAME!");

  snprintf(gDeviceName, sizeof(gDeviceName), "RR-%02u-%s", TEAM_NUMBER, TEAM_NAME);

  NimBLEDevice::init(gDeviceName);
  NimBLEDevice::setPower(9); // dBm, max range

#if BLE_CLEAR_BONDS_ON_BOOT
  NimBLEDevice::deleteAllBonds();
#endif

  // Static passkey, bonding + MITM + secure connections
  /*NimBLEDevice::setSecurityAuth(true, true, true);
  NimBLEDevice::setSecurityIOCap(BLE_HS_IO_DISPLAY_ONLY);
  NimBLEDevice::setSecurityPasskey(BLE_STATIC_PIN);*/

  NimBLEDevice::setSecurityAuth(false, false, false);
  NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);

  // GATT server
  NimBLEServer *pServer = NimBLEDevice::createServer();
  pServer->setCallbacks(new ServerCallbacks());
  pServer->advertiseOnDisconnect(true);

  NimBLEService *pService = pServer->createService(SERVICE_UUID);

  // TX (ESP32 -> app). CCCD is added automatically for NOTIFY.
  NimBLECharacteristic *pTx = pService->createCharacteristic(
      CHARACTERISTIC_UUID_TX, NIMBLE_PROPERTY::NOTIFY | NIMBLE_PROPERTY::READ);
  pTx->setValue("RR26-XBOX-3");

  // RX (app -> ESP32)
  NimBLECharacteristic *pRx = pService->createCharacteristic(
      CHARACTERISTIC_UUID_RX,
      NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR/* |
          NIMBLE_PROPERTY::WRITE_ENC | NIMBLE_PROPERTY::WRITE_AUTHEN*/);
  pRx->setCallbacks(new RxCallbacks());

  pServer->start();

  // Explicit advertising payload so it can't overflow 31 bytes:
  //   adv packet:    flags (3) + 128-bit service UUID (18) = 21 bytes
  //   scan response: complete local name
  NimBLEAdvertisementData advData;
  advData.setFlags(BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP);
  advData.addServiceUUID(SERVICE_UUID);

  NimBLEAdvertisementData scanData;
  scanData.setName(gDeviceName);

  NimBLEAdvertising *pAdvertising = NimBLEDevice::getAdvertising();
  pAdvertising->setAdvertisementData(advData);
  pAdvertising->setScanResponseData(scanData);
  pAdvertising->enableScanResponse(true);
  pAdvertising->setMinInterval(0x30); // 30 ms
  pAdvertising->setMaxInterval(0x60); // 60 ms

  bool ok = startAdv();

  Serial.printf("[BLE] '%s' addr %s advertising=%d\n", gDeviceName,
                NimBLEDevice::getAddress().toString().c_str(), (int)ok);
  digitalWrite(RADIO_LED, ok ? HIGH : LOW);

  xTaskCreate(advWatchdogTask, "bleAdvWd", 3072, nullptr, 1, nullptr);
}

} // namespace BLE
