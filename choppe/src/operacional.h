#include "config.h"
#ifndef _OPERACIONAL_H_
    #define _OPERACIONAL_H_
    #include <EEPROM.h>
    #include "operaBLE.h"
    #include "esp_task_wdt.h"
    
    void executaOperacao(String cmd);
    void taskLiberaML(void *pvParameters);
    void leConfiguracao();
    void gravaConfiguracao();
#endif    