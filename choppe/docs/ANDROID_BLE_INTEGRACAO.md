# Integracao Android <-> ESP32 Chopp

> **ATENÇÃO - Mudança de segurança (24/04/2026):** O firmware foi atualizado para usar o modo **"Just Works"** (sem PIN). O pareamento agora é automático e não requer entrada de PIN pelo usuário.

Este documento resume exatamente o que o app Android precisa usar para conectar, parear e operar a chopeira via BLE NUS.

## Identificacao da placa

- Nome BLE: `CHOPP_XXXX`
- Regra do nome: `CHOPP_` + 4 primeiros caracteres HEX do `ESP.getEfuseMac()`
- Exemplo: `CHOPP_DCB4`
- Validacao recomendada no app:
  - comparar o `wifiMac` vindo da API
  - derivar o nome esperado `CHOPP_XXXX`
  - durante o scan, aceitar apenas o nome esperado
  - se a API tambem possuir o MAC BLE, validar o endereco BLE encontrado no scan

## Parametros fixos BLE

- Service UUID: `6E400001-B5A3-F393-E0A9-E50E24DCCA9E`
- RX Characteristic UUID: `6E400002-B5A3-F393-E0A9-E50E24DCCA9E`
- TX Characteristic UUID: `6E400003-B5A3-F393-E0A9-E50E24DCCA9E`
- ~~PIN de pareamento: `259087`~~ (removido - modo Just Works)
- MTU solicitado: `247`
- `autoConnect`: `false`
- `transport`: `TRANSPORT_LE`

## Requisitos do firmware

- ~~A characteristic RX exige acesso criptografado~~
- ~~A TX e o descriptor `0x2902` de notificacao exigem acesso criptografado~~
- **NOVO**: A characteristic RX permite acesso sem criptografia
- A TX e o descriptor `0x2902` de notificacao permitem acesso sem criptografia
- Sem necessidade de PIN para pareamento

## Fluxo recomendado no Android

1. Fazer scan BLE e localizar apenas o dispositivo com o nome esperado `CHOPP_XXXX`
2. Chamar `connectGatt(context, false, callback, BluetoothDevice.TRANSPORT_LE)`
3. Em `onConnectionStateChange(CONNECTED)`:
   - O pareamento agora é automático (modo Just Works)
   - Não é mais necessário chamar `createBond()` nem configurar PIN
   - Pode prosseguir diretamente para descoberta de serviços
4. ~~No `BroadcastReceiver` de pareamento:~~
   - ~~interceptar `ACTION_PAIRING_REQUEST`~~
   - ~~usar `device.setPin("259087".getBytes())`~~
   - ~~chamar `abortBroadcast()` se a estrategia do app ja usar isso~~
5. ~~Aguardar `BOND_BONDED`~~ (não é mais necessário)
6. Chamar `requestMtu(247)`
7. Em `onMtuChanged`, chamar `discoverServices()`
8. Em `onServicesDiscovered`, localizar o servico NUS e as characteristics RX/TX
9. Habilitar notificacoes na TX:
   - `setCharacteristicNotification(tx, true)`
   - escrever `ENABLE_NOTIFICATION_VALUE` no descriptor `0x2902`
10. Somente apos o `onDescriptorWrite` considerar o estado como `READY`
11. Em `READY`, iniciar `PING` a cada 5 segundos
12. Ao receber `PONG`, manter a sessao como valida
13. Para liberar volume, escrever na RX: `"$ML:300"` por exemplo

## Comandos Android -> ESP32

- `PING`
- `$ML:<ml>`
- `$LB:`
- `$PL:<pulsos>`
- `$TO:<ms>`

## Respostas ESP32 -> Android

- `PONG`
- `OK`
- `ERRO`
- `VP:<ml>`
- `QP:<pulsos>`
- `ML:<ml>`

## Fluxo de liberacao de volume

Para liberar `300 mL`, o app deve:

1. Garantir que o estado BLE esta `READY`
2. Escrever na RX: `"$ML:300"`
3. Aguardar `OK`
4. Processar notificacoes `VP:<ml>` durante a liberacao
5. Considerar concluido apenas quando receber `ML:300` ou `ML:<valorFinal>`

Sequencia esperada:

```text
PING
PONG
$ML:300
OK
VP:50
VP:150
QP:1764
ML:300
```

## Parametros que a API deve fornecer ao app

Campos recomendados no JSON:

```json
{
  "machineId": "maq-001",
  "wifiMac": "DC:B4:D9:99:B8:E2",
  "bleName": "CHOPP_DCB4",
  "pairingPin": null,
  "serviceUuid": "6E400001-B5A3-F393-E0A9-E50E24DCCA9E",
  "rxUuid": "6E400002-B5A3-F393-E0A9-E50E24DCCA9E",
  "txUuid": "6E400003-B5A3-F393-E0A9-E50E24DCCA9E",
  "mtu": 247,
  "autoConnect": false
}
```

## Observacoes importantes

- O comando de liberacao usa `:` e nao `,`
- Exemplo correto: `"$ML:300"`
- ~~O app nao deve enviar `$ML` antes de:~~
  - ~~`BOND_BONDED`~~
  - ~~`MTU` negociado~~
  - ~~servicos descobertos~~
  - ~~notificacoes habilitadas~~
- **NOVO**: O app pode enviar comandos após:
  - Conexão estabelecida
  - MTU negociado
  - Serviços descobertos
  - Notificações habilitadas
- O app deve tratar desconexao limpando o estado `READY` e reiniciando o fluxo completo
- O pareamento agora é automático (modo Just Works) - não requer entrada de PIN
