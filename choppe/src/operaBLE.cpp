#include "operaBLE.h"

#ifdef USAR_ESP32_UART_BLE

    BLEServer         *pServer            = NULL;
    BLECharacteristic *pTxCharacteristic  = NULL;
    bool               deviceConnected    = false;
    bool               oldDeviceConnected = false;
    volatile bool      abortarLiberacao   = false;

    static void iniciaAdvertising() {
        BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
        pAdvertising->addServiceUUID(SERVICE_UUID);
        pAdvertising->setScanResponse(true);
        pAdvertising->setMinPreferred(0x00);
        pAdvertising->setMaxPreferred(0x00);
        BLEDevice::startAdvertising();
        DBG_PRINT(F("\n[BLE] Advertising iniciado"));
    }

    class MyServerCallbacks : public BLEServerCallbacks {
        void onConnect(BLEServer *pServer) {
            abortarLiberacao = false;
            deviceConnected  = true;
            digitalWrite(PINO_STATUS, LED_STATUS_ON);
            DBG_PRINT(F("\n[BLE] Conectado"));
        }

        void onDisconnect(BLEServer *pServer) {
            abortarLiberacao = true;
            deviceConnected  = false;
            digitalWrite(PINO_STATUS, !LED_STATUS_ON);
            DBG_PRINT(F("\n[BLE] Desconectado — abort liberacao"));
        }
    };

    class MyCallbacks : public BLECharacteristicCallbacks {
        void onWrite(BLECharacteristic *pCharacteristic) {
            std::string rxValue = pCharacteristic->getValue();
            if (rxValue.length() > 0) {
                String cmd(rxValue.c_str(), rxValue.length());
                cmd.trim();
                DBG_PRINT(F("\n[BLE] Recebido: "));
                DBG_PRINT(cmd);
                executaOperacao(cmd);
            }
        }
    };

    void taskBLE(void *pvParameters) {
        DBG_PRINT(F("\n[BLE] Task de gerenciamento iniciada"));
        for (;;) {
            if (!deviceConnected && oldDeviceConnected) {
                vTaskDelay(pdMS_TO_TICKS(500));
                iniciaAdvertising();
                oldDeviceConnected = false;
                DBG_PRINT(F("\n[BLE] Aguardando nova conexao"));
            }
            if (deviceConnected && !oldDeviceConnected) {
                oldDeviceConnected = true;
            }
            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }

    void setupBLE() {
        uint64_t mac = ESP.getEfuseMac();
        char bleName[14];
        snprintf(bleName, sizeof(bleName), "%s%02X%02X",
                 BLE_NAME_PREFIX,
                 (uint8_t)(mac & 0xFF),
                 (uint8_t)((mac >> 8) & 0xFF));

        DBG_PRINT(F("\n[BLE] Nome: "));
        DBG_PRINT(bleName);

        BLEDevice::init(bleName);

        pServer = BLEDevice::createServer();
        pServer->setCallbacks(new MyServerCallbacks());

        BLEService *pService = pServer->createService(SERVICE_UUID);

        pTxCharacteristic = pService->createCharacteristic(
            CHARACTERISTIC_UUID_TX,
            BLECharacteristic::PROPERTY_NOTIFY
        );
        pTxCharacteristic->setAccessPermissions(ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE);
        BLE2902 *pTxDescriptor = new BLE2902();
        pTxDescriptor->setAccessPermissions(ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE);
        pTxCharacteristic->addDescriptor(pTxDescriptor);

        BLECharacteristic *pRxCharacteristic = pService->createCharacteristic(
            CHARACTERISTIC_UUID_RX,
            BLECharacteristic::PROPERTY_WRITE
        );
        pRxCharacteristic->setAccessPermissions(ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE);
        pRxCharacteristic->setCallbacks(new MyCallbacks());

        pService->start();
        iniciaAdvertising();
        xTaskCreate(taskBLE, "taskBLE", 4096, NULL, 1, NULL);

        DBG_PRINT(F("\n[BLE] Setup concluido"));
    }

    void enviaBLE(String msg) {
        if (!deviceConnected || pTxCharacteristic == NULL) {
            DBG_PRINT(F("\n[BLE] enviaBLE ignorado: sem conexao"));
            return;
        }

        msg += '\n';
        pTxCharacteristic->setValue(msg.c_str());
        pTxCharacteristic->notify();
    }

#endif
