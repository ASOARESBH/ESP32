#include "operaBLE.h"

#ifdef USAR_ESP32_UART_BLE

    BLEServer  *pServer           = NULL;
    BLECharacteristic *pTxCharacteristic = NULL;
    bool deviceConnected          = false;
    bool oldDeviceConnected       = false;

    // ─────────────────────────────────────────────────────────────────────────
    // CORREÇÃO 1 – Advertising configurado de forma explícita e completa.
    // O advertising precisa incluir o UUID do serviço para que o Android
    // consiga filtrar e reconectar corretamente após uma desconexão.
    // ─────────────────────────────────────────────────────────────────────────
    static void iniciaAdvertising() {
        BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
        pAdvertising->addServiceUUID(SERVICE_UUID);
        pAdvertising->setScanResponse(true);
        // CORREÇÃO 2 – Desativa preferred connection interval automático.
        // Valores automáticos podem causar renegociação de parâmetros de
        // conexão que derruba a sessão no Android.
        pAdvertising->setMinPreferred(0x00);
        pAdvertising->setMaxPreferred(0x00);
        BLEDevice::startAdvertising();
        DBG_PRINT(F("\n[BLE] Advertising iniciado"));
    }

    // ─────────────────────────────────────────────────────────────────────────
    // CORREÇÃO 3 – Callbacks do servidor com flag oldDeviceConnected.
    // O padrão recomendado pela Espressif para reconexão segura é usar duas
    // flags (deviceConnected / oldDeviceConnected) e tratar a transição na
    // task principal — NÃO chamar startAdvertising() diretamente dentro do
    // callback onDisconnect(), pois isso ocorre em contexto de interrupção
    // BLE e pode corromper o estado interno da pilha.
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
            // NÃO reinicia advertising aqui — feito de forma segura em taskBLE()
        }
    };

    // ─────────────────────────────────────────────────────────────────────────
    // CORREÇÃO 4 – onWrite usa String diretamente via pCharacteristic->getValue()
    // como std::string e converte de forma segura, evitando acesso fora de
    // bounds em payloads com caracteres nulos ou lixo.
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
    // CORREÇÃO 5 – Segurança BLE simplificada para Android.
    // O modo ESP_LE_AUTH_REQ_SC_MITM_BOND com ESP_IO_CAP_OUT exige que o
    // Android EXIBA o PIN e o usuário confirme no ESP32 — o Android não tem
    // como digitar o PIN nesse modo, causando falha de pareamento.
    // A combinação correta para "tablet digita o PIN" é:
    //   • authMode = ESP_LE_AUTH_REQ_SC_MITM_BOND  (MITM + bonding)
    //   • ioCap    = ESP_IO_CAP_IN                 (ESP32 recebe o PIN)
    // Assim o Android exibe o teclado numérico e o usuário digita 259087.
    // ─────────────────────────────────────────────────────────────────────────
    class MySecurityCallbacks : public BLESecurityCallbacks {
        // Chamado quando o ESP32 precisa fornecer o PIN (modo IO_CAP_OUT)
        uint32_t onPassKeyRequest() {
            DBG_PRINTF("\n[BLE] PIN gerado: %06lu", (unsigned long)BLE_PIN);
            return BLE_PIN;
        }
        // Chamado quando o ESP32 recebe notificação do PIN gerado pelo peer
        void onPassKeyNotify(uint32_t pass_key) {
            DBG_PRINTF("\n[BLE] PIN notificado: %06lu", (unsigned long)pass_key);
        }
        // Chamado para confirmação numérica (Numeric Comparison)
        bool onConfirmPIN(uint32_t pass_key) {
            DBG_PRINTF("\n[BLE] Confirma PIN: %06lu", (unsigned long)pass_key);
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
                DBG_PRINTF("\n[BLE] Falha autenticacao, motivo: %d", cmpl.fail_reason);
            }
        }
    };

    // ─────────────────────────────────────────────────────────────────────────
    // CORREÇÃO 6 – Task dedicada para gerenciar o ciclo de vida da conexão BLE.
    // Gerenciar reconexão fora dos callbacks evita race conditions e garante
    // que o advertising seja reiniciado de forma segura após desconexão.
    // A task também protege enviaBLE() de ser chamada sem conexão ativa.
    // ─────────────────────────────────────────────────────────────────────────
    void taskBLE(void *pvParameters) {
        DBG_PRINT(F("\n[BLE] Task de gerenciamento iniciada"));
        for (;;) {
            // Transição: estava conectado, agora desconectou
            if (!deviceConnected && oldDeviceConnected) {
                vTaskDelay(pdMS_TO_TICKS(500)); // aguarda estabilização da pilha BLE
                iniciaAdvertising();
                oldDeviceConnected = false;
                DBG_PRINT(F("\n[BLE] Aguardando nova conexão"));
            }
            // Transição: acabou de conectar
            if (deviceConnected && !oldDeviceConnected) {
                oldDeviceConnected = true;
            }
            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }

void setupBLE() {

    // ─── Monta nome BLE dinâmico: CHOPP_ + 4 últimos hex do MAC WiFi ────────
    String mac = WiFi.macAddress();   // ex: "A4:CF:12:B8:E0:5F"
    mac.replace(":", "");             // ex: "A4CF12B8E05F"
    String bleName = String(BLE_NAME_PREFIX) + mac.substring(mac.length() - 4);
    bleName.toUpperCase();            // ex: "CHOPP_E05F"
    DBG_PRINT(F("\n[BLE] Nome: "));
    DBG_PRINT(bleName);

    // ─── Inicializa dispositivo BLE ──────────────────────────────────────────
    BLEDevice::init(bleName.c_str());

    // ─── CORREÇÃO 5 (continuação) – Parâmetros de segurança via esp_ble_gap ──
    // Usar esp_ble_gap_set_security_param() diretamente é mais confiável que
    // a classe BLESecurity para garantir que os parâmetros sejam aplicados
    // antes do primeiro advertising.
    BLEDevice::setEncryptionLevel(ESP_BLE_SEC_ENCRYPT_MITM);
    BLEDevice::setSecurityCallbacks(new MySecurityCallbacks());

    uint8_t authReq   = ESP_LE_AUTH_REQ_SC_MITM_BOND;
    uint8_t ioCap     = ESP_IO_CAP_IN;   // ESP32 recebe PIN → Android exibe teclado
    uint8_t keySize   = 16;
    uint8_t initKey   = ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK;
    uint8_t rspKey    = ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK;

    esp_ble_gap_set_security_param(ESP_BLE_SM_AUTHEN_REQ_MODE,  &authReq, sizeof(uint8_t));
    esp_ble_gap_set_security_param(ESP_BLE_SM_IOCAP_MODE,       &ioCap,   sizeof(uint8_t));
    esp_ble_gap_set_security_param(ESP_BLE_SM_MAX_KEY_SIZE,     &keySize, sizeof(uint8_t));
    esp_ble_gap_set_security_param(ESP_BLE_SM_SET_INIT_KEY,     &initKey, sizeof(uint8_t));
    esp_ble_gap_set_security_param(ESP_BLE_SM_SET_RSP_KEY,      &rspKey,  sizeof(uint8_t));

    // ─── Cria servidor BLE ───────────────────────────────────────────────────
    pServer = BLEDevice::createServer();
    pServer->setCallbacks(new MyServerCallbacks());

    // ─── Cria serviço e características ─────────────────────────────────────
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

    // ─── Inicia serviço e advertising ───────────────────────────────────────
    pService->start();
    iniciaAdvertising();

    // ─── CORREÇÃO 6 – Inicia task de gerenciamento da conexão BLE ───────────
    xTaskCreate(taskBLE, "taskBLE", 4096, NULL, 1, NULL);

    DBG_PRINT(F("\n[BLE] Setup concluído"));
}

// ─────────────────────────────────────────────────────────────────────────────
// CORREÇÃO 7 – enviaBLE() protegida: só notifica se houver conexão ativa.
// Chamar notify() sem conexão ativa causa crash por acesso a ponteiro nulo
// no descritor BLE2902 (CCCD), que é o comportamento observado como
// "desconexão" — na verdade era um panic/reset do ESP32.
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
