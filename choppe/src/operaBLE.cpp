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

void setupBLE() {  

    // Create the BLE Device
    BLEDevice::init(BLE_NAME);

    // Create the BLE Server
    pServer = BLEDevice::createServer();
    pServer->setCallbacks(new MyServerCallbacks());

    // Create the BLE Service
    BLEService *pService = pServer->createService(SERVICE_UUID);

    // Create a BLE Characteristic
    pTxCharacteristic = pService->createCharacteristic(CHARACTERISTIC_UUID_TX, BLECharacteristic::PROPERTY_NOTIFY);
    pTxCharacteristic->addDescriptor(new BLE2902());
    BLECharacteristic *pRxCharacteristic = pService->createCharacteristic(CHARACTERISTIC_UUID_RX, BLECharacteristic::PROPERTY_WRITE);
    pRxCharacteristic->setCallbacks(new MyCallbacks());

    // Start the service
    pService->start();

    // Start advertising
    pServer->getAdvertising()->start();
    DBG_PRINT(F("\n[BLE] Aguardando conexão"));
}

void enviaBLE( String msg ) {
    msg += '\n';
    pTxCharacteristic->setValue(msg.c_str());
    pTxCharacteristic->notify();
}
#endif