#include "operaBuzzer.h"

#ifdef USAR_BUZZER

    static void beep(uint16_t freq, uint32_t ms) {
        tone(PINO_BUZZER, freq, ms);
        vTaskDelay(pdMS_TO_TICKS(ms + 20));
        noTone(PINO_BUZZER);
    }

    void setupBuzzer() {
        pinMode(PINO_BUZZER, OUTPUT);
        digitalWrite(PINO_BUZZER, LOW);
        DBG_PRINT(F("\n[BUZZ] Pino: "));
        DBG_PRINT(PINO_BUZZER);
        DBG_PRINT(F(" — setup concluido"));
    }

    // 1 bipe de 200ms — valvula aberta, fluxo iniciando
    void buzzerIN() {
        beep(880, 200);
    }

    // 2 bipes de 150ms — ciclo encerrado
    void buzzerFN() {
        beep(1100, 150);
        vTaskDelay(pdMS_TO_TICKS(80));
        beep(1100, 150);
    }

#endif
