#include "Arduino.h"

#ifndef _CONFIG_H_
    #define _CONFIG_H_   

    // Definições para debug via porta serial (Serial Monitor)
    #define debug_debug

    #define PULSO_LITRO 5880 //1L = 5880 square waves
    #define RELE_ON     LOW
    #define SENSOR_ON   LOW
    #define TIMER_OUT_SENSOR 10000

    #define COMANDO_ML "ML:" // Libera quantidade de ML
    #define COMANDO_PL "PL:" // Configura pulso por litro no sensor de fluxo
    #define COMANDO_ID "ID:" // Tag rfid lida
    #define COMANDO_LB "LB:" // Aciona liberação continua
    #define COMANDO_VZ "VZ:" // Vazão
    #define COMANDO_QP "QP:" // Quantidade de pulsos
    #define COMANDO_RI "RI:" // Registra id RFID do administrador (para remover basta gravar uma zerada)
    #define COMANDO_VP "VP:" // Volume parcial
    #define COMANDO_TO "TO:" // Configura timeout, tempo aguardando inicio do fluxo
    
    // Habilita modulos para compilação
    #define USAR_ESP32_UART_BLE
    //#define USAR_PAGINA_CONFIG
    //#define USAR_RFID
    
    // pinout
    #ifdef ARDUINO_ESP32S3_DEV 
        #define PINO_RELE           48
        #define PINO_STATUS         21 //47
        #define PINO_SENSOR_FLUSO   2

        #define PINO_RC522_SSEL 14
        #define PINO_RC522_RSET 13
        #define PINO_RC522_MOSI 36
        #define PINO_RC522_MISO 37
        #define PINO_RC522_SCLK 35
    #elif defined(ARDUINO_ESP32_DEV)
        
        #define PINO_RELE           16
        #define PINO_STATUS         2

        #define PINO_RC522_SSEL 5
        #define PINO_RC522_RSET 4
        #define PINO_RC522_MOSI 23
        #define PINO_RC522_MISO 19
        #define PINO_RC522_SCLK 18

        #define LED_STATUS_ON HIGH
    #else
        #define PINO_RC522_SSEL 7
        #define PINO_RC522_RSET 3
        #define PINO_RC522_MOSI 6
        #define PINO_RC522_MISO 5
        #define PINO_RC522_SCLK 4

        #define PINO_SENSOR_FLUSO   0
        #define PINO_RELE           1
        #define PINO_STATUS         8
        #define LED_STATUS_ON LOW
    #endif

    // Prefixo do nome BLE — os 4 últimos dígitos do MAC WiFi são anexados em tempo de execução
    // Resultado final: CHOPP_XXXX (ex: CHOPP_B8E0)
    #define BLE_NAME_PREFIX "CHOPP_"

    // PIN de segurança BLE para pareamento com o tablet
    #define BLE_PIN 259087

    // Flag para identificar se os dados foram gravados na EEPROM
    #define MAGIC_FLAG_EEPROM 0xF2F2  
    
    // Dados para o modo AP (Access Point)
    #define AP_SSID     "CHOPPE"
    #define AP_PASSWORD "1234567890"
        
    // Configuração apenas para o período de desenvolvimetno
    #ifndef WIFI_DEFAULT_SSID
        #define WIFI_DEFAULT_SSID "brisa-448561"
        //#define WIFI_DEFAULT_SSID "ridimuim"
    #endif    
    #ifndef WIFI_DEFAULT_PSW
        #define WIFI_DEFAULT_PSW "9xmkuiw1"
        //#define WIFI_DEFAULT_PSW "88999448494"
    #endif    
   
    // Estrutura da variável de configuração
    typedef struct {
        uint16_t magicFlag;
        uint8_t modoAP;
        char wifiSSID[30];
        char wifiPass[30];
        char rfidMaster[12];
        uint32_t pulsosLitro;
        uint32_t timeOut;
    } __attribute__ ((packed)) config_t;

    #ifdef debug_debug
        #define DBG_WRITE(...)    Serial.write(__VA_ARGS__)
        #define DBG_PRINT(...)    Serial.print(__VA_ARGS__)
        #define DBG_PRINTF(...)   Serial.printf(__VA_ARGS__)
        #define DBG_PRINTLN(...)  Serial.println(__VA_ARGS__)
    #else
        #define DBG_WRITE(...)
        #define DBG_PRINT(...)
        #define DBG_PRINTF(...)
        #define DBG_PRINTLN(...)
    #endif
    
#endif
