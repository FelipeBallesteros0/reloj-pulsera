# ANDIA — asistente de voz (diseño de la fase F3)

Fecha: 2026-09-03. Estado: borrador para revisión de Felipe.

## 1. Objetivo

Hablarle al reloj y que responda con la voz de Mayuri (Fish local, sin su personalidad): "Oye Andia, ¿qué clima hay
mañana?" → texto en pantalla y voz por el altavoz. ANDIA es un asistente **aparte** del bot de WhatsApp: prompt e
historial propios; solo reutiliza código (herramientas, recordatorios, STT, cliente Fish).

## 2. Decisiones ya tomadas

| Tema | Decisión |
| --- | --- |
| Nombre y sigla | **ANDIA** (asistente de voz) |
| Frases de activación | "Oye/Hola/Hey/Ey Andia" y "Andia" sola, al inicio de la frase |
| Dónde se decide la activación | **En la Pi, sobre la transcripción** (OpenAI `gpt-4o-transcribe` con pista de vocabulario). Cambio del 2026-09-03: Porcupine descartado (la cuenta de Picovoice de Felipe fue borrada y no se puede reactivar), Vosk descartado (no conoce "Andia"; lo transcribe como "hoy en día"), WakeNet del C6 solo trae palabras fijas en inglés/chino |
| Disparo en el reloj | Levantar la muñeca (IMU) abre una ventana de escucha de 6 s; respaldo: botón de micrófono en la carátula y PWR largo |
| Voz | Mayuri en el servidor Fish local del PC (4070). Sin PC: respuesta solo en texto (opcional después: voz de reserva sin GPU en la Pi) |
| STT | OpenAI `gpt-4o-transcribe` (como el bot); respaldo Whisper en el PC si está encendido |
| LLM | DeepSeek v4 pro con *thinking* desactivado, herramientas por *function calling* |
| Cerebro | Servicio `andia` en la Pi (coramohost, siempre encendida), Python 3.12, FastAPI + WebSocket |
| Transporte reloj ↔ Pi | WebSocket sobre WiFi. v1 en la red de casa (`ws://`); v2 fuera de casa vía hotspot del iPhone + túnel Cloudflare (`wss://`) |
| Audio | Subida: PCM 16 kHz, 16 bit, mono (32 KB/s). Bajada: PCM 16 kHz mono, remuestreado en la Pi |

## 3. Arquitectura

```
[Reloj ESP32-C6]  ──WiFi/WebSocket──▶  [Pi coramohost: servicio andia]  ──HTTP──▶  [PC: Fish TTS (Mayuri)]
  mic ES7210 → PCM                        Porcupine (activación)                     solo cuando está encendido
  altavoz ES8311 ← PCM                    VAD (fin de frase)
  IMU: levantar muñeca                    STT (OpenAI) → DeepSeek + herramientas
  UI: escuchando/pensando/hablando        historial propio (JSON en disco)
  notificaciones ANCS (ya existe)         cola de avisos hacia el reloj
```

Sin cambios en la parte BLE/ANCS: coexisten (NimBLE + WiFi en el C6; ver riesgos).

## 4. Flujo de una consulta

1. Reloj: gesto de muñeca (o botón) → enciende pantalla "Escuchando…", se asegura WiFi (ya asociado en ahorro de
   energía; si no, conecta), abre WebSocket, manda `{"t":"inicio","modo":"gesto"|"boton"}` y empieza a enviar audio en
   tramas binarias de 640 B (20 ms).
2. Pi: pasa las tramas por Porcupine. Modo `gesto`: si en 6 s no aparece una frase de activación → `{"t":"fin","motivo":"sin_activacion"}`
   y el reloj vuelve a la carátula sin mostrar nada. Modo `boton`: no exige activación.
3. Al activarse: `{"t":"activada"}` (el reloj muestra "Te escucho"); la Pi acumula audio hasta silencio de 800 ms (VAD)
   o 12 s máximo → `{"t":"pensando"}`.
4. STT → texto de la pregunta `{"t":"pregunta","texto":...}` (se muestra); DeepSeek con herramientas → respuesta.
5. `{"t":"respuesta","texto":...,"voz":true|false}`; si hay voz, tramas binarias de audio hasta `{"t":"fin"}`. El reloj
   reproduce mientras recibe (búfer de ~1 s) y muestra el texto; al terminar, 20 s y apaga la pantalla.
6. Si el PC está apagado: `voz:false`, solo texto. Si falla STT/LLM: `{"t":"error","texto":"..."}` legible.

## 5. Protocolo WebSocket (v1)

- Texto = JSON con campo `t`. Binario = audio 16 kHz mono: PCM s16le por defecto, o bloques **IMA ADPCM 4:1**
  (cabecera de 4 B: predictor int16 LE, índice de paso, 0; luego un nibble por muestra, el bajo primero; cada bloque se
  decodifica solo) si el `hola` trae `"audio":"adpcm"` y la Pi lo confirma en su `hola` (`"audio":"adpcm"|"pcm"`). Un
  mensaje `{"t":"hola","token":...,"reloj":"98A316A5745E","v":1,"audio":"adpcm"}` abre la sesión. *(Cambio del
  2026-09-04: por el túnel de Cloudflare el PCM crudo no cabía en las ventanas TCP del reloj y los registros TLS de 16 KB
  no cabían en su RAM; la Pi además espacia los trozos de respuesta a 2× tiempo real.)*
- Reloj → Pi: `hola`, `inicio`, audio, `cancelar` (toque en pantalla), `ping`.
- Pi → Reloj: `activada`, `pensando`, `pregunta`, `respuesta`, audio, `fin`, `error`, `aviso` (mensaje empujado por la
  Pi: recordatorio, "terminó la compilación", alerta), `pong`.
- Token compartido en `sdkconfig`/NVS y en la Pi (`.env`).

## 6. Servicio `andia` en la Pi

Repo nuevo `~/andia` (WSL) con `deploy.sh` a la Pi. Módulos:

- `servidor.py` — FastAPI: `/ws` (reloj), `/avisar` (POST desde PC/Pi: texto → cola del reloj), `/salud`.
- `activacion.py` — Porcupine (`pvporcupine`, AccessKey y `.ppn` de Picovoice Console) + VAD (`webrtcvad`).
- `stt.py`, `tts.py`, `herramientas.py`, `recordatorios.py` — copiados del bot y adaptados (sin voces múltiples, sin WhatsApp).
- `cerebro.py` — DeepSeek con prompt de ANDIA: español de Chile, respuestas cortas (≤ 2 frases para voz, texto ≤ 300
  caracteres), sin personalidad de Mayuri, hora/fecha de Chile en el prompt, historial propio en `datos/historial.json`
  (últimos N turnos, se reinicia con "olvida la conversación").
- Herramientas v1: hora/fecha, clima (Open-Meteo), búsqueda web + leer página, recordatorios (crear/listar/borrar; al
  cumplirse → `aviso` al reloj), temporizador (el reloj lo cuenta), estado del PC (ping) y **encender el PC** (Wake-on-LAN),
  estado del robot 4WD (ping a auto4wd.local).
- `pruebas/`: tests con WAV grabados (frase con activación, sin activación, silencio) y respuestas simuladas del LLM.

## 7. Firmware del reloj (cambios)

- Nuevo componente `reloj_voz`: captura I2S (BSP `bsp_audio_codec_microphone_init`, ES7210) en tarea propia → cola → WebSocket;
  reproducción (ES8311, `bsp_audio_codec_speaker_init`) con búfer anillo; control de PA (GPIO6) solo al hablar.
- Nuevo componente `reloj_red`: WiFi STA con lista de redes (casa, hotspot "Felipe " con espacio final), ahorro de energía
  (`WIFI_PS_MIN_MODEM`), cliente WebSocket (`esp_websocket_client`), reconexión con retroceso, `avisos` → notificaciones
  locales (misma UI que ANCS, app "ANDIA").
- `reloj_hw`: gesto de levantar la muñeca con el QMI8658 (lectura a 25 Hz en la tarea de la app: paso de "brazo abajo" a
  "pantalla hacia la cara" en < 600 ms; umbral ajustable en Kconfig). Interrupción INT1 en F4.
- `reloj_ui`: pantalla ANDIA (estado, pregunta, respuesta con desplazamiento, botón cancelar), botón de micrófono en la carátula,
  indicador WiFi en la barra superior, ajustes: WiFi (estado, red), ANDIA (activar/desactivar gesto).
- `sdkconfig`: `CONFIG_ESP_WIFI_ENABLED`, coexistencia BLE+WiFi (`CONFIG_ESP_COEX_SW_COEXIST_ENABLE`), búferes de WiFi
  reducidos (RX estáticos 4-6, TX dinámicos) para caber en RAM.

## 8. Manejo de errores

| Situación | Comportamiento |
| --- | --- |
| Sin WiFi (fuera de casa, sin hotspot) | Icono WiFi tachado; el gesto no hace nada; el botón muestra "Sin conexión con ANDIA" |
| Pi no responde / WebSocket cae | Reintento con retroceso 1-2-4-8-30 s; mensaje en pantalla si fue por acción del usuario |
| Sin activación en 6 s (modo gesto) | Silencio total: vuelve a la carátula |
| STT vacío / no entendió | "No te entendí" (texto y voz si hay) |
| PC apagado | Respuesta solo texto; la Pi lo indica con `voz:false`; ANDIA puede ofrecer "¿enciendo el PC?" |
| DeepSeek/OpenAI caídos | `error` con texto claro; nunca se queda en "pensando" (timeout 20 s) |
| Batería < 15 % | El gesto se desactiva; solo botón |

## 9. Consumo (estimaciones a medir)

- BLE + pantalla apagada (hoy): base. WiFi asociado en `MIN_MODEM`: +5-10 mA medio. Ventana de escucha (WiFi activo +
  I2S): ~120 mA durante ≤ 20 s por consulta. Objetivo: que una jornada con 20 consultas quite < 15 % de batería respecto
  de hoy. Se mide con el `latido` (batería %) antes/después.

## 10. Pruebas

- Host (gcc): empaquetado/desempaquetado de mensajes JSON del reloj, búfer anillo de audio, detector de gesto con trazas
  de IMU grabadas (CSV).
- Pi (pytest): Porcupine con WAV de las 5 frases y de frases parecidas ("mañana", "Ana"); VAD; flujo completo con LLM
  simulado; `/avisar`.
- Integración: PC como reloj falso (script Python con micrófono) contra la Pi; luego el reloj real: 10 consultas seguidas,
  PC apagado, WiFi caída a mitad de respuesta.

## 11. Fases de construcción

1. **F3a Pi**: servicio `andia` completo, probado con el PC como cliente (sin tocar el reloj). Requiere: cuenta Picovoice
   + AccessKey + 5 `.ppn`; claves DeepSeek/OpenAI en `.env` de la Pi; Fish accesible desde la Pi.
2. **F3b Reloj audio**: WiFi + WebSocket + captura/reproducción, activado por botón. Primera conversación real.
3. **F3c Gesto y energía**: levantar la muñeca, WiFi en ahorro, medición de batería.
4. **F3d Avisos y herramientas**: `/avisar` desde WSL (`andia "texto"`), recordatorios al reloj, WoL del PC, estado del robot.
5. **F3e Fuera de casa**: `wss://` por Cloudflare + hotspot (RAM de TLS a medir).

## 12. Riesgos

- **RAM del C6**: WiFi (~50 KB) + WebSocket + búferes de audio (~20 KB) sobre los 165 KB libres actuales. Mitigación:
  búferes WiFi mínimos, audio en PCM sin códec, medir en F3b antes de seguir; TLS (F3e) solo si sobra.
- **Coexistencia BLE + WiFi** en un solo núcleo: posibles cortes de BLE durante el streaming; aceptable (ANCS se recupera).
- **Falsos disparos del gesto**: se calibra con umbrales; el nombre en la Pi filtra el resto.
- **Picovoice**: plan gratuito para uso personal con límites (revisar en la consola: dispositivos activos, vigencia de los
  modelos personalizados).
- **Latencia**: objetivo < 4 s desde fin de frase a inicio de voz (STT ~1 s, LLM ~1-2 s, TTS ~1 s).
