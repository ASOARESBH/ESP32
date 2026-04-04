#include "operaBLE.h"

#ifdef USAR_ESP32_UART_BLE

    BLEServer         *pServer           = NULL;
    BLECharacteristic *pTxCharacteristic = NULL;
    bool               deviceConnected    = false;
    bool               oldDeviceConnected = false;

    // ─────────────────────────────────────────────────────────────────────────
    // Advertising explícito com UUID do serviço e sem renegociação de intervalo.
    // ─────────────────────────────────────────────────────────────────────────
    static void iniciaAdvertising() {
        BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
        pAdvertising->addServiceUUID(SERVICE_UUID);
        pAdvertising->setScanResponse(true);
        pAdvertising->setMinPreferred(0x00);
        pAdvertising->setMaxPreferred(0x00);
        BLEDevice::startAdvertising();
        DBG_PRINT(F("\n[BLE] Advertising iniciado"));
    }

    // ─────────────────────────────────────────────────────────────────────────
    // Callbacks de conexão/desconexão — reconexão gerenciada pela taskBLE.
    // ─────────────────────────────────────────────────────────────────────────
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

    // ─────────────────────────────────────────────────────────────────────────
    // Callback de recepção de dados — conversão segura do payload.
    // ─────────────────────────────────────────────────────────────────────────
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

    // ─────────────────────────────────────────────────────────────────────────
    // Callbacks de segurança BLE — modo Passkey Entry com PIN fixo.
    //
    // COMO FUNCIONA O PAREAMENTO COM PIN FIXO NO ANDROID:
    //
    // O protocolo usado é "Passkey Entry" (BLE Legacy Pairing):
    //   • ioCap = ESP_IO_CAP_OUT  → ESP32 declara que "tem display"
    //   • O Android interpreta isso como: "o dispositivo exibe um PIN,
    //     o usuário precisa digitá-lo aqui no tablet"
    //   • O Android exibe um campo de entrada numérica
    //   • O usuário digita 259087
    //   • O Android envia o PIN para o ESP32 via protocolo SMP
    //   • O ESP32 valida em onPassKeyRequest() retornando o mesmo valor
    //
    // POR QUE NÃO USAR SC (LE Secure Connections):
    //   • SC força o protocolo "Numeric Comparison" onde AMBOS os lados
    //     geram um código aleatório e o usuário apenas confirma se são iguais
    //   • Nesse modo NÃO é possível ter PIN fixo — o código é sempre aleatório
    //   • Para PIN fixo digitado pelo usuário, deve-se usar Legacy Pairing
    //     (sem SC), com authMode = ESP_LE_AUTH_REQ_MITM_BOND
    // ─────────────────────────────────────────────────────────────────────────
    class MySecurityCallbacks : public BLESecurityCallbacks {

        // Retorna o PIN fixo que o ESP32 "exibe" (e o Android pede ao usuário)
        uint32_t onPassKeyRequest() {
            DBG_PRINTF("\n[BLE] PIN solicitado, retornando: %06lu", (unsigned long)BLE_PIN);
            return BLE_PIN;
        }

        // Notificação do PIN gerado pelo peer (não usado no Passkey Entry)
        void onPassKeyNotify(uint32_t pass_key) {
            DBG_PRINTF("\n[BLE] PIN notificado: %06lu", (unsigned long)pass_key);
        }

        // Confirmação numérica — não é chamada no Passkey Entry (apenas em SC)
        bool onConfirmPIN(uint32_t pass_key) {
            DBG_PRINTF("\n[BLE] Confirma PIN: %06lu == %06lu ? %s",
                (unsigned long)pass_key,
                (unsigned long)BLE_PIN,
                (pass_key == BLE_PIN) ? "SIM" : "NAO");
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
                DBG_PRINTF("\n[BLE] Falha na autenticacao, motivo: %d", cmpl.fail_reason);
            }
        }
    };

    // ─────────────────────────────────────────────────────────────────────────
    // Task dedicada para gerenciar reconexão BLE de forma segura fora dos
    // callbacks (evita race conditions na pilha BLE do ESP32).
    // ─────────────────────────────────────────────────────────────────────────
    void taskBLE(void *pvParameters) {
        DBG_PRINT(F("\n[BLE] Task de gerenciamento iniciada"));
        for (;;) {
            if (!deviceConnected && oldDeviceConnected) {
                vTaskDelay(pdMS_TO_TICKS(500));
                iniciaAdvertising();
                oldDeviceConnected = false;
                DBG_PRINT(F("\n[BLE] Aguardando nova conexão"));
            }
            if (deviceConnected && !oldDeviceConnected) {
                oldDeviceConnected = true;
            }
            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }

void setupBLE() {

    // ─── Nome BLE dinâmico: CHOPP_ + 4 últimos hex do MAC WiFi ─────────────
    String mac = WiFi.macAddress();   // ex: "A4:CF:12:B8:E0:5F"
    mac.replace(":", "");             // ex: "A4CF12B8E05F"
    String bleName = String(BLE_NAME_PREFIX) + mac.substring(mac.length() - 4);
    bleName.toUpperCase();            // ex: "CHOPP_E05F"
    DBG_PRINT(F("\n[BLE] Nome: "));
    DBG_PRINT(bleName);

    // ─── Inicializa dispositivo BLE ─────────────────────────────────────────
    BLEDevice::init(bleName.c_str());

    // ─── Configuração de segurança: Passkey Entry com PIN fixo ──────────────
    //
    // Tabela de modos de pareamento BLE:
    //
    // | authMode                       | ioCap            | Comportamento Android        |
    // |--------------------------------|------------------|------------------------------|
    // | SC_MITM_BOND                   | IO_CAP_OUT       | Exibe código aleatório (NC)  | ← era o problema
    // | SC_MITM_BOND                   | IO_CAP_IN        | Exibe código aleatório (NC)  | ← ainda NC
    // | MITM_BOND (sem SC)             | IO_CAP_OUT       | Pede PIN para digitar ✓      | ← CORRETO
    // | BOND (sem MITM)                | IO_CAP_NONE      | Just Works, sem PIN          |
    //
    // ESP_LE_AUTH_REQ_MITM_BOND = MITM + BOND, sem LE Secure Connections
    // ESP_IO_CAP_OUT = ESP32 declara ter "display" → Android pede PIN ao usuário
    //
    BLEDevice::setEncryptionLevel(ESP_BLE_SEC_ENCRYPT_MITM);
    BLEDevice::setSecurityCallbacks(new MySecurityCallbacks());

    uint8_t authReq = ESP_LE_AUTH_REQ_MITM_BOND;  // Legacy Pairing + MITM + Bonding
    uint8_t ioCap   = ESP_IO_CAP_OUT;              // ESP32 tem "display" → Android digita PIN
    uint8_t keySize = 16;
    uint8_t initKey = ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK;
    uint8_t rspKey  = ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK;

    esp_ble_gap_set_security_param(ESP_BLE_SM_AUTHEN_REQ_MODE, &authReq, sizeof(uint8_t));
    esp_ble_gap_set_security_param(ESP_BLE_SM_IOCAP_MODE,      &ioCap,   sizeof(uint8_t));
    esp_ble_gap_set_security_param(ESP_BLE_SM_MAX_KEY_SIZE,    &keySize, sizeof(uint8_t));
    esp_ble_gap_set_security_param(ESP_BLE_SM_SET_INIT_KEY,    &initKey, sizeof(uint8_t));
    esp_ble_gap_set_security_param(ESP_BLE_SM_SET_RSP_KEY,     &rspKey,  sizeof(uint8_t));

    // ─── Cria servidor BLE ──────────────────────────────────────────────────
    pServer = BLEDevice::createServer();
    pServer->setCallbacks(new MyServerCallbacks());

    // ─── Cria serviço UART BLE ──────────────────────────────────────────────
    BLEService *pService = pServer->createService(SERVICE_UUID);

    pTxCharacteristic = pService->createCharacteristic(
        CHARACTERISTIC_UUID_TX,
        BLECharacteristic::PROPERTY_NOTIFY
    );
    pTxCharacteristic->addDescriptor(new BLE2902());

    BLECharacteristic *pRxCharacteristic = pService->createCharacteristic(
        CHARACTERISTIC_UUID_RX,
        BLECharacteristic::PROPERTY_WRITE
    );
    pRxCharacteristic->setCallbacks(new MyCallbacks());

    // ─── Inicia serviço, advertising e task de gerenciamento ────────────────
    pService->start();
    iniciaAdvertising();
    xTaskCreate(taskBLE, "taskBLE", 4096, NULL, 1, NULL);

    DBG_PRINT(F("\n[BLE] Setup concluído — aguardando conexão com PIN 259087"));
}

// ─────────────────────────────────────────────────────────────────────────────
// enviaBLE() protegida: só notifica se houver conexão ativa.
// ─────────────────────────────────────────────────────────────────────────────
void enviaBLE(String msg) {
    if (!deviceConnected || pTxCharacteristic == NULL) {
        DBG_PRINT(F("\n[BLE] enviaBLE ignorado: sem conexão"));
        return;
    }
    msg += '\n';
    pTxCharacteristic->setValue(msg.c_str());
    pTxCharacteristic->notify();
}

#endif
