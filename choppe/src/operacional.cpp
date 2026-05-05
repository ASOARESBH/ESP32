#include "operacional.h"
/* 
    *** Apenas para observar ***
    Sensor de fluxo YF-S401
    Precision (Flow rate - pulse output) 0.3 ~ 6L / min ± 3%
    1L = 5880 pulsos
    6L = 35280 pulsos
    
    6L/min = 35280 / 60 => 588 pulsos/seg
    um cliclo = 1000ms / 588 => 1.700680272ms/pulso
    1L demora 5880 * 1.700680272 = 9999.99999936ms => 9.999999999s ~= 10s

*/

extern config_t configuracao;
extern xQueueHandle listaLiberarML;
extern TaskHandle_t taskRFIDHandle;

volatile uint32_t contadorPulso = 0;
volatile uint32_t quantidadePulso = 0;
volatile int64_t horaPulso = 0;
volatile int64_t ultimoPulsoAceito = 0;

static const int64_t MIN_INTERVALO_PULSO_US = 800; // YF-S401 max ~588Hz => ~1700us/pulso.

static uint32_t ultimaMetaML       = 0;    // meta do ultimo ciclo ($RS:)
static float    ultimoDispensadoML = 0.0f; // real dispensado no ultimo ciclo ($RS:)

void executaOperacao(String cmd) {
    cmd.trim();
    if (cmd == "PING") {
        DBG_PRINT(F("\n[OPER] PING recebido -> PONG"));
        #ifdef USAR_ESP32_UART_BLE
            enviaBLE("PONG");
        #endif
        return;
    }

    String rsp = "ERRO";
    if (cmd[0] != '$') {
        cmd = "!!!";
    }    
    String op  = cmd.substring(1,4);
    String param = cmd.substring(4);
    param.trim();
    if (op == COMANDO_ML) {
        uint32_t quantidade = (uint32_t)param.toInt();
        if (quantidade>0){
            if (xQueueSend(listaLiberarML,&quantidade,0) == pdTRUE ) {
                rsp = "OK";
            }
        }
    } else if (op == COMANDO_PL) {
        uint32_t quantidade = (uint32_t)param.toInt();
        if (quantidade>0){
            DBG_PRINT( F( "\n[OPER] Configurando pulsos/litro: "));
            DBG_PRINT(quantidade);
            configuracao.pulsosLitro = quantidade;
            gravaConfiguracao();
            rsp = "OK";
        } else {
            rsp = COMANDO_PL + String(configuracao.pulsosLitro);
        }
    } else if (op == COMANDO_LB) {
        uint32_t quantidade = 0xFFFFFFFF;
        if (xQueueSend(listaLiberarML,&quantidade,0) == pdTRUE ) {
            rsp = "OK";
        } else { 
            DBG_PRINT( F( "\n[OPER] Erro xQueueSend"));
        }
    } else if (op == COMANDO_TO) {
        uint32_t quantidade = (uint32_t)param.toInt();
        if (quantidade>0){
            DBG_PRINT( F( "\n[OPER] Configuração timeOut do sensor: "));
            DBG_PRINT(param);            
            configuracao.timeOut = quantidade * 1000UL; // converte segundos → ms
            rsp = "OK";
        } else {
            rsp = COMANDO_PL + String(configuracao.pulsosLitro);
        }
    } else if (op == COMANDO_RS) {
        if (ultimaMetaML > 0) {
            float restante = (float)ultimaMetaML - ultimoDispensadoML;
            uint32_t restanteML = (restante > 1.0f) ? (uint32_t)restante : 0;
            if (restanteML > 0) {
                if (xQueueSend(listaLiberarML, &restanteML, 0) == pdTRUE) {
                    DBG_PRINT(F("\n[OPER] Resume: enfileirando "));
                    DBG_PRINT(restanteML);
                    DBG_PRINT(F("mL restantes"));
                    rsp = COMANDO_RS + String(restanteML);
                }
            } else {
                rsp = COMANDO_RS + String(0); // ciclo anterior ja concluido
            }
        }
    } else if (op == "DB:") {
        // Diagnóstico: retorna config e estado atual do pino do sensor
        rsp  = "PIN="  + String(PINO_SENSOR_FLUSO);
        rsp += " VAL=" + String(digitalRead(PINO_SENSOR_FLUSO));
        rsp += " TO="  + String(configuracao.timeOut);
        rsp += " PL="  + String(configuracao.pulsosLitro);
        rsp += " QP="  + String(contadorPulso);
    } else {
        DBG_PRINT( F( "\n[OPER] Erro. Comando desconhecido"));
    }
    #ifdef USAR_ESP32_UART_BLE
        enviaBLE(rsp);
    #endif
}

void IRAM_ATTR fluxoISR() {
    int64_t agora = esp_timer_get_time();
    if ((agora - ultimoPulsoAceito) < MIN_INTERVALO_PULSO_US) {
        return;
    }
    ultimoPulsoAceito = agora;
    contadorPulso++;
    horaPulso = agora;
    if ((quantidadePulso)&&(!( contadorPulso < quantidadePulso ))) {
        digitalWrite(PINO_RELE,!RELE_ON);
        detachInterrupt(digitalPinToInterrupt(PINO_SENSOR_FLUSO));
    }
}

void taskLiberaML(void *pvParameters) {
    String statusRetorno;
    float mlLiberado = 0.0;
    //float vazao = 0.0;
    float pulsoML = 0.0;
    float tempoDecorridoS = 0;
    uint32_t ml = 0;
    int64_t tempoInicio = 0;
    unsigned long proximoStatus = 0;    
    DBG_PRINT( F( "\n[OPER] Task taskLiberaML iniciada"));
    
    for (;;){
        vTaskDelay(50);
        if (xQueueReceive(listaLiberarML,&ml,0) == pdTRUE){            
            if (ml){
                pulsoML = (float)configuracao.pulsosLitro / 1000.0;
                DBG_PRINT(F("\n[OPER] liberando (Pulsos/ML): "));
                DBG_PRINT(pulsoML);
                
                // Inicia variáveis para calculo da vazão
                tempoDecorridoS = 0.0;
                mlLiberado = 0.0;
                //vazao = 0.0;                
                
                if (ml == 0xFFFFFFFF) {
                    ml = 0;
                    quantidadePulso = 0;
                    ultimaMetaML = 0; // modo LB nao tem meta para retomar
                } else {
                    quantidadePulso = (uint32_t)(pulsoML * (float)ml);
                    ultimaMetaML = ml;
                    DBG_PRINT(F("\n[OPER] Liberando (ML): "));
                    DBG_PRINT(ml);
                    DBG_PRINT(F("\n[OPER] liberando (Pulsos): "));
                    DBG_PRINT(quantidadePulso);
                }                
                
                contadorPulso = 0;
                ultimoPulsoAceito = 0;
                attachInterrupt(digitalPinToInterrupt(PINO_SENSOR_FLUSO), fluxoISR, RISING);
                
                // Aciona valvula
                digitalWrite(PINO_RELE,RELE_ON);
                #ifdef USAR_ESP32_UART_BLE
                    enviaBLE(COMANDO_IN);
                #endif
                tempoInicio = esp_timer_get_time();
                horaPulso = tempoInicio;
                // Timeout adaptativo: reinicia a cada pulso recebido.
                // Fecha somente se ficar SEM PULSOS por configuracao.timeOut ms consecutivos.
                int64_t ultimoPulsoCheck = esp_timer_get_time();
                uint32_t ultimoContador = contadorPulso;

                while ((contadorPulso < quantidadePulso) || (quantidadePulso == 0)) {
                    vTaskDelay(50);

                    #ifdef USAR_ESP32_UART_BLE
                    if (abortarLiberacao) {
                        DBG_PRINT(F("\n[OPER] Abort: BLE desconectado. Fechando valvula."));
                        break;
                    }
                    #endif

                    if (contadorPulso != ultimoContador) {
                        ultimoContador = contadorPulso;
                        ultimoPulsoCheck = esp_timer_get_time();
                    }

                    int64_t inatividade = esp_timer_get_time() - ultimoPulsoCheck;
                    if (inatividade > ((int64_t)configuracao.timeOut * 1000LL)) {
                        DBG_PRINT(F("\n[OPER] Timeout de inatividade — sem fluxo por "));
                        DBG_PRINT(configuracao.timeOut);
                        DBG_PRINT(F("ms. Fechando valvula."));
                        break;
                    }

                    if (millis() > proximoStatus) {
                        proximoStatus = millis() + 2000UL;
                        tempoDecorridoS = (float)(esp_timer_get_time() - tempoInicio) / 1000000.0;
                        if (contadorPulso) {
                            mlLiberado = (float)contadorPulso / pulsoML;
                        }
                        #ifdef USAR_ESP32_UART_BLE
                            statusRetorno = COMANDO_VP + String(mlLiberado, 3);
                            enviaBLE(statusRetorno);
                        #endif
                    }
                }

                digitalWrite(PINO_RELE,!RELE_ON);
                detachInterrupt(digitalPinToInterrupt(PINO_SENSOR_FLUSO));

                if (pulsoML > 0) mlLiberado = (float)contadorPulso / pulsoML;
                ultimoDispensadoML = mlLiberado;

                // Envia status
                #ifdef USAR_ESP32_UART_BLE
                    statusRetorno = COMANDO_QP + String(contadorPulso);
                    enviaBLE(statusRetorno);
                
                    // Envia status de ML liberado                
                    statusRetorno = COMANDO_ML;
                    if (contadorPulso == quantidadePulso){
                        statusRetorno += String(ml);
                    } else {
                        statusRetorno += String(mlLiberado);
                    }
                    enviaBLE(statusRetorno);
                    enviaBLE(COMANDO_FN);
                #endif

                DBG_PRINT(F("\n[OPER] Liberado (L): "));
                DBG_PRINT(mlLiberado/1000,3);
                DBG_PRINT(F("\n[OPER] Tempo (S): "));
                DBG_PRINT(tempoDecorridoS);
                //DBG_PRINT(F("\n[OPER] Vazao (L/min): "));
                //DBG_PRINT(vazao);
                DBG_PRINT(F("\n[OPER] Quantidade pulsos: "));
                DBG_PRINT(contadorPulso);
            }                
        }
    }
}

// Recupera configuração gravada na EEPROM
void leConfiguracao() {  
    String stemp;
    DBG_PRINT(F("[OPER] Lendo configuração"));
    EEPROM.begin(sizeof(config_t));
    EEPROM.get( 0, configuracao );  
  
    // Inicializa com configurações padrão,quando as configurações não foram gravadas pela primeira vez ou em caso de reset //
    if ( configuracao.magicFlag != MAGIC_FLAG_EEPROM ) {
        DBG_PRINT(F(", carregando configuração de fábrica"));
        memset(&configuracao,0,sizeof(config_t));
        configuracao.magicFlag = MAGIC_FLAG_EEPROM;
        configuracao.modoAP = 0;

        stemp = WIFI_DEFAULT_SSID;
        stemp.toCharArray(configuracao.wifiSSID, stemp.length()+1);
        stemp = WIFI_DEFAULT_PSW;
        stemp.toCharArray(configuracao.wifiPass, stemp.length()+1);

        configuracao.pulsosLitro = (uint32_t)PULSO_LITRO;
        configuracao.timeOut     = (uint32_t)TIMER_OUT_SENSOR;
        gravaConfiguracao();

    } else {
        bool precisaGravar = false;

        // timeOut deve estar em ms no range 1000–300000.
        // Valores fora disso indicam legado (segundos) ou lixo de EEPROM.
        if (configuracao.timeOut > 0 && configuracao.timeOut < 1000) {
            // Legado em segundos → converte para ms
            DBG_PRINT(F(", migrando timeOut para ms"));
            configuracao.timeOut *= 1000;
            precisaGravar = true;
        } else if (configuracao.timeOut == 0 || configuracao.timeOut > 300000) {
            // Inválido (0 ou lixo) → reseta para padrão
            DBG_PRINT(F(", corrigindo timeOut invalido"));
            configuracao.timeOut = (uint32_t)TIMER_OUT_SENSOR;
            precisaGravar = true;
        }

        // pulsosLitro = 0 causaria divisão indireta por zero no cálculo de ML.
        if (configuracao.pulsosLitro == 0 || configuracao.pulsosLitro > 100000) {
            DBG_PRINT(F(", corrigindo pulsosLitro invalido"));
            configuracao.pulsosLitro = (uint32_t)PULSO_LITRO;
            precisaGravar = true;
        }

        if (precisaGravar) gravaConfiguracao();
    }
    DBG_PRINTLN();
}

// Salva configuraçao na EEPROM
void gravaConfiguracao() {
    DBG_PRINT(F("\n[OPER] Gravando configuração "));
    EEPROM.put( 0, configuracao );
    if (EEPROM.commit()) {
        DBG_PRINT(F("OK"));
    } else {
        DBG_PRINT(F(" *** Falha"));
    }
}
