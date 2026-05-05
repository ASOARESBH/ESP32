# Integracao Android <-> ESP32 Chopp

> **Firmware: 2026-05-05**
> Nome BLE dinamico `CHOPP_XXXX` | Just Works sem PIN | Abort automatico de valvula ao desconectar | Resume de ciclo incompleto via `$RS:`

---

## 1. Identificacao da placa

**ESP32 — implementado:**
- Nome BLE gerado em runtime: `CHOPP_` + byte0 + byte1 de `ESP.getEfuseMac()` em HEX maiusculo
- Exemplo: MAC `DC:B4:D9:9A:67:1A` → nome `CHOPP_DCB4`

**Android deve:**
- Obter `wifiMac` da API e derivar o nome esperado:
  - Remover separadores do MAC → `DCB4D99AB81A`
  - Prefixar com `CHOPP_` + primeiros 4 chars → `CHOPP_DCB4`
- Se a API ja fornecer o campo `bleName`, usa-lo diretamente
- Durante o scan BLE, **aceitar apenas** o dispositivo com o nome derivado
- Rejeitar qualquer outro dispositivo `CHOPP_` que nao corresponda ao MAC esperado

---

## 2. Parametros fixos BLE

| Campo | Valor |
|-------|-------|
| Service UUID | `6E400001-B5A3-F393-E0A9-E50E24DCCA9E` |
| RX UUID — Android → ESP32 | `6E400002-B5A3-F393-E0A9-E50E24DCCA9E` |
| TX UUID — ESP32 → Android | `6E400003-B5A3-F393-E0A9-E50E24DCCA9E` |
| MTU solicitado | `247` |
| autoConnect | `false` |
| transport | `TRANSPORT_LE` |

**Android deve:** solicitar `requestMtu(247)` logo apos `onConnectionStateChange(CONNECTED)`.

---

## 3. Seguranca e pareamento

**ESP32 — implementado:**
- Modo Just Works: sem PIN, sem confirmacao
- Characteristics RX, TX e descriptor `0x2902` permitem acesso sem criptografia

**Android deve:**
- **Nao** chamar `createBond()` nem exibir tela de PIN
- Conectar diretamente e prosseguir para o fluxo de conexao

---

## 4. Fluxo de conexao obrigatorio (Android)

```
1. Scan BLE → filtrar pelo nome CHOPP_XXXX esperado
2. connectGatt(context, false, callback, TRANSPORT_LE)
3. onConnectionStateChange(CONNECTED)
       └── requestMtu(247)
4. onMtuChanged
       └── discoverServices()
5. onServicesDiscovered
       └── localizar Service NUS e characteristics RX e TX
       └── setCharacteristicNotification(tx, true)
       └── writeDescriptor(0x2902, ENABLE_NOTIFICATION_VALUE)
6. onDescriptorWrite com sucesso
       └── estado = READY  ← app pode enviar comandos a partir daqui
```

**Android deve:**
- Nao enviar nenhum comando antes do estado `READY`
- Se qualquer passo falhar, desconectar e reiniciar o fluxo do zero
- Ao receber `onConnectionStateChange(DISCONNECTED)`, limpar estado `READY` e reiniciar

---

## 5. Comportamento ao desconectar

**ESP32 — implementado (2026-05-05):**
- `onDisconnect` seta a flag `abortarLiberacao = true`
- A `taskLiberaML` verifica a flag a cada 50 ms dentro do ciclo de liberacao
- Ao detectar a flag, encerra o ciclo e **fecha a valvula imediatamente**
- Ao reconectar (`onConnect`), a flag e zerada automaticamente

**Consequencias para o Android:**
- Se desconectar no meio de `$ML:` ou `$LB:`, a valvula fecha em ate 50 ms
- `QP:`, `ML:` e `FN:` **nao serao enviados** apos abort por desconexao
- Ao reconectar, assumir que a liberacao anterior foi interrompida
- Nao aguardar `FN:` apos reconexao — ver **secao 9** para retomar o volume restante via `$RS:`

---

## 6. Comandos Android -> ESP32

Todos os comandos sao escritos na characteristic **RX** (`6E400002-...`).

| Comando | Parametro | Descricao |
|---------|-----------|-----------|
| `PING` | — | Keepalive opcional — retorna `PONG` |
| `$ML:<ml>` | inteiro > 0 | Libera quantidade em mL |
| `$LB:` | — | Libera modo continuo (fecha pelo timeout de inatividade) |
| `$PL:<pulsos>` | inteiro > 0 | Configura pulsos/litro — persiste na EEPROM |
| `$PL:0` | 0 | GET: retorna valor atual como `PL:<valor>` |
| `$TO:<s>` | inteiro > 0 | Configura timeout de inatividade em **segundos** — persiste na EEPROM |
| `$TO:0` | 0 | GET: retorna valor atual como `TO:<valor_ms>` (ms, nao segundos) |
| `$RS:` | — | Retoma ciclo anterior incompleto — retorna `RS:<restante_ml>` ou `RS:0` se ja completo |
| `$DB:` | — | Diagnostico: retorna `PIN=`, `VAL=`, `TO=`, `PL=`, `QP=` |

> `$TO:` recebe **segundos** como entrada, mas `$TO:0` retorna em **ms** internamente.

---

## 7. Respostas ESP32 -> Android

Todas as respostas chegam como notificacoes na characteristic **TX** (`6E400003-...`), terminadas com `\n`.

| Resposta | Quando | Descricao |
|----------|--------|-----------|
| `PONG` | Apos `PING` | Confirma firmware ativo |
| `OK` | Apos comando aceito | Comando enfileirado ou executado |
| `ERRO` | Comando invalido ou parametro zero invalido | Incluindo quando ja ha operacao em andamento (`BUSY` nao implementado) |
| `IN:` | Inicio da liberacao | Valvula abriu, ciclo iniciado |
| `VP:<ml>` | A cada ~2s durante liberacao | Volume parcial em mL com 3 casas decimais |
| `QP:<pulsos>` | Fim do ciclo | Total de pulsos contados |
| `ML:<ml>` | Fim do ciclo | Volume final liberado em mL — **inteiro** se ciclo completo (`ML:300`), **float 2 casas** se interrompido (`ML:150.34`) |
| `FN:` | Fim do ciclo | Ciclo encerrado, valvula fechada |
| `PL:<valor>` | Resposta a `$PL:0` | Pulsos por litro configurado |
| `TO:<valor_ms>` | Resposta a `$TO:0` | Timeout de inatividade em ms |
| `RS:<restante_ml>` | Resposta a `$RS:` com pendencia | Confirmacao de resume: mL que serao dispensados |
| `RS:0` | Resposta a `$RS:` sem pendencia | Ciclo anterior ja estava completo, nada a retomar |

> **`BUSY` nao implementado:** operacao em andamento retorna `ERRO`. Android deve aguardar `FN:` antes de enviar novo `$ML:` ou `$RS:`.

---

## 8. Fluxo de liberacao de volume

Para liberar 300 mL:

```
Android                      ESP32
   |                            |
   |--- $ML:300 --------------->|
   |<-- OK ---------------------|   comando enfileirado
   |<-- IN: --------------------|   valvula abriu
   |<-- VP:0.000 ---------------|   inicio do fluxo
   |<-- VP:50.170 --------------|   atualizacao parcial (~2s)
   |<-- VP:150.340 -------------|   atualizacao parcial (~2s)
   |<-- QP:1764 ---------------|   fim: pulsos totais contados
   |<-- ML:300 -----------------|   fim: volume confirmado
   |<-- FN: --------------------|   ciclo encerrado
```

**Android deve:**
- Considerar ciclo **iniciado** ao receber `IN:`
- Atualizar progresso a cada `VP:`
- Considerar ciclo **concluido** somente ao receber `FN:`
- Se desconectar antes de `FN:`, assumir liberacao interrompida (valvula fechou no ESP32)

---

## 9. Retomada de ciclo incompleto (espuma / pausa do operador)

### Cenario

Em uma choperia autonoma, o operador pode recuar a alavanca manualmente enquanto o ciclo esta em andamento (ex.: espuma excessiva). O sensor para de pulsar, o timeout de inatividade dispara, a valvula fecha e o ciclo encerra com volume parcial. O Android recebe `ML:<parcial>` e `FN:`.

### O que o ESP32 guarda

Ao final de cada ciclo (normal ou por timeout), o firmware salva:
- `ultimaMetaML` — o que foi pedido (mL inteiros do `$ML:`)
- `ultimoDispensadoML` — o que realmente saiu (calculado dos pulsos)

Esses valores persistem em memoria enquanto o ESP32 estiver ligado.
Sao zerados apenas no modo `$LB:` (sem meta definida) ou ao ligar o dispositivo.

### Opcao A — Android gerencia o estado (recomendado como fallback)

O Android deve rastrear localmente:

```
pedidoML       = valor enviado no $ML:          (ex.: 300)
dispensadoML   = 0
```

A cada `ML:<valor>` recebido no `FN:`, acumular:
```
dispensadoML  += <valor>
restanteML     = pedidoML - dispensadoML
```

Se `restanteML > 1`:
- Exibir para o operador: "Faltam X mL — continuar?"
- Ao confirmar, enviar `$ML:<restanteML>` como novo ciclo normal
- Continuar acumulando `dispensadoML` ate `restanteML <= 1`

Esse estado deve sobreviver a reconexoes BLE. Se o app fechar, o pedido e perdido — cabe ao produto definir se isso e aceitavel.

### Opcao B — Comando `$RS:` (retomada via ESP32)

**ESP32 — implementado:**

`$RS:` calcula `restante = ultimaMetaML - ultimoDispensadoML` e enfileira automaticamente.

| Situacao | Resposta |
|----------|----------|
| Ha restante > 1 mL e fila livre | `RS:<restante_ml>` + ciclo inicia normalmente |
| Ciclo anterior ja estava completo | `RS:0` |
| Nenhum ciclo `$ML:` anterior ou modo `$LB:` | `ERRO` |
| Fila ocupada (outra operacao em andamento) | `ERRO` |

Apos o `$RS:` aceito, o fluxo e identico ao `$ML:` normal:
```
Android                      ESP32
   |                            |
   |--- $RS: ------------------>|
   |<-- RS:150 -----------------|   150 mL enfileirados
   |<-- IN: --------------------|   valvula abriu
   |<-- VP:0.000 ---------------|
   |<-- VP:80.500 --------------|
   |<-- QP:882 -----------------|
   |<-- ML:150 -----------------|
   |<-- FN: --------------------|
```

### Caso especial: desconexao durante o ciclo

Se o Android desconectar durante uma liberacao, a valvula fecha em ate 50 ms (Bloco 2). O ESP32 **nao envia** `ML:` nem `FN:` pois nao ha cliente conectado. Mas `ultimoDispensadoML` **e salvo** antes do envio BLE.

Ao reconectar:
- Android nao sabe quanto saiu (nao recebeu `ML:`)
- Enviar `$RS:` e a unica forma de retomar sem re-enviar o pedido completo
- O ESP32 responde `RS:<restante>` com base no que foi medido

**Android deve neste caso:**
1. Ao reconectar apos desconexao inesperada durante ciclo: tentar `$RS:` antes de qualquer outro comando
2. Se receber `RS:0`: pedido estava completo, nada a fazer
3. Se receber `RS:<n>`: apresentar ao operador e confirmar retomada
4. Se receber `ERRO`: nenhum ciclo anterior registrado (ESP32 reiniciou), solicitar novo pedido completo

### Resumo de decisao para o Android

```
ao receber FN: {
    se dispensadoML < pedidoML - 1:
        exibir opcao de retomar
        se operador confirmar:
            enviar $ML:<restante>   // Opcao A: Android calcula
            // OU
            enviar $RS:             // Opcao B: ESP32 calcula
}

ao reconectar apos desconexao inesperada durante ciclo {
    enviar $RS:
    se RS:<n>: apresentar retomada
    se RS:0 ou ERRO: solicitar novo pedido
}
```

---

## 10. Keepalive PING/PONG

**Status: opcional.**

Com o abort automatico ao desconectar implementado e o BLE supervision timeout de 5000 ms configurado em hardware, o PING/PONG nao e necessario para seguranca operacional.

Pode ser usado opcionalmente para:
- Confirmar que o firmware esta responsivo antes de enviar `$ML:`
- Verificar latencia da conexao

Se o app usar: enviar `PING` a cada 5s em estado idle e aguardar `PONG`.

---

## 11. Parametros que a API deve fornecer ao app

```json
{
  "machineId": "maq-001",
  "wifiMac": "DC:B4:D9:9A:67:1A",
  "bleName": "CHOPP_DCB4",
  "pairingPin": null,
  "serviceUuid": "6E400001-B5A3-F393-E0A9-E50E24DCCA9E",
  "rxUuid": "6E400002-B5A3-F393-E0A9-E50E24DCCA9E",
  "txUuid": "6E400003-B5A3-F393-E0A9-E50E24DCCA9E",
  "mtu": 247,
  "autoConnect": false
}
```

---

## 12. Estado de implementacao do firmware

| Funcionalidade | Status |
|----------------|--------|
| Nome BLE dinamico `CHOPP_XXXX` | implementado |
| Just Works sem PIN | implementado |
| Abort de valvula ao desconectar BLE | implementado |
| Comando `$ML:` | implementado |
| Comando `$LB:` | implementado |
| Comando `$PL:` set e get | implementado |
| Comando `$TO:` set e get | implementado |
| Comando `$DB:` diagnostico | implementado |
| PING / PONG | implementado |
| `IN:` e `FN:` | implementado |
| `VP:` durante fluxo | implementado |
| Comando `$RS:` resume de ciclo incompleto | implementado |
| MTU explicito no servidor | nao configurado — aceita proposta do cliente |
| Resposta `BUSY` para operacao em andamento | nao implementado — retorna `ERRO` |
