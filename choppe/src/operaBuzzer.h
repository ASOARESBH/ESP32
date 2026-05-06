#include "config.h"
#if !defined(_OPERA_BUZZER_) && defined(USAR_BUZZER)
    #define _OPERA_BUZZER_

    void setupBuzzer();
    void buzzerIN();  // 1 bipe curto: inicio da liberacao
    void buzzerFN();  // 2 bipes curtos: fim da liberacao

#endif
