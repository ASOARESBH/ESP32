#include "operaBLE.h"

#ifdef USAR_ESP32_UART_BLE

    BLEServer         *pServer            = NULL;
    BLECharacteristic *pTxCharacteristic  = NULL;
    bool               deviceConnected    = false;
    bool               oldDeviceConnected = false;

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
            deviceConnected = true;
            digitalWrite(PINO_STATUS, LED_STATUS_ON);
            DBG_PRINT(F("\n[BLE] Conectado"));
        }

        void onDisconnect(BLEServer *pServer) {
            deviceConnected = false;
            digitalWrite(PINO_STATUS, !LED_STATUS_ON);
            DBG_PRINT(F("\n[BLE] Desconectado"));
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

    class MySecurityCallbacks : public BLESecurityCallbacks {
        uint32_t onPassKeyRequest() {
            DBG_PRINTF("\n[BLE] PIN solicitado, retornando: %06lu", (unsigned long)BLE_PIN);
            return BLE_PIN;
        }

        void onPassKeyNotify(uint32_t pass_key) {
            DBG_PRINTF("\n[BLE] PIN notificado: %06lu", (unsigned long)pass_key);
        }

        bool onConfirmPIN(uint32_t pass_key) {
            DBG_PRINTF(
                "\n[BLE] Confirma PIN: %06lu == %06lu ? %s",
                (unsigned long)pass_key,
                (unsigned long)BLE_PIN,
                (pass_key == BLE_PIN) ? "SIM" : "NAO"
            );
            return (pass_key == BLE_PIN);
        }

        bool onSecurityRequest() {
            DBG_PRINT(F("\n[BLE] Seguranca solicitada"));
            return true;
        }

        void onAuthenticationComplete(esp_ble_auth_cmpl_t cmpl) {
            if (cmpl.success) {
                DBG_PRINT(F("\n[BLE] Autenticado com sucesso"));
            } else {
                DBG_PRINTF("\n[BLE] Falha na autenticacao, motivo: %d", cmpl.fail_reason);
            }
        }
    };

    static String geraNomeBle() {
        const uint64_t efuseMac = ESP.getEfuseMac() & 0xFFFFFFFFFFFFULL;
        char macHex[13];

        snprintf(macHex, sizeof(macHex), "%012llX", efuseMac);

        String bleName = String(BLE_NAME_PREFIX) + String(macHex).substring(0, 4);
        bleName.toUpperCase();
        return bleName;
    }

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
        String bleName = geraNomeBle();
        DBG_PRINT(F("\n[BLE] Nome: "));
        DBG_PRINT(bleName);

        BLEDevice::init(bleName.c_str());
        // Modo de segurança "Just Works" - sem PIN, sem MITM
        BLEDevice::setEncryptionLevel(ESP_BLE_SEC_ENCRYPT_NO_MITM);
        BLEDevice::setSecurityCallbacks(new MySecurityCallbacks());

        uint8_t authReq  = ESP_LE_AUTH_NO_BOND;  // Sem necessidade de bond/pareamento
        uint8_t ioCap    = ESP_IO_CAP_NONE;
        uint8_t keySize  = 16;
        uint8_t initKey  = ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK;
        uint8_t rspKey   = ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK;

        // Removido: ESP_BLE_SM_SET_STATIC_PASSKEY - sem PIN fixo
        esp_ble_gap_set_security_param(ESP_BLE_SM_AUTHEN_REQ_MODE, &authReq, sizeof(authReq));
        esp_ble_gap_set_security_param(ESP_BLE_SM_IOCAP_MODE, &ioCap, sizeof(ioCap));
        esp_ble_gap_set_security_param(ESP_BLE_SM_MAX_KEY_SIZE, &keySize, sizeof(keySize));
        esp_ble_gap_set_security_param(ESP_BLE_SM_SET_INIT_KEY, &initKey, sizeof(initKey));
        esp_ble_gap_set_security_param(ESP_BLE_SM_SET_RSP_KEY, &rspKey, sizeof(rspKey));

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

        DBG_PRINT(F("\n[BLE] Setup concluido - modo Just Works (sem PIN)"));
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
