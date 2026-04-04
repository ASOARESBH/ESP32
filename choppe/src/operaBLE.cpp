#include "operaBLE.h"

#ifdef USAR_ESP32_UART_BLE

    BLEServer *pServer = NULL;
    BLECharacteristic *pTxCharacteristic;
    bool deviceConnected = false;

    class MyServerCallbacks : public BLEServerCallbacks {
        void onConnect(BLEServer *pServer) {
            digitalWrite( PINO_STATUS, LED_STATUS_ON);
            DBG_PRINT(F("\n[BLE] Conectado"));
            deviceConnected = true;
        };

        void onDisconnect(BLEServer *pServer) {
            digitalWrite( PINO_STATUS, !LED_STATUS_ON);
            DBG_PRINT(F("\n[BLE] Desconectado"));            
            deviceConnected = false;            
            delay(500);
            pServer->startAdvertising();
            
        }
    };

    class MyCallbacks : public BLECharacteristicCallbacks {
        void onWrite(BLECharacteristic *pCharacteristic) {        
            String cmd = "";
            std::string rxValue = pCharacteristic->getValue();
            DBG_PRINT(F("\n[BLE] Recebido: "));
            if (rxValue.length() > 0) {
                for (int i = 0; i < rxValue.length(); i++) {                    
                    cmd += (char)rxValue[i];
                }
                DBG_PRINT(cmd);
                executaOperacao(cmd);
            }
        }
    };

    // Callback de segurança BLE: define o PIN para pareamento
    class MySecurityCallbacks : public BLESecurityCallbacks {
        uint32_t onPassKeyRequest() {
            DBG_PRINT(F("\n[BLE] PIN solicitado"));
            return BLE_PIN;
        }
        void onPassKeyNotify(uint32_t pass_key) {
            DBG_PRINTF("\n[BLE] PIN notificado: %06d", pass_key);
        }
        bool onConfirmPIN(uint32_t pass_key) {
            DBG_PRINTF("\n[BLE] Confirma PIN: %06d", pass_key);
            return (pass_key == BLE_PIN);
        }
        bool onSecurityRequest() {
            DBG_PRINT(F("\n[BLE] Segurança solicitada"));
            return true;
        }
        void onAuthenticationComplete(esp_ble_auth_cmpl_t cmpl) {
            if (cmpl.success) {
                DBG_PRINT(F("\n[BLE] Autenticado com sucesso"));
            } else {
                DBG_PRINT(F("\n[BLE] Falha na autenticacao"));
            }
        }
    };

void setupBLE() {

    // Monta o nome BLE dinamicamente: CHOPP_ + 4 últimos dígitos do MAC WiFi
    // O MAC WiFi tem formato XX:XX:XX:XX:XX:XX — usamos os 2 últimos octetos (sem ':')
    String mac = WiFi.macAddress(); // ex: "A4:CF:12:B8:E0:5F"
    // Remove os ':' e pega os últimos 4 caracteres hex (2 octetos finais sem separador)
    mac.replace(":", "");           // ex: "A4CF12B8E05F"
    String bleName = BLE_NAME_PREFIX + mac.substring(mac.length() - 4); // ex: "CHOPP_E05F"
    bleName.toUpperCase();

    DBG_PRINT(F("\n[BLE] Nome: "));
    DBG_PRINT(bleName);

    // Cria o dispositivo BLE com o nome dinâmico
    BLEDevice::init(bleName.c_str());

    // Configura segurança BLE com PIN fixo
    BLEDevice::setEncryptionLevel(ESP_BLE_SEC_ENCRYPT);
    BLEDevice::setSecurityCallbacks(new MySecurityCallbacks());

    BLESecurity *pSecurity = new BLESecurity();
    pSecurity->setAuthenticationMode(ESP_LE_AUTH_REQ_SC_MITM_BOND);
    pSecurity->setCapability(ESP_IO_CAP_OUT);
    pSecurity->setInitEncryptionKey(ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK);
    pSecurity->setRespEncryptionKey(ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK);

    // Cria o servidor BLE
    pServer = BLEDevice::createServer();
    pServer->setCallbacks(new MyServerCallbacks());

    // Cria o serviço BLE
    BLEService *pService = pServer->createService(SERVICE_UUID);

    // Cria as características BLE
    pTxCharacteristic = pService->createCharacteristic(CHARACTERISTIC_UUID_TX, BLECharacteristic::PROPERTY_NOTIFY);
    pTxCharacteristic->addDescriptor(new BLE2902());
    BLECharacteristic *pRxCharacteristic = pService->createCharacteristic(CHARACTERISTIC_UUID_RX, BLECharacteristic::PROPERTY_WRITE);
    pRxCharacteristic->setCallbacks(new MyCallbacks());

    // Inicia o serviço
    pService->start();

    // Inicia o advertising
    pServer->getAdvertising()->start();
    DBG_PRINT(F("\n[BLE] Aguardando conexão"));
}

void enviaBLE( String msg ) {
    msg += '\n';
    pTxCharacteristic->setValue(msg.c_str());
    pTxCharacteristic->notify();
}
#endif
