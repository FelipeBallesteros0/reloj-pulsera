# ANDIA F3a — Servicio de voz en la Pi: plan de implementación

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Un servicio `andia` en la Raspberry Pi (coramohost) que recibe audio por WebSocket, detecta "Oye Andia" (y variantes) con Porcupine, transcribe con OpenAI, responde con DeepSeek y herramientas, y devuelve texto más la voz de Mayuri (Fish local del PC) como PCM 16 kHz. Se prueba de punta a punta con el PC como "reloj falso" antes de tocar el firmware.

**Architecture:** Repo nuevo `~/andia` (WSL) desplegado por `deploy.sh` (rsync + venv + systemd) a la Pi, puerto 8001. Módulos puros y testeables (`audio`, `activacion`, `sesion`, `cerebro`) separados del cableado real (`servidor.py`). La máquina de estados de una conversación (`sesion.Sesion`) no hace red ni I/O: recibe bytes y devuelve mensajes; el servidor los envía y ejecuta la tubería STT→LLM→TTS en un hilo.

**Tech Stack:** Python 3.12 en la Pi (aarch64), FastAPI + uvicorn (WebSocket), `pvporcupine` (Porcupine, modelo español), `webrtcvad-wheels` (VAD), `openai` (STT `gpt-4o-transcribe` y DeepSeek por API compatible), `requests` + `ormsgpack` (Fish TTS), `ffmpeg` (remuestreo), `ddgs` + `trafilatura` (búsqueda/lectura), `pytest` + `httpx`. En el PC: `sounddevice` + `websockets` para el reloj falso.

**Spec:** `docs/superpowers/specs/2026-09-03-andia-diseno.md` (este plan cubre la sección 6 "Servicio andia en la Pi", el protocolo de la sección 5 y la fase F3a de la sección 11).

## Global Constraints

- Idioma del código y los mensajes: español, identificadores en español como en `Desktop\bot_whatsapp` (de donde se copian `stt.py`, `tts.py`, `texto.py`, `herramientas.py`, `recordatorios.py`).
- ANDIA es un asistente **aparte** del bot de WhatsApp: prompt e historial propios (`datos/historial.json`), sin voces múltiples ni WhatsApp.
- Audio en ambos sentidos: **PCM 16 000 Hz, 16 bit, mono, little-endian**. Porcupine consume tramas de `frame_length` muestras (512); el VAD, tramas de 30 ms (480 muestras). El servidor reagrupa: el cliente manda trozos de cualquier tamaño.
- Protocolo WebSocket (sección 5 de la spec): texto = JSON con campo `t`; binario = PCM. Cliente→Pi: `hola`, `inicio`, audio, `cancelar`, `ping`. Pi→cliente: `activada`, `pensando`, `pregunta`, `respuesta`, audio, `fin`, `error`, `aviso`, `temporizador`, `pong`.
- Ventana de activación en modo `gesto`: 6 s. Fin de frase: 800 ms de silencio tras voz, o 12 s máximo. Sin voz del PC: `respuesta` con `voz:false`.
- Respuestas de ANDIA: español de Chile, ≤ 2 frases y ≤ 280 caracteres, sin markdown ni emojis (se leen en pantalla chica y en voz alta).
- Claves y secretos solo en `.env` (nunca en git ni en la consola). Puerto del servicio: **8001**. Token compartido `ANDIA_TOKEN`.
- Nunca imprimir el contenido de `.env` del bot ni de la Pi al copiar claves.

---

## Estructura de archivos

```
~/andia/
  README.md                 qué es, cómo correr, cómo desplegar, cómo probar
  requirements.txt          dependencias de la Pi
  requirements-pc.txt       dependencias del reloj falso en Windows (sounddevice, websockets)
  .env.example              variables sin valores
  .gitignore                .env, venv/, datos/, palabras/*.ppn, __pycache__
  andia.service             unidad systemd (puerto 8001, usuario felipe)
  deploy.sh                 rsync a la Pi + venv + pip + systemd
  cliente_pc.py             reloj falso: micrófono/altavoz del PC por WebSocket
  tools/exponer_fish.ps1    portproxy + firewall en Windows para que la Pi vea el Fish de WSL
  andia/__init__.py
  andia/audio.py            pcm_a_wav, wav_a_pcm16k (ffmpeg), Trozador
  andia/activacion.py       Detector (Porcupine), crear_porcupine, FinDeFrase (VAD con histéresis)
  andia/texto.py            copia: partir_frases, etiquetar_para_tts, limpiar_para_voz
  andia/stt.py              transcribir_openai (copia) + transcribir_pcm
  andia/tts.py              Fish: construir_peticion, servidor_vivo, sintetizar_pcm16k
  andia/recordatorios.py    copia tal cual del bot
  andia/herramientas.py     copia + hora, encender_pc (WoL), estado_pc, estado_robot, temporizador
  andia/cerebro.py          Cerebro: prompt de ANDIA, historial JSON, bucle_herramientas, responder()
  andia/sesion.py           Sesion: máquina de estados de una conversación (pura)
  andia/servidor.py         FastAPI: /ws, /avisar, /salud; cola de avisos; vigilante de recordatorios; cableado real
  pruebas/test_audio.py
  pruebas/test_activacion.py
  pruebas/test_stt.py
  pruebas/test_tts.py
  pruebas/test_herramientas.py
  pruebas/test_cerebro.py
  pruebas/test_sesion.py
  pruebas/test_servidor.py
  pruebas/integracion/README.md    cómo grabar WAV de prueba y correr Porcupine real (requiere clave y .ppn)
  pruebas/integracion/test_porcupine_real.py
  palabras/                 .ppn de Picovoice Console (fuera de git) + porcupine_params_es.pv si hace falta
  datos/                    historial.json, recordatorios.json, avisos.json (fuera de git)
```

---

### Task 1: Esqueleto del repo y utilidades de audio

**Files:**
- Create: `~/andia/README.md`, `~/andia/requirements.txt`, `~/andia/.env.example`, `~/andia/.gitignore`, `~/andia/andia/__init__.py`, `~/andia/andia/audio.py`
- Test: `~/andia/pruebas/test_audio.py`

**Interfaces:**
- Produces: `audio.TASA = 16000`; `audio.pcm_a_wav(pcm: bytes, tasa: int = 16000) -> bytes`; `audio.wav_a_pcm16k(wav: bytes) -> bytes`; `class audio.Trozador(n_muestras: int)` con `alimentar(datos: bytes) -> list[bytes]` (trozos de exactamente `n_muestras*2` bytes) y `vaciar()`.

- [ ] **Step 1: Crear el repo y el entorno**

```bash
mkdir -p ~/andia/andia ~/andia/pruebas/integracion ~/andia/palabras ~/andia/datos ~/andia/tools
cd ~/andia && git init -q -b main
python3 -m venv venv && . venv/bin/activate && pip install --quiet --upgrade pip
```

- [ ] **Step 2: Escribir requirements y archivos base**

`~/andia/requirements.txt`:
```
fastapi>=0.115
uvicorn[standard]>=0.30
websockets>=12
openai>=1.40
requests>=2.32
ormsgpack>=1.5
python-dotenv>=1.0
pvporcupine>=3.0
webrtcvad-wheels>=2.0.14
ddgs>=9.0
trafilatura>=1.12
pytest>=8
httpx>=0.27
```

`~/andia/.env.example`:
```
# Claves (no compartir)
DEEPSEEK_API_KEY=
OPENAI_API_KEY=
PICOVOICE_ACCESS_KEY=
# Token que el reloj/cliente manda en el mensaje "hola"
ANDIA_TOKEN=cambia-esto
# Servidor Fish del PC (WSL expuesto con tools/exponer_fish.ps1)
TTS_URL=http://192.168.x.x:8080
TTS_VOZ=mayuri
# LLM
LLM_MODEL=deepseek-v4-pro
# PC de Felipe (Wake-on-LAN y estado)
PC_IP=192.168.x.x
PC_MAC=
# Robot
ROBOT_HOST=auto4wd.local
# Puerto del servicio
ANDIA_PUERTO=8001
```

`~/andia/.gitignore`:
```
.env
venv/
datos/
palabras/*.ppn
__pycache__/
.pytest_cache/
```

`~/andia/andia/__init__.py`: vacío.

`~/andia/pytest.ini` (sin esto pytest no encuentra el paquete `andia` desde `pruebas/`):
```
[pytest]
pythonpath = .
testpaths = pruebas
```

`~/andia/README.md` (primera versión; se completa en la Task 12):
```markdown
# ANDIA — asistente de voz

Servicio de voz para el reloj (y para cualquier cliente WebSocket): Porcupine detecta "Oye Andia",
OpenAI transcribe, DeepSeek responde con herramientas y Fish (Mayuri, en el PC) pone la voz.
Diseño: `~/reloj_pulsera/docs/superpowers/specs/2026-09-03-andia-diseno.md`.

Desarrollo en WSL: `. venv/bin/activate && pytest -q`. Producción: la Pi coramohost, `./deploy.sh`.
```

- [ ] **Step 3: Escribir el test de audio (falla)**

`~/andia/pruebas/test_audio.py`:
```python
import io
import wave

from andia import audio


def test_pcm_a_wav_tiene_cabecera_correcta():
    pcm = b"\x00\x01" * 1600            # 100 ms a 16 kHz
    wav = audio.pcm_a_wav(pcm)
    with wave.open(io.BytesIO(wav), "rb") as w:
        assert w.getnchannels() == 1
        assert w.getsampwidth() == 2
        assert w.getframerate() == 16000
        assert w.getnframes() == 1600
        assert w.readframes(1600) == pcm


def test_trozador_reagrupa_en_tramas_exactas():
    t = audio.Trozador(4)                # 4 muestras = 8 bytes
    assert t.alimentar(b"\x00" * 5) == []
    trozos = t.alimentar(b"\x00" * 12)   # 17 bytes acumulados → 2 trozos de 8, sobra 1
    assert [len(x) for x in trozos] == [8, 8]
    assert t.alimentar(b"\x00" * 7) == [b"\x00" * 8]
    t.vaciar()
    assert t.alimentar(b"\x00" * 7) == []


def test_wav_a_pcm16k_remuestrea_con_ffmpeg():
    # 44,1 kHz estéreo de 10 ms → 16 kHz mono: 160 muestras = 320 bytes (±1 muestra por redondeo)
    b = io.BytesIO()
    with wave.open(b, "wb") as w:
        w.setnchannels(2); w.setsampwidth(2); w.setframerate(44100)
        w.writeframes(b"\x00\x00" * 2 * 441)
    pcm = audio.wav_a_pcm16k(b.getvalue())
    assert 316 <= len(pcm) <= 324 and len(pcm) % 2 == 0
```

- [ ] **Step 4: Correr el test y ver que falla**

Run: `cd ~/andia && . venv/bin/activate && pip install -q -r requirements.txt && pytest -q pruebas/test_audio.py`
Expected: FAIL con `ModuleNotFoundError` o `AttributeError` (no existe `andia.audio`).

- [ ] **Step 5: Implementar `audio.py`**

`~/andia/andia/audio.py`:
```python
"""Utilidades de audio: PCM 16 kHz mono s16le ↔ WAV, remuestreo con ffmpeg y reagrupado en tramas."""
import io
import subprocess
import wave

TASA = 16000


def pcm_a_wav(pcm: bytes, tasa: int = TASA) -> bytes:
    """PCM s16le mono → WAV en memoria (OpenAI necesita un archivo con cabecera)."""
    b = io.BytesIO()
    with wave.open(b, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(tasa)
        w.writeframes(pcm)
    return b.getvalue()


def wav_a_pcm16k(wav: bytes) -> bytes:
    """Cualquier WAV (lo que entrega Fish) → PCM 16 kHz mono s16le, normalizado a -16 LUFS."""
    r = subprocess.run(
        ["ffmpeg", "-v", "error", "-i", "pipe:0", "-af", "loudnorm=I=-16:TP=-1.5:LRA=11",
         "-ar", str(TASA), "-ac", "1", "-f", "s16le", "pipe:1"],
        input=wav, capture_output=True, check=True,
    )
    return r.stdout


class Trozador:
    """Reagrupa bytes en trozos de exactamente n_muestras (2 bytes por muestra)."""

    def __init__(self, n_muestras: int):
        self.n_bytes = n_muestras * 2
        self.buf = b""

    def alimentar(self, datos: bytes) -> list[bytes]:
        self.buf += datos
        trozos = []
        while len(self.buf) >= self.n_bytes:
            trozos.append(self.buf[:self.n_bytes])
            self.buf = self.buf[self.n_bytes:]
        return trozos

    def vaciar(self) -> None:
        self.buf = b""
```

- [ ] **Step 6: Correr el test y ver que pasa**

Run: `pytest -q pruebas/test_audio.py`
Expected: `3 passed`. (Si `ffmpeg` no está en WSL: `sudo apt install ffmpeg`, lo corre Felipe con `!`.)

- [ ] **Step 7: Commit**

```bash
cd ~/andia && git add -A && git commit -q -m "Esqueleto de ANDIA y utilidades de audio (PCM/WAV/trozador)"
```

---

### Task 2: Activación (Porcupine) y fin de frase (VAD)

**Files:**
- Create: `~/andia/andia/activacion.py`
- Test: `~/andia/pruebas/test_activacion.py`

**Interfaces:**
- Consumes: `audio.Trozador`.
- Produces: `class activacion.Detector(motor)` donde `motor` tiene `.frame_length: int` y `.process(muestras: Sequence[int]) -> int` (≥0 = índice de la frase, −1 = nada); `Detector.alimentar(pcm: bytes) -> int` y `Detector.reiniciar()`. `activacion.crear_porcupine(clave: str, rutas_ppn: list[str], sensibilidades: list[float], modelo_es: str | None) -> motor`. `class activacion.FinDeFrase(vad, silencio_ms=800, maximo_ms=12000, trama_ms=30)` con `alimentar(pcm: bytes) -> bool` (True = la frase terminó), `reiniciar()`, atributos `hubo_voz: bool`, `total_ms: int`; `vad` tiene `.is_speech(trama: bytes, tasa: int) -> bool`. `activacion.crear_vad(agresividad: int = 2)`.

- [ ] **Step 1: Escribir los tests (fallan)**

`~/andia/pruebas/test_activacion.py`:
```python
from andia import activacion


class MotorFalso:
    """Imita a Porcupine: detecta cuando la primera muestra de la trama vale 7."""
    frame_length = 4

    def __init__(self):
        self.tramas = []

    def process(self, muestras):
        self.tramas.append(list(muestras))
        return 2 if muestras[0] == 7 else -1


def trama(valor, n=4):
    return (valor).to_bytes(2, "little", signed=True) * n


def test_detector_devuelve_indice_de_la_frase_y_reagrupa():
    m = MotorFalso()
    d = activacion.Detector(m)
    assert d.alimentar(trama(0)[:6]) == -1          # 3 muestras: no completa la trama
    assert d.alimentar(trama(0)[:2] + trama(7)) == 2 # completa la trama 0 y luego la trama 7
    assert len(m.tramas) == 2 and m.tramas[1][0] == 7


def test_detector_reiniciar_descarta_restos():
    d = activacion.Detector(MotorFalso())
    d.alimentar(b"\x07\x00" * 3)
    d.reiniciar()
    assert d.alimentar(b"\x00\x00") == -1


class VadFalso:
    """Voz si la trama contiene algún byte distinto de cero."""
    def is_speech(self, trama, tasa):
        assert tasa == 16000 and len(trama) == 960   # 30 ms a 16 kHz, 2 bytes por muestra
        return any(trama)


VOZ = b"\x01\x00" * 480
SILENCIO = b"\x00\x00" * 480


def test_fin_de_frase_termina_tras_800ms_de_silencio_despues_de_voz():
    f = activacion.FinDeFrase(VadFalso(), silencio_ms=800, maximo_ms=12000)
    assert not f.alimentar(SILENCIO * 40)             # 1,2 s de silencio inicial: no hubo voz aún
    assert not f.alimentar(VOZ * 10)                  # 300 ms de voz
    assert f.hubo_voz
    assert not f.alimentar(SILENCIO * 26)             # 780 ms: todavía no
    assert f.alimentar(SILENCIO)                      # 810 ms: terminó


def test_fin_de_frase_tope_maximo_aunque_siga_hablando():
    f = activacion.FinDeFrase(VadFalso(), silencio_ms=800, maximo_ms=3000)
    assert not f.alimentar(VOZ * 99)                  # 2,97 s
    assert f.alimentar(VOZ)                           # 3,0 s → tope
    assert f.total_ms == 3000


def test_fin_de_frase_reiniciar():
    f = activacion.FinDeFrase(VadFalso())
    f.alimentar(VOZ * 5)
    f.reiniciar()
    assert not f.hubo_voz and f.total_ms == 0
```

- [ ] **Step 2: Correr y ver que falla**

Run: `pytest -q pruebas/test_activacion.py`
Expected: FAIL (`ModuleNotFoundError: andia.activacion`).

- [ ] **Step 3: Implementar `activacion.py`**

`~/andia/andia/activacion.py`:
```python
"""Palabra de activación (Porcupine) y detector de fin de frase (VAD con histéresis).

Ambas clases reciben el motor real por inyección para poder probarlas sin claves ni micrófono.
"""
import struct

from .audio import TASA, Trozador


class Detector:
    """Alimenta a Porcupine con tramas de `motor.frame_length` muestras; devuelve el índice detectado o -1."""

    def __init__(self, motor):
        self.motor = motor
        self.trozador = Trozador(motor.frame_length)

    def alimentar(self, pcm: bytes) -> int:
        for trama in self.trozador.alimentar(pcm):
            muestras = struct.unpack_from(f"<{len(trama) // 2}h", trama)
            i = self.motor.process(muestras)
            if i >= 0:
                self.trozador.vaciar()
                return i
        return -1

    def reiniciar(self) -> None:
        self.trozador.vaciar()


def crear_porcupine(clave: str, rutas_ppn: list[str], sensibilidades: list[float], modelo_es: str | None):
    """Motor real. `modelo_es` es la ruta a porcupine_params_es.pv (None = inglés, no sirve para 'Oye Andia')."""
    import pvporcupine
    return pvporcupine.create(access_key=clave, keyword_paths=rutas_ppn, sensitivities=sensibilidades,
                              model_path=modelo_es)


def crear_vad(agresividad: int = 2):
    import webrtcvad
    return webrtcvad.Vad(agresividad)


class FinDeFrase:
    """Termina cuando hubo voz y después `silencio_ms` seguidos sin voz, o al llegar a `maximo_ms`."""

    def __init__(self, vad, silencio_ms: int = 800, maximo_ms: int = 12000, trama_ms: int = 30):
        self.vad = vad
        self.silencio_ms = silencio_ms
        self.maximo_ms = maximo_ms
        self.trama_ms = trama_ms
        self.trozador = Trozador(TASA * trama_ms // 1000)
        self.reiniciar()

    def reiniciar(self) -> None:
        self.hubo_voz = False
        self.silencio = 0
        self.total_ms = 0
        self.trozador.vaciar()

    def alimentar(self, pcm: bytes) -> bool:
        for trama in self.trozador.alimentar(pcm):
            self.total_ms += self.trama_ms
            if self.vad.is_speech(trama, TASA):
                self.hubo_voz = True
                self.silencio = 0
            else:
                self.silencio += self.trama_ms
            if (self.hubo_voz and self.silencio >= self.silencio_ms) or self.total_ms >= self.maximo_ms:
                return True
        return False
```

- [ ] **Step 4: Correr y ver que pasa**

Run: `pytest -q pruebas/test_activacion.py`
Expected: `5 passed`.

- [ ] **Step 5: Comprobar que Porcupine y el VAD reales se importan en WSL** (sin clave todavía)

Run: `python -c "import pvporcupine, webrtcvad, os; print(pvporcupine.LIBRARY_PATH); import glob; print(glob.glob(os.path.join(os.path.dirname(pvporcupine.__file__), 'lib', 'common', '*es*.pv')))"`
Expected: el paquete pip solo trae `porcupine_params.pv` (inglés). Descargar el modelo en español a `~/andia/palabras/`
(el servidor lo busca ahí primero): `curl -sSL -o ~/andia/palabras/porcupine_params_es.pv https://github.com/Picovoice/porcupine/raw/master/lib/common/porcupine_params_es.pv`.

- [ ] **Step 6: Commit**

```bash
git add -A && git commit -q -m "Activación con Porcupine y fin de frase con VAD, probados con motores falsos"
```

---

### Task 3: STT (OpenAI) desde PCM

**Files:**
- Create: `~/andia/andia/stt.py`
- Test: `~/andia/pruebas/test_stt.py`

**Interfaces:**
- Consumes: `audio.pcm_a_wav`.
- Produces: `stt.transcribir_openai(wav: bytes, clave: str, modelo: str = "gpt-4o-transcribe") -> str`; `stt.transcribir_pcm(pcm: bytes, clave: str, cliente=None) -> str` (`cliente` inyectable para tests; devuelve `""` si el audio dura menos de 300 ms).

- [ ] **Step 1: Test (falla)**

`~/andia/pruebas/test_stt.py`:
```python
import io
import wave

from andia import stt


class ClienteFalso:
    def __init__(self):
        self.recibido = None

        class _Tr:
            def create(_self, model, file, language):
                self.recibido = (model, file.name, file.read(), language)
                class R: text = "  hola andia  "
                return R()

        class _Audio:
            transcriptions = _Tr()
        self.audio = _Audio()


def test_transcribir_pcm_envuelve_en_wav_y_limpia():
    c = ClienteFalso()
    pcm = b"\x00\x00" * 16000          # 1 s
    assert stt.transcribir_pcm(pcm, "clave", cliente=c) == "hola andia"
    modelo, nombre, wav, idioma = c.recibido
    assert modelo == "gpt-4o-transcribe" and nombre.endswith(".wav") and idioma == "es"
    with wave.open(io.BytesIO(wav), "rb") as w:
        assert w.getframerate() == 16000 and w.getnframes() == 16000


def test_transcribir_pcm_ignora_audio_demasiado_corto():
    c = ClienteFalso()
    assert stt.transcribir_pcm(b"\x00\x00" * 1000, "clave", cliente=c) == ""   # 62 ms
    assert c.recibido is None
```

- [ ] **Step 2: Correr y ver que falla**

Run: `pytest -q pruebas/test_stt.py` → FAIL (`ModuleNotFoundError`).

- [ ] **Step 3: Implementar `stt.py`**

`~/andia/andia/stt.py`:
```python
"""Transcripción por la nube de OpenAI (gpt-4o-transcribe), a partir de PCM 16 kHz."""
import io

from .audio import TASA, pcm_a_wav

MODELO = "gpt-4o-transcribe"
MINIMO_MS = 300


def _cliente_real(clave: str):
    from openai import OpenAI
    return OpenAI(api_key=clave, timeout=20.0, max_retries=1)   # la spec: nunca quedarse en "pensando"


def transcribir_openai(wav: bytes, clave: str, modelo: str = MODELO, cliente=None) -> str:
    cliente = cliente or _cliente_real(clave)
    f = io.BytesIO(wav)
    f.name = "frase.wav"
    r = cliente.audio.transcriptions.create(model=modelo, file=f, language="es")
    return r.text.strip()


def transcribir_pcm(pcm: bytes, clave: str, cliente=None) -> str:
    """PCM 16 kHz mono → texto. Devuelve '' si es demasiado corto para contener una frase."""
    if len(pcm) < TASA * 2 * MINIMO_MS // 1000:
        return ""
    return transcribir_openai(pcm_a_wav(pcm), clave, cliente=cliente)
```

- [ ] **Step 4: Correr y ver que pasa** → `2 passed`.

- [ ] **Step 5: Commit**

```bash
git add -A && git commit -q -m "STT: transcripción OpenAI desde PCM con cliente inyectable"
```

---

### Task 4: Texto y TTS (Fish, voz Mayuri) a PCM 16 kHz

**Files:**
- Create: `~/andia/andia/texto.py`, `~/andia/andia/tts.py`
- Test: `~/andia/pruebas/test_tts.py`

**Interfaces:**
- Consumes: `audio.wav_a_pcm16k`.
- Produces: `texto.partir_frases(t) -> list[str]`, `texto.etiquetar_para_tts(t) -> str`, `texto.limpiar_para_voz(t) -> str` (copiadas del bot). `tts.construir_peticion(texto: str, voz: str) -> dict`; `tts.servidor_vivo(url: str, timeout: float = 3) -> bool`; `tts.sintetizar_pcm16k(texto: str, url: str, voz: str, post=None) -> bytes` (`post(url, data, headers, timeout) -> objeto con .content y .raise_for_status()` inyectable).

- [ ] **Step 1: Copiar `texto.py` del bot** (solo las tres funciones de texto; sin comandos ni teléfono)

`~/andia/andia/texto.py`:
```python
"""Utilidades de texto puras: troceo para el TTS y limpieza para voz."""
import re

_FIN_DE_FRASE = re.compile(r"(?<=[.!?…])\s+")
_MARKDOWN = re.compile(r"[*_#`>~]+|^\s*[-•]\s+", re.M)
_EMOJI = re.compile("[\U0001F000-\U0001FAFF\U00002600-\U000027BF\U0001F900-\U0001F9FF️]")


def partir_frases(texto: str) -> list[str]:
    """Parte por final de frase (. ! ? …) y descarta trozos vacíos."""
    return [f.strip() for f in _FIN_DE_FRASE.split(texto.strip()) if f.strip()]


def etiquetar_para_tts(texto: str) -> str:
    """Una etiqueta <|speaker:0|> por frase: sin ellas el motor Fish no lotea."""
    return " ".join("<|speaker:0|>" + f for f in partir_frases(texto))


def limpiar_para_voz(texto: str) -> str:
    """Quita markdown, viñetas y emojis: el TTS los leería en voz alta."""
    sin_md = _MARKDOWN.sub("", texto)
    sin_emoji = _EMOJI.sub("", sin_md)
    return " ".join(sin_emoji.split())
```

- [ ] **Step 2: Test de TTS (falla)**

`~/andia/pruebas/test_tts.py`:
```python
import io
import wave

import ormsgpack

from andia import texto, tts


def test_limpiar_para_voz_quita_markdown_y_emojis():
    assert texto.limpiar_para_voz("**Hola** 😀 - lista\n  ok") == "Hola lista ok"


def test_construir_peticion_usa_mayuri_y_etiquetas():
    p = tts.construir_peticion("Hola. ¿Cómo estás?", "mayuri")
    assert p["reference_id"] == "mayuri"
    assert p["text"] == "<|speaker:0|>Hola. <|speaker:0|>¿Cómo estás?"
    assert p["format"] == "wav" and p["streaming"] is False


def _wav_silencio(ms=100, tasa=44100):
    b = io.BytesIO()
    with wave.open(b, "wb") as w:
        w.setnchannels(1); w.setsampwidth(2); w.setframerate(tasa)
        w.writeframes(b"\x00\x00" * (tasa * ms // 1000))
    return b.getvalue()


def test_sintetizar_pcm16k_manda_msgpack_y_remuestrea():
    llamadas = {}

    class Resp:
        content = _wav_silencio()
        def raise_for_status(self): pass

    def post(url, data, headers, timeout):
        llamadas["url"] = url
        llamadas["peticion"] = ormsgpack.unpackb(data)
        llamadas["headers"] = headers
        return Resp()

    pcm = tts.sintetizar_pcm16k("Hola.", "http://pc:8080", "mayuri", post=post)
    assert llamadas["url"] == "http://pc:8080/v1/tts"
    assert llamadas["headers"]["content-type"] == "application/msgpack"
    assert llamadas["peticion"]["reference_id"] == "mayuri"
    assert 3180 <= len(pcm) <= 3220        # 100 ms a 16 kHz = 3200 bytes (±)
```

- [ ] **Step 3: Correr y ver que falla** → FAIL (`ModuleNotFoundError: andia.tts`).

- [ ] **Step 4: Implementar `tts.py`**

`~/andia/andia/tts.py`:
```python
"""Cliente del servidor Fish Audio del PC: texto → WAV (voz Mayuri) → PCM 16 kHz mono para el reloj."""
import ormsgpack
import requests

from .audio import wav_a_pcm16k
from .texto import etiquetar_para_tts

TIMEOUT = 120


def construir_peticion(texto: str, voz: str) -> dict:
    return {
        "text": etiquetar_para_tts(texto),
        "reference_id": voz,
        "streaming": False,
        "format": "wav",
        "chunk_length": 100,
        "temperature": 0.8,
        "normalize": False,          # el normalizador del servidor es para en/zh
        "use_memory_cache": "on",    # no recodificar la referencia cada vez
    }


def servidor_vivo(url: str, timeout: float = 3) -> bool:
    try:
        return requests.get(f"{url}/v1/health", timeout=timeout).ok
    except requests.RequestException:
        return False


def sintetizar_pcm16k(texto: str, url: str, voz: str, post=None) -> bytes:
    """Lanza excepción si el servidor falla; el llamador decide responder solo en texto."""
    post = post or requests.post
    r = post(f"{url}/v1/tts", data=ormsgpack.packb(construir_peticion(texto, voz)),
             headers={"content-type": "application/msgpack"}, timeout=TIMEOUT)
    r.raise_for_status()
    return wav_a_pcm16k(r.content)
```

- [ ] **Step 5: Correr y ver que pasa** → `3 passed`.

- [ ] **Step 6: Commit**

```bash
git add -A && git commit -q -m "TTS: Fish (Mayuri) a PCM 16 kHz con cliente inyectable; utilidades de texto"
```

---

### Task 5: Herramientas del asistente (copiadas + nuevas) y recordatorios

**Files:**
- Create: `~/andia/andia/recordatorios.py` (copia exacta de `/mnt/c/Users/Felipe/Desktop/bot_whatsapp/recordatorios.py`), `~/andia/andia/herramientas.py`
- Test: `~/andia/pruebas/test_herramientas.py`

**Interfaces:**
- Consumes: `recordatorios.Agenda`, `recordatorios.ESQUEMAS`, `recordatorios.FUNCIONES`, `recordatorios.usar(agenda)`.
- Produces: `herramientas.ESQUEMAS` (lista de esquemas *function calling*), `herramientas.ejecutar(nombre: str, args) -> str` (nunca lanza), `herramientas.encender_pc(mac: str = None, difusion: str = "192.168.x.x", enviar=None) -> str`, `herramientas.estado_pc(ip=None, url_tts=None) -> str`, `herramientas.estado_robot(host=None) -> str`, `herramientas.temporizador(minutos) -> str` y `herramientas.tomar_temporizador() -> int | None` (segundos pedidos en la última respuesta; se consume al leer).

- [ ] **Step 1: Copiar recordatorios y verificar que sus tests originales pasan aquí**

```bash
cp /mnt/c/Users/Felipe/Desktop/bot_whatsapp/recordatorios.py ~/andia/andia/recordatorios.py
cp /mnt/c/Users/Felipe/Desktop/bot_whatsapp/tests/test_recordatorios.py ~/andia/pruebas/test_recordatorios.py
sed -i 's/^import recordatorios$/from andia import recordatorios/; s/^from recordatorios import/from andia.recordatorios import/' ~/andia/pruebas/test_recordatorios.py
pytest -q pruebas/test_recordatorios.py
```
Expected: todos pasan (si algún import del test original difiere, ajustar solo la línea de import).

- [ ] **Step 2: Test de las herramientas nuevas (falla)**

`~/andia/pruebas/test_herramientas.py`:
```python
from pathlib import Path

from andia import herramientas, recordatorios


def test_esquemas_incluyen_las_nuevas_y_las_de_recordatorios():
    nombres = {e["function"]["name"] for e in herramientas.ESQUEMAS}
    assert {"clima", "buscar_web", "leer_pagina", "crear_recordatorio", "listar_recordatorios",
            "borrar_recordatorio", "encender_pc", "estado_pc", "estado_robot", "temporizador"} <= nombres


def test_encender_pc_manda_paquete_magico():
    enviados = []
    def enviar(paquete, destino):
        enviados.append((paquete, destino))
    r = herramientas.encender_pc(mac="AA:BB:CC:DD:EE:FF", difusion="192.168.x.x", enviar=enviar)
    paquete, destino = enviados[0]
    assert destino == ("192.168.x.x", 9)
    assert paquete == b"\xff" * 6 + bytes.fromhex("AABBCCDDEEFF") * 16
    assert "encendido" in r.lower() or "enviad" in r.lower()


def test_encender_pc_sin_mac_avisa():
    assert "PC_MAC" in herramientas.encender_pc(mac="", enviar=lambda p, d: None)


def test_temporizador_guarda_segundos_y_se_consume():
    r = herramientas.temporizador(2.5)
    assert "2 minutos y 30 segundos" in r or "150" in r
    assert herramientas.tomar_temporizador() == 150
    assert herramientas.tomar_temporizador() is None


def test_temporizador_rechaza_valores_raros():
    assert "positivo" in herramientas.temporizador(0)
    assert herramientas.tomar_temporizador() is None


def test_ejecutar_nunca_lanza(tmp_path):
    recordatorios.usar(recordatorios.Agenda(tmp_path / "r.json"))
    assert "no existe" in herramientas.ejecutar("nada", {})
    assert "falló" in herramientas.ejecutar("borrar_recordatorio", {"id": "x"})
    assert "Programado" in herramientas.ejecutar("crear_recordatorio", '{"mensaje": "pan", "en_minutos": 5}')
```

- [ ] **Step 3: Correr y ver que falla** → FAIL (`ModuleNotFoundError: andia.herramientas`).

- [ ] **Step 4: Escribir `herramientas.py`** (clima, buscar_web, leer_pagina copiados del bot; se añaden las nuevas)

`~/andia/andia/herramientas.py`:
```python
"""Herramientas que el LLM puede pedir. `ejecutar()` nunca lanza: devuelve el error como texto."""
import json
import os
import socket
import subprocess

import requests

from . import recordatorios as _rec

ESQUEMAS = [
    {"type": "function", "function": {
        "name": "clima",
        "description": "Pronóstico del tiempo para un lugar (ciudad, comuna o barrio), hoy o hasta 6 días adelante: "
                       "temperatura, máxima, mínima, cielo y probabilidad de lluvia.",
        "parameters": {"type": "object", "properties": {
            "lugar": {"type": "string", "description": "Nombre del lugar, ej. 'Ñuñoa' o 'Valparaíso'"},
            "dia": {"type": "string", "description": "'hoy' (por defecto), 'mañana', 'pasado mañana' o número de días adelante (0-6)"},
        }, "required": ["lugar"]}}},
    {"type": "function", "function": {
        "name": "buscar_web",
        "description": "Busca en internet información actual: noticias, precios, resultados, datos que puedan haber "
                       "cambiado. Devuelve títulos y resúmenes.",
        "parameters": {"type": "object", "properties": {
            "consulta": {"type": "string", "description": "Qué buscar, en pocas palabras"}}, "required": ["consulta"]}}},
    {"type": "function", "function": {
        "name": "leer_pagina",
        "description": "Descarga una página web y devuelve su texto legible. Úsala para leer un enlace de buscar_web.",
        "parameters": {"type": "object", "properties": {
            "url": {"type": "string", "description": "Dirección completa, con http:// o https://"}}, "required": ["url"]}}},
    {"type": "function", "function": {
        "name": "encender_pc",
        "description": "Enciende el computador de Felipe por Wake-on-LAN. Úsala si pide encender el PC o si necesita "
                       "la voz y el PC está apagado (avísale que tarda un minuto).",
        "parameters": {"type": "object", "properties": {}}}},
    {"type": "function", "function": {
        "name": "estado_pc",
        "description": "Dice si el PC de Felipe está encendido y si el servidor de voz responde.",
        "parameters": {"type": "object", "properties": {}}}},
    {"type": "function", "function": {
        "name": "estado_robot",
        "description": "Dice si el robot 4WD (auto4wd) está encendido y en la red.",
        "parameters": {"type": "object", "properties": {}}}},
    {"type": "function", "function": {
        "name": "temporizador",
        "description": "Pone un temporizador en el reloj que avisa cuando pasen los minutos indicados "
                       "(ej. 'avísame en 10 minutos' → 10). Para horas concretas usa crear_recordatorio.",
        "parameters": {"type": "object", "properties": {
            "minutos": {"type": "number", "description": "Duración en minutos (admite decimales)"}}, "required": ["minutos"]}}},
]

# ---- clima / web: copiados del bot de WhatsApp --------------------------------------------------

_CIELO = {
    0: "despejado", 1: "mayormente despejado", 2: "parcialmente nublado", 3: "nublado",
    45: "con niebla", 48: "con niebla", 51: "llovizna", 53: "llovizna", 55: "llovizna",
    61: "lluvia débil", 63: "lluvia", 65: "lluvia fuerte", 71: "nieve", 73: "nieve", 75: "nieve",
    80: "chubascos", 81: "chubascos", 82: "chubascos fuertes", 95: "tormenta", 96: "tormenta con granizo",
    99: "tormenta con granizo",
}
_NOMBRE_DIA = {0: "hoy", 1: "mañana", 2: "pasado mañana"}


def describir_cielo(codigo) -> str:
    return _CIELO.get(codigo, "sin datos")


def indice_dia(dia) -> int:
    if dia is None:
        return 0
    v = str(dia).strip().lower().replace("ñ", "n")
    if v in ("", "hoy"): return 0
    if v == "manana": return 1
    if v == "pasado manana": return 2
    try:
        return max(0, min(6, int(float(v))))
    except ValueError:
        return 0


def formatear_clima(d: dict) -> str:
    dia = d.get("dia", 0)
    cuando = _NOMBRE_DIA.get(dia, f"en {dia} días") + (f" ({d['fecha']})" if dia and d.get("fecha") else "")
    ahora = f"ahora {d['actual']:.0f}°, " if d.get("actual") is not None else ""
    return (f"Clima {cuando} en {d['lugar']}: {ahora}{describir_cielo(d['codigo'])}. "
            f"Máxima {d['max']:.0f}°, mínima {d['min']:.0f}°, probabilidad de lluvia {d['lluvia']:.0f}%.")


def clima(lugar: str, dia="hoy") -> str:
    g = requests.get("https://geocoding-api.open-meteo.com/v1/search",
                     params={"name": lugar, "count": 1, "language": "es"}, timeout=10).json()
    if not g.get("results"):
        return f"No encontré el lugar '{lugar}'."
    r = g["results"][0]
    nombre = ", ".join(x for x in (r.get("name"), r.get("admin1"), r.get("country")) if x)
    i = indice_dia(dia)
    f = requests.get("https://api.open-meteo.com/v1/forecast", params={
        "latitude": r["latitude"], "longitude": r["longitude"], "timezone": "auto",
        "current": "temperature_2m,weather_code",
        "daily": "temperature_2m_max,temperature_2m_min,precipitation_probability_max,weather_code",
        "forecast_days": 7,
    }, timeout=10).json()
    d = f["daily"]
    return formatear_clima({
        "lugar": nombre, "dia": i, "fecha": d["time"][i],
        "actual": f["current"]["temperature_2m"] if i == 0 else None,
        "codigo": f["current"]["weather_code"] if i == 0 else d["weather_code"][i],
        "max": d["temperature_2m_max"][i], "min": d["temperature_2m_min"][i],
        "lluvia": d["precipitation_probability_max"][i] or 0,
    })


def buscar_web(consulta: str) -> str:
    from ddgs import DDGS
    resultados = DDGS().text(consulta, region="cl-es", max_results=5)
    if not resultados:
        return f"Sin resultados para '{consulta}'."
    lineas = []
    for r in resultados:
        dominio = r.get("href", "").split("/")[2] if "//" in r.get("href", "") else ""
        lineas.append(f"- {r.get('title', '')} — {r.get('body', '')[:220]} ({dominio})")
    return "\n".join(lineas)


MAX_PAGINA = 6000
MAX_DESCARGA = 3_000_000


def descargar(url: str) -> str:
    r = requests.get(url, timeout=15, stream=True, headers={"User-Agent": "Mozilla/5.0 (ANDIA; lector de páginas)"})
    r.raise_for_status()
    cuerpo = r.raw.read(MAX_DESCARGA, decode_content=True)
    return cuerpo.decode(r.encoding or "utf-8", errors="replace")


def extraer_texto(html: str) -> str:
    import trafilatura
    texto = trafilatura.extract(html, include_comments=False, include_tables=True, favor_recall=True)
    if texto:
        return texto.strip()
    import re
    sin = re.sub(r"<(script|style)[^>]*>.*?</\1>", " ", html, flags=re.S | re.I)
    return " ".join(re.sub(r"<[^>]+>", " ", sin).split())


def leer_pagina(url: str, maximo: int = MAX_PAGINA) -> str:
    if not url.lower().startswith(("http://", "https://")):
        return f"La URL '{url}' no es válida: tiene que empezar por http:// o https://."
    texto = extraer_texto(descargar(url))
    if not texto:
        return f"La página {url} no tiene texto legible."
    return texto if len(texto) <= maximo else texto[:maximo].rsplit(" ", 1)[0] + "…"


# ---- nuevas: PC, robot, temporizador -------------------------------------------------------------

def _enviar_udp(paquete: bytes, destino: tuple[str, int]) -> None:
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as s:
        s.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
        s.sendto(paquete, destino)


def encender_pc(mac: str = None, difusion: str = "192.168.x.x", enviar=None) -> str:
    """Wake-on-LAN: 6 × 0xFF seguidos de la MAC 16 veces, por UDP al puerto 9 de la red."""
    mac = (mac if mac is not None else os.environ.get("PC_MAC", "")).strip()
    if not mac:
        return "No sé la dirección MAC del PC: falta PC_MAC en la configuración."
    limpia = mac.replace(":", "").replace("-", "")
    if len(limpia) != 12:
        return f"La MAC '{mac}' no es válida."
    paquete = b"\xff" * 6 + bytes.fromhex(limpia) * 16
    (enviar or _enviar_udp)(paquete, (difusion, 9))
    return "Paquete de encendido enviado al PC; tarda cerca de un minuto en estar listo."


def _responde_ping(host: str) -> bool:
    r = subprocess.run(["ping", "-c", "1", "-W", "1", host], capture_output=True)
    return r.returncode == 0


def estado_pc(ip: str = None, url_tts: str = None) -> str:
    ip = ip or os.environ.get("PC_IP", "192.168.x.x")
    url_tts = url_tts or os.environ.get("TTS_URL", "http://192.168.x.x:8080")
    if not _responde_ping(ip):
        return "El PC está apagado o fuera de la red."
    from .tts import servidor_vivo
    return "El PC está encendido y el servidor de voz responde." if servidor_vivo(url_tts) \
        else "El PC está encendido pero el servidor de voz (Fish) no responde."


def estado_robot(host: str = None) -> str:
    host = host or os.environ.get("ROBOT_HOST", "auto4wd.local")
    return "El robot está encendido y en la red." if _responde_ping(host) else "El robot no responde: está apagado o fuera de la red."


_temporizador_s: int | None = None


def temporizador(minutos) -> str:
    global _temporizador_s
    try:
        m = float(minutos)
    except (TypeError, ValueError):
        return "Los minutos tienen que ser un número positivo."
    if m <= 0 or m > 24 * 60:
        return "Los minutos tienen que ser un número positivo (hasta un día)."
    _temporizador_s = int(round(m * 60))
    mins, segs = divmod(_temporizador_s, 60)
    partes = [f"{mins} minuto{'s' if mins != 1 else ''}"] if mins else []
    if segs:
        partes.append(f"{segs} segundos")
    return f"Temporizador de {' y '.join(partes)} puesto en el reloj."


def tomar_temporizador() -> int | None:
    """Segundos pedidos en la última respuesta (o None). Se consume al leer."""
    global _temporizador_s
    s, _temporizador_s = _temporizador_s, None
    return s


ESQUEMAS += _rec.ESQUEMAS
_FUNCIONES = {"clima": clima, "buscar_web": buscar_web, "leer_pagina": leer_pagina,
              "encender_pc": encender_pc, "estado_pc": estado_pc, "estado_robot": estado_robot,
              "temporizador": temporizador, **_rec.FUNCIONES}


def ejecutar(nombre: str, args) -> str:
    """Despacha una llamada del modelo. `args` puede venir como dict o como JSON en texto."""
    if nombre not in _FUNCIONES:
        return f"La herramienta '{nombre}' no existe."
    try:
        if isinstance(args, str):
            args = json.loads(args) if args.strip() else {}
        return _FUNCIONES[nombre](**args)
    except Exception as e:
        return f"La herramienta '{nombre}' falló: {type(e).__name__}: {str(e)[:120]}"
```

- [ ] **Step 5: Correr y ver que pasa**

Run: `pytest -q pruebas/test_herramientas.py pruebas/test_recordatorios.py`
Expected: todos `passed`.

- [ ] **Step 6: Commit**

```bash
git add -A && git commit -q -m "Herramientas: clima/web/recordatorios del bot + encender PC (WoL), estado PC/robot, temporizador"
```

---

### Task 6: Cerebro (DeepSeek) con prompt e historial propios

**Files:**
- Create: `~/andia/andia/cerebro.py`
- Test: `~/andia/pruebas/test_cerebro.py`

**Interfaces:**
- Consumes: `herramientas.ESQUEMAS`, `herramientas.ejecutar`.
- Produces: `cerebro.bucle_herramientas(pedir, mensajes, ejecutar, max_vueltas=3) -> str`; `class cerebro.Cerebro(pedir, ejecutar, ruta_historial: Path, ahora=None, max_turnos=10)` con `responder(texto: str) -> str`, `olvidar()`, `prompt_sistema() -> str`; `cerebro.crear_pedir_deepseek(clave: str, modelo: str, esquemas: list) -> pedir`. `pedir(mensajes: list[dict]) -> objeto con .content y .tool_calls` (formato del SDK de OpenAI).

- [ ] **Step 1: Test (falla)**

`~/andia/pruebas/test_cerebro.py`:
```python
import json
from datetime import datetime
from types import SimpleNamespace
from zoneinfo import ZoneInfo

from andia import cerebro

AHORA = lambda: datetime(2026, 9, 3, 17, 30, tzinfo=ZoneInfo("America/Santiago"))


def respuesta(texto, tool_calls=None):
    return SimpleNamespace(content=texto, tool_calls=tool_calls)


def llamada(nombre, args):
    return SimpleNamespace(id="c1", function=SimpleNamespace(name=nombre, arguments=json.dumps(args)))


def test_prompt_dice_quien_es_y_la_hora(tmp_path):
    c = cerebro.Cerebro(pedir=lambda m: respuesta("ok"), ejecutar=lambda n, a: "", ruta_historial=tmp_path / "h.json", ahora=AHORA)
    p = c.prompt_sistema()
    assert "ANDIA" in p and "asistente de voz" in p
    assert "jueves 3 de septiembre de 2026" in p and "17:30" in p
    assert "280" in p and "dos frases" in p


def test_responder_guarda_historial_y_lo_reenvia(tmp_path):
    recibidos = []
    def pedir(mensajes):
        recibidos.append(mensajes)
        return respuesta("Hola Felipe.")
    c = cerebro.Cerebro(pedir, lambda n, a: "", tmp_path / "h.json", ahora=AHORA, max_turnos=2)
    assert c.responder("hola") == "Hola Felipe."
    c.responder("¿qué hora es?")
    c.responder("tercera")
    ultimo = recibidos[-1]
    assert ultimo[0]["role"] == "system"
    # max_turnos=2 → como mucho 2 turnos previos (4 mensajes) + el actual
    assert [m["content"] for m in ultimo if m["role"] == "user"] == ["¿qué hora es?", "tercera"]
    # persiste en disco y se recarga
    c2 = cerebro.Cerebro(pedir, lambda n, a: "", tmp_path / "h.json", ahora=AHORA, max_turnos=2)
    assert len(c2.historial) == 4


def test_olvidar_por_frase_y_por_metodo(tmp_path):
    c = cerebro.Cerebro(lambda m: respuesta("ok"), lambda n, a: "", tmp_path / "h.json", ahora=AHORA)
    c.responder("hola")
    assert c.responder("Olvida la conversación") == "Listo, empezamos de cero."
    assert c.historial == []
    c.responder("hola")
    c.olvidar()
    assert c.historial == []


def test_bucle_herramientas_ejecuta_y_devuelve_texto():
    llamadas = []
    def pedir(mensajes):
        if len(llamadas) == 0:
            llamadas.append(1)
            return respuesta("", [llamada("clima", {"lugar": "Ñuñoa"})])
        assert mensajes[-1]["role"] == "tool" and "22°" in mensajes[-1]["content"]
        return respuesta("Hace 22 grados.")
    r = cerebro.bucle_herramientas(pedir, [{"role": "user", "content": "clima"}], lambda n, a: "22° y sol")
    assert r == "Hace 22 grados."


def test_responder_acorta_respuestas_largas(tmp_path):
    largo = "Frase uno. " * 60
    c = cerebro.Cerebro(lambda m: respuesta(largo), lambda n, a: "", tmp_path / "h.json", ahora=AHORA)
    r = c.responder("cuéntame")
    assert len(r) <= 300 and r.endswith(".")
```

- [ ] **Step 2: Correr y ver que falla** → FAIL (`ModuleNotFoundError: andia.cerebro`).

- [ ] **Step 3: Implementar `cerebro.py`**

`~/andia/andia/cerebro.py`:
```python
"""El cerebro de ANDIA: prompt propio, historial en disco y bucle de herramientas sobre DeepSeek."""
import json
from datetime import datetime
from pathlib import Path
from zoneinfo import ZoneInfo

from .texto import partir_frases

TZ = ZoneInfo("America/Santiago")
DIAS = ["lunes", "martes", "miércoles", "jueves", "viernes", "sábado", "domingo"]
MESES = ["enero", "febrero", "marzo", "abril", "mayo", "junio", "julio", "agosto",
         "septiembre", "octubre", "noviembre", "diciembre"]
MAX_CARACTERES = 280
FRASES_OLVIDAR = ("olvida la conversación", "olvida la conversacion", "empecemos de nuevo", "borra el historial")

PROMPT = """Eres ANDIA (asistente de voz), el asistente de voz del reloj de Felipe.
Hablas en español de Chile, tuteando, con naturalidad y sin exagerar. Tu respuesta se muestra en una pantalla
muy pequeña y se lee en voz alta, así que responde en máximo dos frases y {max} caracteres, sin listas, sin
markdown, sin emojis y sin repetir la pregunta. Si te preguntan quién eres, di tu nombre y qué significa.
Usa las herramientas cuando necesites datos actuales (clima, búsquedas, recordatorios, estado del PC o del
robot, temporizadores); no inventes datos. Si una herramienta falla, dilo en una frase.
Ahora son las {hora} del {fecha} (hora de Chile)."""


def fecha_en_espanol(t: datetime) -> str:
    return f"{DIAS[t.weekday()]} {t.day} de {MESES[t.month - 1]} de {t.year}"


def bucle_herramientas(pedir, mensajes: list[dict], ejecutar, max_vueltas: int = 3) -> str:
    """Deja que el modelo pida herramientas, las ejecuta y le devuelve el resultado (copiado del bot)."""
    mensajes = list(mensajes)
    for _ in range(max_vueltas):
        m = pedir(mensajes)
        if not m.tool_calls:
            return (m.content or "").strip()
        mensajes.append({"role": "assistant", "content": m.content or "", "tool_calls": [
            {"id": c.id, "type": "function",
             "function": {"name": c.function.name, "arguments": c.function.arguments}} for c in m.tool_calls]})
        for c in m.tool_calls:
            try:
                args = json.loads(c.function.arguments or "{}")
            except ValueError:
                args = {}
            mensajes.append({"role": "tool", "tool_call_id": c.id, "content": ejecutar(c.function.name, args)})
    m = pedir(mensajes + [{"role": "user", "content": "Responde ya con lo que tengas, sin usar más herramientas."}])
    return (m.content or "").strip() or "No logré armar la respuesta con lo que encontré."


def acortar(texto: str, maximo: int = MAX_CARACTERES) -> str:
    """Corta por frases enteras hasta `maximo` caracteres (nunca a media palabra)."""
    if len(texto) <= maximo:
        return texto
    salida = ""
    for f in partir_frases(texto):
        if len(salida) + len(f) + 1 > maximo:
            break
        salida = (salida + " " + f).strip()
    return salida or texto[:maximo].rsplit(" ", 1)[0] + "."


class Cerebro:
    def __init__(self, pedir, ejecutar, ruta_historial: Path, ahora=None, max_turnos: int = 10):
        self.pedir = pedir
        self.ejecutar = ejecutar
        self.ruta = Path(ruta_historial)
        self.ahora = ahora or (lambda: datetime.now(TZ))
        self.max_turnos = max_turnos
        self.historial: list[dict] = []
        if self.ruta.exists():
            try:
                self.historial = json.loads(self.ruta.read_text(encoding="utf-8"))
            except ValueError:
                self.historial = []

    def prompt_sistema(self) -> str:
        t = self.ahora()
        return PROMPT.format(max=MAX_CARACTERES, hora=t.strftime("%H:%M"), fecha=fecha_en_espanol(t))

    def _guardar(self) -> None:
        self.ruta.parent.mkdir(parents=True, exist_ok=True)
        self.ruta.write_text(json.dumps(self.historial, ensure_ascii=False, indent=1), encoding="utf-8")

    def olvidar(self) -> None:
        self.historial = []
        self._guardar()

    def responder(self, texto: str) -> str:
        texto = texto.strip()
        if texto.lower().rstrip(".!") in FRASES_OLVIDAR:
            self.olvidar()
            return "Listo, empezamos de cero."
        mensajes = [{"role": "system", "content": self.prompt_sistema()}]
        mensajes += self.historial[-2 * self.max_turnos:]
        mensajes.append({"role": "user", "content": texto})
        respuesta = acortar(bucle_herramientas(self.pedir, mensajes, self.ejecutar))
        self.historial += [{"role": "user", "content": texto}, {"role": "assistant", "content": respuesta}]
        self.historial = self.historial[-2 * self.max_turnos:]
        self._guardar()
        return respuesta


def crear_pedir_deepseek(clave: str, modelo: str, esquemas: list):
    """Cableado real: DeepSeek por su API compatible con OpenAI, sin 'thinking' (consume max_tokens y llega vacío)."""
    from openai import OpenAI
    cliente = OpenAI(api_key=clave, base_url="https://api.deepseek.com", timeout=20.0, max_retries=1)

    def pedir(mensajes: list[dict]):
        r = cliente.chat.completions.create(
            model=modelo, messages=mensajes, max_tokens=400, temperature=0.7,
            tools=esquemas, tool_choice="auto",
            extra_body={"thinking": {"type": "disabled"}},
        )
        return r.choices[0].message
    return pedir
```

- [ ] **Step 4: Correr y ver que pasa** → `5 passed`.

- [ ] **Step 5: Commit**

```bash
git add -A && git commit -q -m "Cerebro de ANDIA: prompt propio, historial en disco, bucle de herramientas, DeepSeek"
```

---

### Task 7: Máquina de estados de una conversación (`Sesion`)

**Files:**
- Create: `~/andia/andia/sesion.py`
- Test: `~/andia/pruebas/test_sesion.py`

**Interfaces:**
- Consumes: `activacion.Detector` (`alimentar(pcm) -> int`, `reiniciar()`), `activacion.FinDeFrase` (`alimentar(pcm) -> bool`, `reiniciar()`).
- Produces: `class sesion.Sesion(detector, fin_frase, ventana_ms=6000, tasa=16000)`; `Sesion.inicio(modo: str) -> list`; `Sesion.audio(pcm: bytes) -> list`; `Sesion.cancelar() -> list`; `Sesion.terminar() -> None`; atributo `estado` ∈ {`"inactiva"`, `"esperando_activacion"`, `"escuchando"`, `"procesando"`}. Las listas contienen `dict` (mensajes JSON para el cliente) y, como máximo una vez por frase, la tupla `("frase", pcm_bytes)`.

- [ ] **Step 1: Test (falla)**

`~/andia/pruebas/test_sesion.py`:
```python
from andia import sesion


class DetectorFalso:
    def __init__(self, en_llamada=None):
        self.en_llamada = en_llamada   # número de llamada en que "detecta"
        self.n = 0
    def alimentar(self, pcm):
        self.n += 1
        return 0 if self.n == self.en_llamada else -1
    def reiniciar(self):
        self.n = 0


class FinFalso:
    def __init__(self, termina_en=None):
        self.termina_en = termina_en
        self.n = 0
    def alimentar(self, pcm):
        self.n += 1
        return self.n == self.termina_en
    def reiniciar(self):
        self.n = 0


MS100 = b"\x00\x00" * 1600   # 100 ms


def test_modo_boton_escucha_directo_y_entrega_la_frase():
    s = sesion.Sesion(DetectorFalso(), FinFalso(termina_en=3))
    assert s.inicio("boton") == [{"t": "activada"}]
    assert s.estado == "escuchando"
    assert s.audio(MS100) == [] and s.audio(MS100) == []
    salida = s.audio(MS100)
    assert salida[0] == {"t": "pensando"}
    assert salida[1][0] == "frase" and salida[1][1] == MS100 * 3
    assert s.estado == "procesando"
    assert s.audio(MS100) == []                     # se ignora mientras procesa
    s.terminar()
    assert s.estado == "inactiva"


def test_modo_gesto_exige_activacion_y_vence_a_los_6s():
    s = sesion.Sesion(DetectorFalso(en_llamada=None), FinFalso(), ventana_ms=6000)
    assert s.inicio("gesto") == []
    assert s.estado == "esperando_activacion"
    for _ in range(59):
        assert s.audio(MS100) == []
    assert s.audio(MS100) == [{"t": "fin", "motivo": "sin_activacion"}]
    assert s.estado == "inactiva"


def test_modo_gesto_activada_pasa_a_escuchar_desde_cero():
    fin = FinFalso(termina_en=2)
    s = sesion.Sesion(DetectorFalso(en_llamada=3), fin)
    s.inicio("gesto")
    assert s.audio(MS100) == [] and s.audio(MS100) == []
    assert s.audio(MS100) == [{"t": "activada"}]
    assert s.estado == "escuchando"
    assert s.audio(b"\x01\x00" * 1600) == []
    salida = s.audio(b"\x02\x00" * 1600)
    assert salida[1] == ("frase", b"\x01\x00" * 1600 + b"\x02\x00" * 1600)   # solo el audio posterior a la activación


def test_cancelar_en_cualquier_estado():
    s = sesion.Sesion(DetectorFalso(), FinFalso())
    s.inicio("boton")
    assert s.cancelar() == [{"t": "fin", "motivo": "cancelado"}]
    assert s.estado == "inactiva"
    assert s.cancelar() == []


def test_audio_sin_inicio_se_ignora():
    s = sesion.Sesion(DetectorFalso(), FinFalso())
    assert s.audio(MS100) == []
```

- [ ] **Step 2: Correr y ver que falla** → FAIL (`ModuleNotFoundError: andia.sesion`).

- [ ] **Step 3: Implementar `sesion.py`**

`~/andia/andia/sesion.py`:
```python
"""Máquina de estados de una conversación con el reloj. Pura: recibe bytes y devuelve mensajes.

Estados: inactiva → (inicio) → esperando_activacion (modo gesto) | escuchando (modo botón) → procesando → inactiva.
"""
from .audio import TASA


class Sesion:
    def __init__(self, detector, fin_frase, ventana_ms: int = 6000, tasa: int = TASA):
        self.detector = detector
        self.fin_frase = fin_frase
        self.ventana_ms = ventana_ms
        self.bytes_por_ms = tasa * 2 // 1000
        self.estado = "inactiva"
        self.frase = b""
        self.escuchado_ms = 0

    def inicio(self, modo: str) -> list:
        self.detector.reiniciar()
        self.fin_frase.reiniciar()
        self.frase = b""
        self.escuchado_ms = 0
        if modo == "boton":
            self.estado = "escuchando"
            return [{"t": "activada"}]
        self.estado = "esperando_activacion"
        return []

    def audio(self, pcm: bytes) -> list:
        if self.estado == "esperando_activacion":
            self.escuchado_ms += len(pcm) // self.bytes_por_ms
            if self.detector.alimentar(pcm) >= 0:
                self.estado = "escuchando"
                self.fin_frase.reiniciar()
                self.frase = b""
                return [{"t": "activada"}]
            if self.escuchado_ms >= self.ventana_ms:
                self.estado = "inactiva"
                return [{"t": "fin", "motivo": "sin_activacion"}]
            return []
        if self.estado == "escuchando":
            self.frase += pcm
            if self.fin_frase.alimentar(pcm):
                self.estado = "procesando"
                return [{"t": "pensando"}, ("frase", self.frase)]
            return []
        return []

    def cancelar(self) -> list:
        if self.estado == "inactiva":
            return []
        self.estado = "inactiva"
        self.frase = b""
        return [{"t": "fin", "motivo": "cancelado"}]

    def terminar(self) -> None:
        self.estado = "inactiva"
        self.frase = b""
```

- [ ] **Step 4: Correr y ver que pasa** → `5 passed`.

- [ ] **Step 5: Commit**

```bash
git add -A && git commit -q -m "Sesión: máquina de estados de una conversación (gesto/botón, activación, fin de frase, cancelar)"
```

---

### Task 8: Servidor FastAPI: `/ws`, `/avisar`, `/salud`, recordatorios y cableado real

**Files:**
- Create: `~/andia/andia/servidor.py`
- Test: `~/andia/pruebas/test_servidor.py`

**Interfaces:**
- Consumes: `Sesion`, `Cerebro.responder`, `stt.transcribir_pcm`, `tts.sintetizar_pcm16k`, `tts.servidor_vivo`, `herramientas.tomar_temporizador`, `recordatorios.Agenda`.
- Produces: `servidor.crear_app(fabrica_sesion, procesar_frase, token: str, agenda=None, intervalo_recordatorios_s=30) -> FastAPI`; `servidor.crear_procesar_frase(transcribir, responder, sintetizar, hay_voz, tomar_temporizador) -> procesar_frase` donde `procesar_frase(pcm: bytes) -> list[dict | bytes]`; `servidor.app` (instancia real cuando hay `.env`). Mensajes de aviso: `{"t":"aviso","texto":...,"origen":...}`.

- [ ] **Step 1: Test (falla)**

`~/andia/pruebas/test_servidor.py`:
```python
import json

from fastapi.testclient import TestClient

from andia import servidor
from andia.sesion import Sesion


class DetectorFalso:
    def alimentar(self, pcm): return -1
    def reiniciar(self): pass


class FinFalso:
    """Termina la frase en cuanto recibe 2 trozos."""
    def __init__(self): self.n = 0
    def alimentar(self, pcm):
        self.n += 1
        return self.n >= 2
    def reiniciar(self): self.n = 0


def fabrica():
    return Sesion(DetectorFalso(), FinFalso())


def procesar_ok(pcm):
    assert len(pcm) == 400
    return [{"t": "pregunta", "texto": "hola"}, {"t": "respuesta", "texto": "Hola Felipe.", "voz": True},
            b"\x00" * 100, b"\x00" * 100, {"t": "fin"}]


def cliente():
    app = servidor.crear_app(fabrica, procesar_ok, token="secreto")
    return TestClient(app)


def test_salud():
    assert cliente().get("/salud").json()["ok"] is True


def test_ws_rechaza_token_malo():
    with cliente().websocket_connect("/ws") as ws:
        ws.send_text(json.dumps({"t": "hola", "token": "otro", "reloj": "x", "v": 1}))
        assert ws.receive_json() == {"t": "error", "texto": "token inválido"}


def test_ws_flujo_boton_completo():
    with cliente().websocket_connect("/ws") as ws:
        ws.send_text(json.dumps({"t": "hola", "token": "secreto", "reloj": "x", "v": 1}))
        assert ws.receive_json() == {"t": "hola", "ok": True}
        ws.send_text(json.dumps({"t": "inicio", "modo": "boton"}))
        assert ws.receive_json() == {"t": "activada"}
        ws.send_bytes(b"\x00" * 200)
        ws.send_bytes(b"\x00" * 200)
        assert ws.receive_json() == {"t": "pensando"}
        assert ws.receive_json() == {"t": "pregunta", "texto": "hola"}
        assert ws.receive_json() == {"t": "respuesta", "texto": "Hola Felipe.", "voz": True}
        assert ws.receive_bytes() == b"\x00" * 100
        assert ws.receive_bytes() == b"\x00" * 100
        assert ws.receive_json() == {"t": "fin"}
        ws.send_text(json.dumps({"t": "ping"}))
        assert ws.receive_json() == {"t": "pong"}


def test_avisar_llega_al_reloj_conectado():
    app = servidor.crear_app(fabrica, procesar_ok, token="secreto")
    c = TestClient(app)
    with c.websocket_connect("/ws") as ws:
        ws.send_text(json.dumps({"t": "hola", "token": "secreto", "reloj": "x", "v": 1}))
        ws.receive_json()
        r = c.post("/avisar", json={"texto": "terminó la compilación", "token": "secreto", "origen": "pc"})
        assert r.json() == {"ok": True, "entregado": True}
        assert ws.receive_json() == {"t": "aviso", "texto": "terminó la compilación", "origen": "pc"}
    r = c.post("/avisar", json={"texto": "sin reloj", "token": "secreto"})
    assert r.json() == {"ok": True, "entregado": False}
    assert c.post("/avisar", json={"texto": "x", "token": "malo"}).status_code == 403


def test_procesar_frase_real_con_piezas_falsas():
    pf = servidor.crear_procesar_frase(
        transcribir=lambda pcm: "qué hora es",
        responder=lambda t: "Son las cinco.",
        sintetizar=lambda t: b"\x01" * 7000,
        hay_voz=lambda: True,
        tomar_temporizador=lambda: 90,
    )
    salida = pf(b"\x00" * 400)
    assert salida[0] == {"t": "pregunta", "texto": "qué hora es"}
    assert salida[1] == {"t": "respuesta", "texto": "Son las cinco.", "voz": True}
    audio = [x for x in salida if isinstance(x, bytes)]
    assert b"".join(audio) == b"\x01" * 7000 and all(len(a) <= 3200 for a in audio)
    assert {"t": "temporizador", "segundos": 90} in salida
    assert salida[-1] == {"t": "fin"}


def test_procesar_frase_sin_voz_y_sin_texto():
    pf = servidor.crear_procesar_frase(lambda pcm: "", lambda t: "x", lambda t: b"", lambda: False, lambda: None)
    salida = pf(b"\x00" * 400)
    assert salida == [{"t": "respuesta", "texto": "No te entendí.", "voz": False}, {"t": "fin"}]


def test_procesar_frase_si_falla_la_voz_responde_en_texto():
    def sintetizar(t):
        raise RuntimeError("fish caído")
    pf = servidor.crear_procesar_frase(lambda pcm: "hola", lambda t: "Hola.", sintetizar, lambda: True, lambda: None)
    salida = pf(b"\x00" * 400)
    assert salida[1] == {"t": "respuesta", "texto": "Hola.", "voz": False}
```

- [ ] **Step 2: Correr y ver que falla** → FAIL (`ModuleNotFoundError: andia.servidor`).

- [ ] **Step 3: Implementar `servidor.py`**

`~/andia/andia/servidor.py`:
```python
"""Servidor de ANDIA: WebSocket para el reloj (o el reloj falso del PC), avisos por HTTP y recordatorios."""
import asyncio
import json
import os
from pathlib import Path

from fastapi import FastAPI, WebSocket, WebSocketDisconnect
from fastapi.concurrency import run_in_threadpool
from fastapi.responses import JSONResponse

TROZO_AUDIO = 3200          # 100 ms de PCM 16 kHz por mensaje binario
_DIR = Path(__file__).resolve().parent.parent


def crear_procesar_frase(transcribir, responder, sintetizar, hay_voz, tomar_temporizador):
    """Arma la tubería STT → LLM → TTS como función pura de una frase PCM a una lista de mensajes."""
    def procesar_frase(pcm: bytes) -> list:
        salida = []
        try:
            texto = transcribir(pcm)
        except Exception as e:
            return [{"t": "error", "texto": f"No pude transcribir: {type(e).__name__}"}, {"t": "fin"}]
        if not texto:
            return [{"t": "respuesta", "texto": "No te entendí.", "voz": False}, {"t": "fin"}]
        salida.append({"t": "pregunta", "texto": texto})
        try:
            respuesta = responder(texto)
        except Exception as e:
            return salida + [{"t": "error", "texto": f"ANDIA no pudo responder: {type(e).__name__}"}, {"t": "fin"}]
        audio = b""
        if hay_voz():
            try:
                audio = sintetizar(respuesta)
            except Exception as e:
                print(f"[tts] falló ({type(e).__name__}: {str(e)[:80]}); respondo en texto")
                audio = b""
        salida.append({"t": "respuesta", "texto": respuesta, "voz": bool(audio)})
        for i in range(0, len(audio), TROZO_AUDIO):
            salida.append(audio[i:i + TROZO_AUDIO])
        segundos = tomar_temporizador()
        if segundos:
            salida.append({"t": "temporizador", "segundos": segundos})
        salida.append({"t": "fin"})
        return salida
    return procesar_frase


def crear_app(fabrica_sesion, procesar_frase, token: str, agenda=None, intervalo_recordatorios_s: int = 30) -> FastAPI:
    app = FastAPI(title="ANDIA")
    conectados: set[WebSocket] = set()

    async def enviar(ws: WebSocket, item) -> None:
        if isinstance(item, (bytes, bytearray)):
            await ws.send_bytes(bytes(item))
        else:
            await ws.send_text(json.dumps(item, ensure_ascii=False))

    async def difundir(mensaje: dict) -> bool:
        entregado = False
        for ws in list(conectados):
            try:
                await enviar(ws, mensaje)
                entregado = True
            except Exception:
                conectados.discard(ws)
        return entregado

    @app.get("/salud")
    async def salud():
        return {"ok": True, "relojes": len(conectados)}

    @app.post("/avisar")
    async def avisar(cuerpo: dict):
        if cuerpo.get("token") != token:
            return JSONResponse({"ok": False, "error": "token inválido"}, status_code=403)
        texto = str(cuerpo.get("texto", "")).strip()
        if not texto:
            return JSONResponse({"ok": False, "error": "falta texto"}, status_code=400)
        entregado = await difundir({"t": "aviso", "texto": texto, "origen": cuerpo.get("origen", "")})
        return {"ok": True, "entregado": entregado}

    @app.websocket("/ws")
    async def ws_reloj(ws: WebSocket):
        await ws.accept()
        try:
            hola = json.loads(await ws.receive_text())
        except Exception:
            await ws.close()
            return
        if hola.get("t") != "hola" or hola.get("token") != token:
            await enviar(ws, {"t": "error", "texto": "token inválido"})
            await ws.close()
            return
        await enviar(ws, {"t": "hola", "ok": True})
        conectados.add(ws)
        sesion = fabrica_sesion()
        try:
            while True:
                m = await ws.receive()
                if m.get("type") == "websocket.disconnect":
                    break
                if m.get("bytes") is not None:
                    salidas = sesion.audio(m["bytes"])
                elif m.get("text") is not None:
                    try:
                        msg = json.loads(m["text"])
                    except ValueError:
                        continue
                    t = msg.get("t")
                    if t == "inicio":
                        salidas = sesion.inicio(msg.get("modo", "boton"))
                    elif t == "cancelar":
                        salidas = sesion.cancelar()
                    elif t == "ping":
                        salidas = [{"t": "pong"}]
                    else:
                        salidas = []
                else:
                    salidas = []
                for s in salidas:
                    if isinstance(s, tuple) and s[0] == "frase":
                        for item in await run_in_threadpool(procesar_frase, s[1]):
                            await enviar(ws, item)
                        sesion.terminar()
                    else:
                        await enviar(ws, s)
        except WebSocketDisconnect:
            pass
        finally:
            conectados.discard(ws)

    if agenda is not None:
        async def vigilar_recordatorios():
            while True:
                for item in agenda.vencidos():
                    if await difundir({"t": "aviso", "texto": item["mensaje"], "origen": "recordatorio"}):
                        agenda.marcar_disparado(item["id"])
                await asyncio.sleep(intervalo_recordatorios_s)

        @app.on_event("startup")
        async def _arrancar():
            asyncio.create_task(vigilar_recordatorios())

    return app


# ---- Cableado real (lo que ejecuta uvicorn: `uvicorn andia.servidor:app`) ------------------------

def _app_real() -> FastAPI:
    from dotenv import load_dotenv
    load_dotenv(_DIR / ".env")
    from . import activacion, cerebro, herramientas, recordatorios, stt, tts
    from .sesion import Sesion

    datos = _DIR / "datos"
    datos.mkdir(exist_ok=True)
    agenda = recordatorios.Agenda(datos / "recordatorios.json")
    recordatorios.usar(agenda)

    palabras = sorted(str(p) for p in (_DIR / "palabras").glob("*.ppn"))
    if not palabras:
        raise SystemExit("faltan los .ppn de Picovoice en palabras/ (ver README)")
    modelo_es = next(iter((_DIR / "palabras").glob("porcupine_params_es.pv")), None)
    if modelo_es is None:
        import pvporcupine
        candidatos = list(Path(pvporcupine.__file__).parent.glob("lib/common/porcupine_params_es.pv"))
        modelo_es = candidatos[0] if candidatos else None
    sensibilidad = {p: (0.4 if Path(p).stem.lower().startswith("andia") else 0.6) for p in palabras}
    motor = activacion.crear_porcupine(os.environ["PICOVOICE_ACCESS_KEY"], palabras,
                                       [sensibilidad[p] for p in palabras], str(modelo_es) if modelo_es else None)
    print(f"[andia] frases de activación: {[Path(p).stem for p in palabras]}")

    def fabrica_sesion():
        return Sesion(activacion.Detector(motor), activacion.FinDeFrase(activacion.crear_vad(2)))

    clave_openai = os.environ["OPENAI_API_KEY"]
    pedir = cerebro.crear_pedir_deepseek(os.environ["DEEPSEEK_API_KEY"], os.environ.get("LLM_MODEL", "deepseek-v4-pro"),
                                         herramientas.ESQUEMAS)
    mente = cerebro.Cerebro(pedir, herramientas.ejecutar, datos / "historial.json")
    url_tts = os.environ.get("TTS_URL", "http://192.168.x.x:8080")
    voz = os.environ.get("TTS_VOZ", "mayuri")

    procesar = crear_procesar_frase(
        transcribir=lambda pcm: stt.transcribir_pcm(pcm, clave_openai),
        responder=mente.responder,
        sintetizar=lambda texto: tts.sintetizar_pcm16k(texto, url_tts, voz),
        hay_voz=lambda: tts.servidor_vivo(url_tts),
        tomar_temporizador=herramientas.tomar_temporizador,
    )
    return crear_app(fabrica_sesion, procesar, token=os.environ.get("ANDIA_TOKEN", ""), agenda=agenda)


if (_DIR / ".env").exists() and os.environ.get("ANDIA_SIN_ARRANQUE") != "1":
    app = _app_real()
```

Nota: Porcupine es un solo motor compartido entre sesiones; como el reloj es uno solo, `Detector` lo reinicia por conversación y no hay dos sesiones activas a la vez. Si algún día hay dos clientes, crear un motor por sesión.

- [ ] **Step 4: Correr y ver que pasa**

Run: `ANDIA_SIN_ARRANQUE=1 pytest -q pruebas/test_servidor.py`
Expected: `7 passed` (la variable evita que el módulo intente arrancar la app real al importarse).

- [ ] **Step 5: Correr toda la suite**

Run: `ANDIA_SIN_ARRANQUE=1 pytest -q`
Expected: todo `passed`.

- [ ] **Step 6: Commit**

```bash
git add -A && git commit -q -m "Servidor FastAPI: WebSocket del reloj, tubería STT→LLM→TTS, /avisar, recordatorios, cableado real"
```

---

### Task 9: Reloj falso en el PC (`cliente_pc.py`)

**Files:**
- Create: `~/andia/cliente_pc.py`, `~/andia/requirements-pc.txt`

**Interfaces:**
- Consumes: protocolo WebSocket de la Task 8.
- Produces: script que se ejecuta en Windows con `py.exe cliente_pc.py ws://192.168.x.x:8001/ws TOKEN`.

- [ ] **Step 1: Escribir el cliente**

`~/andia/requirements-pc.txt`:
```
sounddevice>=0.5
websockets>=12
```

`~/andia/cliente_pc.py`:
```python
"""Reloj falso: micrófono y altavoz del PC contra el servidor ANDIA. Sirve para probar todo sin firmware.

Uso (Windows): py cliente_pc.py ws://192.168.x.x:8001/ws MI_TOKEN
Teclas: Enter = modo botón (habla directo); g = modo gesto (di "Oye Andia ..."); c = cancelar; q = salir.
"""
import asyncio
import json
import queue
import sys
import threading

import sounddevice as sd
import websockets

TASA = 16000
BLOQUE = 512     # muestras por trozo (lo que pide Porcupine)


async def principal(url: str, token: str):
    cola_mic = queue.Queue()
    cola_alt = queue.Queue()
    enviando = threading.Event()

    def cb_mic(datos, frames, tiempo, estado):
        if enviando.is_set():
            cola_mic.put(bytes(datos))

    def cb_alt(salida, frames, tiempo, estado):
        try:
            trozo = cola_alt.get_nowait()
            salida[:len(trozo)] = trozo
            if len(trozo) < len(salida):
                salida[len(trozo):] = b"\x00" * (len(salida) - len(trozo))
        except queue.Empty:
            salida[:] = b"\x00" * len(salida)

    mic = sd.RawInputStream(samplerate=TASA, channels=1, dtype="int16", blocksize=BLOQUE, callback=cb_mic)
    alt = sd.RawOutputStream(samplerate=TASA, channels=1, dtype="int16", blocksize=BLOQUE, callback=cb_alt)
    async with websockets.connect(url, max_size=None) as ws:
        await ws.send(json.dumps({"t": "hola", "token": token, "reloj": "pc", "v": 1}))
        print("servidor:", await ws.recv())

        async def bombear_mic():
            while True:
                try:
                    trozo = cola_mic.get_nowait()
                    await ws.send(trozo)
                except queue.Empty:
                    await asyncio.sleep(0.01)

        async def recibir():
            async for m in ws:
                if isinstance(m, bytes):
                    for i in range(0, len(m), BLOQUE * 2):
                        cola_alt.put(m[i:i + BLOQUE * 2])
                else:
                    msg = json.loads(m)
                    print("<-", msg)
                    if msg.get("t") in ("fin", "error"):
                        enviando.clear()

        async def teclado():
            loop = asyncio.get_event_loop()
            while True:
                tecla = await loop.run_in_executor(None, input, "[Enter=botón, g=gesto, c=cancelar, q=salir] ")
                tecla = tecla.strip().lower()
                if tecla == "q":
                    return
                if tecla == "c":
                    await ws.send(json.dumps({"t": "cancelar"}))
                    enviando.clear()
                    continue
                with cola_mic.mutex:
                    cola_mic.queue.clear()
                await ws.send(json.dumps({"t": "inicio", "modo": "gesto" if tecla == "g" else "boton"}))
                enviando.set()
                print("... hablando" if tecla != "g" else "... di 'Oye Andia' y tu pregunta")

        with mic, alt:
            tareas = [asyncio.create_task(bombear_mic()), asyncio.create_task(recibir())]
            await teclado()
            for t in tareas:
                t.cancel()


if __name__ == "__main__":
    if len(sys.argv) < 3:
        print(__doc__)
        sys.exit(1)
    asyncio.run(principal(sys.argv[1], sys.argv[2]))
```

- [ ] **Step 2: Probar el cliente contra un servidor local en WSL con piezas falsas** (sin claves)

Crear `~/andia/servidor_falso.py` (solo para esta prueba; no se despliega):
```python
"""Servidor de prueba: sin Porcupine ni claves. Modo botón: responde con un tono de 1 s."""
import math
import struct

import uvicorn

from andia import servidor
from andia.activacion import FinDeFrase
from andia.sesion import Sesion


class DetectorNulo:
    def alimentar(self, pcm): return -1
    def reiniciar(self): pass


class VadEnergia:
    def is_speech(self, trama, tasa):
        muestras = struct.unpack(f"<{len(trama)//2}h", trama)
        return sum(abs(m) for m in muestras) / len(muestras) > 300


def tono(seg=1.0):
    return b"".join(struct.pack("<h", int(8000 * math.sin(2 * math.pi * 440 * i / 16000))) for i in range(int(16000 * seg)))


pf = servidor.crear_procesar_frase(lambda pcm: f"({len(pcm)//32} ms de audio)", lambda t: f"Recibí {t}.",
                                   lambda t: tono(), lambda: True, lambda: None)
app = servidor.crear_app(lambda: Sesion(DetectorNulo(), FinDeFrase(VadEnergia())), pf, token="prueba")
uvicorn.run(app, host="0.0.0.0", port=8001)
```

Run en WSL: `ANDIA_SIN_ARRANQUE=1 python servidor_falso.py` (en segundo plano con `setsid nohup ... > /tmp/andia_falso.log 2>&1 &`).
Run en Windows (Felipe con `!`, o desde WSL con `cd /mnt/c && py.exe`): `py -m pip install --user sounddevice websockets` y luego `py \\wsl.localhost\Ubuntu\home\felipe\andia\cliente_pc.py ws://<IP de WSL>:8001/ws prueba` (la IP de WSL sale de `hostname -I`; desde Windows se llega a WSL por su IP directa).
Expected: al pulsar Enter y hablar, el servidor imprime `pregunta` con los ms de audio, `respuesta` con `voz: true` y suena un tono de 1 s en el altavoz del PC. Si no hay sonido, revisar el dispositivo por defecto de `sounddevice` (`py -c "import sounddevice as sd; print(sd.query_devices())"`).

- [ ] **Step 3: Commit**

```bash
git add -A && git commit -q -m "Reloj falso para Windows (micrófono/altavoz por WebSocket) y servidor de prueba sin claves"
```

---

### Task 10: Exponer el Fish del PC a la LAN y verificar la voz desde WSL

**Files:**
- Create: `~/andia/tools/exponer_fish.ps1`
- Modify: `/mnt/c/Users/Felipe/Desktop/fishaudio/scripts/servidor.sh` (el comando que arranca el servidor: escuchar en `0.0.0.0`)

**Interfaces:**
- Produces: `http://192.168.x.x:8080/v1/health` responde desde la Pi.

- [ ] **Step 1: Hacer que el servidor Fish escuche en todas las interfaces de WSL**

Run: `grep -n "api_server\|--listen\|127.0.0.1" /mnt/c/Users/Felipe/Desktop/fishaudio/scripts/servidor.sh`
Cambiar el argumento de escucha del servidor (`--listen 127.0.0.1:8080` o el equivalente) por `--listen 0.0.0.0:8080`; si no existe el argumento, añadirlo al comando que lanza `tools/api_server.py`. Actualizar el comentario de la línea 10 ("Escucha en http://0.0.0.0:8080"). El chequeo de `--estado` sigue usando 127.0.0.1 (válido).

- [ ] **Step 2: Escribir el script de portproxy** (Windows reenvía 8080 → IP de WSL:8080; la IP de WSL cambia al reiniciar, por eso se recalcula)

`~/andia/tools/exponer_fish.ps1`:
```powershell
# Expone el servidor Fish (WSL, puerto 8080) a la LAN en la IP del PC. Ejecutar como administrador.
# Idempotente: borra el reenvío anterior y crea uno nuevo con la IP actual de WSL.
$wslIp = (wsl.exe -d Ubuntu hostname -I).Trim().Split(" ")[0]
if (-not $wslIp) { Write-Error "no pude obtener la IP de WSL"; exit 1 }
netsh interface portproxy delete v4tov4 listenport=8080 listenaddress=0.0.0.0 | Out-Null
netsh interface portproxy add v4tov4 listenport=8080 listenaddress=0.0.0.0 connectport=8080 connectaddress=$wslIp
if (-not (Get-NetFirewallRule -DisplayName "Fish TTS 8080" -ErrorAction SilentlyContinue)) {
  New-NetFirewallRule -DisplayName "Fish TTS 8080" -Direction Inbound -Protocol TCP -LocalPort 8080 -Action Allow | Out-Null
}
Write-Host "Fish expuesto: 0.0.0.0:8080 -> $wslIp:8080"
netsh interface portproxy show v4tov4
```

- [ ] **Step 3: Ejecutarlo (Felipe, PowerShell como administrador)**

```powershell
powershell -ExecutionPolicy Bypass -File \\wsl.localhost\Ubuntu\home\felipe\andia\tools\exponer_fish.ps1
```
Y arrancar Fish: en WSL `~/…/fishaudio/scripts/servidor.sh --fondo` (ruta real del script; ver memoria del proyecto Fish).

- [ ] **Step 4: Verificar desde la Pi**

Run: `ssh felipe@192.168.x.x 'curl -s -m 5 http://192.168.x.x:8080/v1/health; echo'`
Expected: respuesta de salud del servidor Fish (JSON o `OK`). Si falla: `netsh interface portproxy show v4tov4` en Windows, y `curl http://127.0.0.1:8080/v1/health` dentro de WSL.

- [ ] **Step 5: Probar una síntesis real con Mayuri desde WSL**

Run: `cd ~/andia && . venv/bin/activate && python -c "
from andia import tts, audio
pcm = tts.sintetizar_pcm16k('Hola Felipe, soy Andia.', 'http://127.0.0.1:8080', 'mayuri')
open('/tmp/andia_hola.wav','wb').write(audio.pcm_a_wav(pcm)); print(len(pcm)/32000, 'segundos')"`
Expected: imprime una duración de 1-3 s. Escuchar el WAV (`explorer.exe` sobre `\\wsl.localhost\Ubuntu\tmp\andia_hola.wav` o `cp` al Escritorio): voz de Mayuri, volumen normal.

- [ ] **Step 6: Persistir el portproxy al reiniciar** (opcional pero recomendado): tarea programada al inicio de sesión.

```powershell
schtasks /Create /TN "ExponerFish" /SC ONLOGON /RL HIGHEST /TR "powershell -ExecutionPolicy Bypass -File \\wsl.localhost\Ubuntu\home\felipe\andia\tools\exponer_fish.ps1" /F
```

- [ ] **Step 7: Commit**

```bash
git add -A && git commit -q -m "Fish del PC expuesto a la LAN (portproxy + firewall) para que la Pi pida la voz"
```

---

### Task 11: Palabras de activación (Picovoice Console) y prueba real de Porcupine

**Files:**
- Create: `~/andia/pruebas/integracion/README.md`, `~/andia/pruebas/integracion/test_porcupine_real.py`
- Data: `~/andia/palabras/*.ppn` (fuera de git)

**Interfaces:**
- Produces: motor Porcupine real cargando los `.ppn` en español; test de integración marcado `@pytest.mark.skipif` sin clave.

- [ ] **Step 1: Felipe crea las palabras en Picovoice Console** (https://console.picovoice.ai): cuenta → AccessKey (copiar a `.env` como `PICOVOICE_ACCESS_KEY`) → Porcupine → idioma **Español** → frase → plataforma **Raspberry Pi** → entrenar → descargar. Repetir para: `oye andia`, `hola andia`, `hey andia`, `ey andia`, `andia`. Guardar los `.ppn` en `~/andia/palabras/` con nombres `oye_andia.ppn`, `hola_andia.ppn`, `hey_andia.ppn`, `ey_andia.ppn`, `andia.ppn` (el servidor pone sensibilidad 0,4 a `andia` y 0,6 al resto por el nombre de archivo).

- [ ] **Step 2: Escribir el README de integración y el test**

`~/andia/pruebas/integracion/README.md`:
```markdown
# Pruebas de integración (necesitan claves y micrófono)

- `test_porcupine_real.py`: carga los `.ppn` de `palabras/` con la clave de Picovoice y pasa WAV grabados.
  Grabar con el PC (Windows): `py -c "import sounddevice as sd, scipy.io.wavfile as w; d=sd.rec(int(3*16000),16000,1,dtype='int16'); sd.wait(); w.write('oye_andia.wav',16000,d)"`
  Guardar en `pruebas/integracion/wav/`: `oye_andia.wav`, `hola_andia.wav`, `hey_andia.wav`, `ana_manana.wav`
  (frase sin activación: "mañana viene Ana a la semana"), `silencio.wav`.
- Se saltan solas si no hay `PICOVOICE_ACCESS_KEY` o faltan los archivos.
```

`~/andia/pruebas/integracion/test_porcupine_real.py`:
```python
import os
import wave
from pathlib import Path

import pytest

from andia import activacion

DIR = Path(__file__).parent
PALABRAS = sorted((DIR.parent.parent / "palabras").glob("*.ppn"))
CLAVE = os.environ.get("PICOVOICE_ACCESS_KEY", "")
pytestmark = pytest.mark.skipif(not CLAVE or not PALABRAS, reason="sin clave de Picovoice o sin .ppn")


def pcm(nombre):
    with wave.open(str(DIR / "wav" / nombre), "rb") as w:
        assert w.getframerate() == 16000 and w.getnchannels() == 1
        return w.readframes(w.getnframes())


@pytest.fixture(scope="module")
def detector():
    import pvporcupine
    modelo = next(Path(pvporcupine.__file__).parent.glob("lib/common/porcupine_params_es.pv"))
    motor = activacion.crear_porcupine(CLAVE, [str(p) for p in PALABRAS], [0.5] * len(PALABRAS), str(modelo))
    return activacion.Detector(motor)


@pytest.mark.parametrize("archivo", ["oye_andia.wav", "hola_andia.wav", "hey_andia.wav"])
def test_detecta_las_frases(detector, archivo):
    if not (DIR / "wav" / archivo).exists():
        pytest.skip(f"falta {archivo}")
    detector.reiniciar()
    assert detector.alimentar(pcm(archivo)) >= 0


@pytest.mark.parametrize("archivo", ["ana_manana.wav", "silencio.wav"])
def test_no_detecta_parecidas(detector, archivo):
    if not (DIR / "wav" / archivo).exists():
        pytest.skip(f"falta {archivo}")
    detector.reiniciar()
    assert detector.alimentar(pcm(archivo)) == -1
```

- [ ] **Step 3: Correr con la clave** (Felipe graba los WAV con el PC y los copia a `pruebas/integracion/wav/`)

Run: `set -a; . .env; set +a; pytest -q pruebas/integracion`
Expected: las tres frases detectan; "Ana / mañana" y silencio no. Si "andia" sola dispara con "mañana", bajar su sensibilidad (0,3) en `servidor._app_real`.

- [ ] **Step 4: Commit** (los `.ppn` y WAV quedan fuera de git; `.gitignore` ya los excluye; añadir `pruebas/integracion/wav/` al `.gitignore`)

```bash
echo "pruebas/integracion/wav/" >> .gitignore && git add -A && git commit -q -m "Prueba de integración de Porcupine con las frases de ANDIA (se salta sin clave)"
```

---

### Task 12: Despliegue en la Pi y prueba de punta a punta con el PC

**Files:**
- Create: `~/andia/andia.service`, `~/andia/deploy.sh`
- Modify: `~/andia/README.md` (sección de despliegue y uso)

**Interfaces:**
- Produces: servicio `andia` en `http://192.168.x.x:8001` (`/salud`, `/ws`, `/avisar`); comando `andia "texto"` en WSL para avisar al reloj.

- [ ] **Step 1: Unidad systemd** (mismo patrón que `rowino.service`)

`~/andia/andia.service`:
```ini
[Unit]
Description=ANDIA - asistente de voz del reloj
After=network-online.target
Wants=network-online.target

[Service]
Type=simple
User=felipe
WorkingDirectory=/home/felipe/andia
EnvironmentFile=/home/felipe/andia/.env
ExecStart=/home/felipe/andia/venv/bin/uvicorn andia.servidor:app --host 0.0.0.0 --port 8001
Restart=always
RestartSec=5

[Install]
WantedBy=multi-user.target
```

- [ ] **Step 2: Script de despliegue**

`~/andia/deploy.sh`:
```bash
#!/usr/bin/env bash
# Despliega ANDIA en la Pi (coramohost): copia el repo, prepara el venv y (re)arranca el servicio.
# Primera vez: crea el .env en la Pi a mano (nunca se copia desde aquí) y ejecuta con --instalar.
set -euo pipefail
PI="felipe@192.168.x.x"
DEST="/home/felipe/andia"
rsync -rlptvz --delete --exclude venv --exclude datos --exclude .env --exclude __pycache__ --exclude .git \
      --exclude .pytest_cache "$HOME/andia/" "$PI:$DEST/"
ssh "$PI" "cd $DEST && ([ -d venv ] || python3 -m venv venv) && venv/bin/pip install -q -r requirements.txt"
if [ "${1:-}" = "--instalar" ]; then
  ssh -t "$PI" "sudo cp $DEST/andia.service /etc/systemd/system/andia.service && sudo systemctl daemon-reload && sudo systemctl enable --now andia"
else
  ssh -t "$PI" "sudo systemctl restart andia"
fi
sleep 3
echo "--- salud ---"; curl -s -m 5 "http://192.168.x.x:8001/salud"; echo
ssh "$PI" "journalctl -u andia -n 15 --no-pager"
```

- [ ] **Step 3: Primer despliegue**

```bash
chmod +x ~/andia/deploy.sh
# En la Pi, crear el .env copiando .env.example y rellenando las claves (Felipe, por ssh; nunca imprimirlas):
ssh -t felipe@192.168.x.x 'mkdir -p ~/andia && nano ~/andia/.env'
# Copiar los .ppn:
rsync -av ~/andia/palabras/ felipe@192.168.x.x:~/andia/palabras/
~/andia/deploy.sh --instalar
```
Expected: `/salud` → `{"ok": true, "relojes": 0}`; el journal muestra `[andia] frases de activación: [...]` y `Uvicorn running on http://0.0.0.0:8001`. Si `webrtcvad-wheels` no trae rueda aarch64 para Python 3.12: `ssh felipe@192.168.x.x 'cd ~/andia && venv/bin/pip install webrtcvad'` (compila con `python3-dev` y `build-essential`, que se instalan con apt).

- [ ] **Step 4: Prueba de punta a punta desde el PC** (Fish encendido y expuesto, Task 10)

Run en Windows: `py \\wsl.localhost\Ubuntu\home\felipe\andia\cliente_pc.py ws://192.168.x.x:8001/ws <ANDIA_TOKEN>`
1. Enter → "¿Qué hora es?" → `pregunta`, `respuesta` con `voz: true`, se oye a Mayuri.
2. `g` → "Oye Andia, ¿qué clima hay mañana en Ñuñoa?" → `activada` → `pensando` → respuesta con el clima.
3. `g` → hablar sin decir el nombre → a los 6 s `{"t":"fin","motivo":"sin_activacion"}`.
4. Enter → "ponme un temporizador de 2 minutos" → aparece `{"t":"temporizador","segundos":120}`.
5. Enter → "recuérdame en 1 minuto tomar agua" → `Programado`; al minuto llega `{"t":"aviso","texto":"tomar agua","origen":"recordatorio"}`.
6. Apagar el servidor Fish (`servidor.sh --parar`) → una pregunta más → `voz: false` y solo texto.
7. Desde WSL: `curl -s -X POST http://192.168.x.x:8001/avisar -H 'content-type: application/json' -d '{"texto":"terminó la compilación","token":"<ANDIA_TOKEN>","origen":"pc"}'` → el cliente imprime el `aviso`.
Anotar en el README la latencia observada (fin de frase → inicio de voz), objetivo < 4 s.

- [ ] **Step 5: Comando `andia` en WSL para avisar al reloj**

`~/.local/bin/andia`:
```bash
#!/usr/bin/env bash
# Manda un aviso al reloj a través de ANDIA. Uso: andia "texto"
set -euo pipefail
TOKEN=$(grep '^ANDIA_TOKEN=' "$HOME/andia/.env" | cut -d= -f2-)
curl -s -X POST http://192.168.x.x:8001/avisar -H 'content-type: application/json' \
     -d "$(python3 -c 'import json,sys; print(json.dumps({"texto": sys.argv[1], "token": sys.argv[2], "origen": "pc"}))' "$1" "$TOKEN")"
echo
```
`chmod +x ~/.local/bin/andia`; requiere un `~/andia/.env` local con al menos `ANDIA_TOKEN` (mismo valor que en la Pi).

- [ ] **Step 6: Completar el README** con: variables de `.env`, cómo entrenar las frases, `deploy.sh`, `cliente_pc.py`, `andia "texto"`, latencias medidas y problemas conocidos.

- [ ] **Step 7: Commit y anotar en el repo del reloj**

```bash
cd ~/andia && git add -A && git commit -q -m "Despliegue en la Pi (systemd, deploy.sh), comando andia para avisos, README"
cd ~/reloj_pulsera && sed -i 's/^> \*\*Estado 2026-09-03:\*\*.*/&\n> F3a (servicio ANDIA en la Pi) implementada según docs\/superpowers\/plans\/2026-09-03-andia-f3a-servicio-pi.md; repo ~\/andia./' docs/plan.md && git add -A && git commit -q -m "Plan: F3a hecha (servicio ANDIA en la Pi)"
```

---

## Verificación final de la fase

- `ANDIA_SIN_ARRANQUE=1 pytest -q` en WSL: toda la suite en verde.
- `pytest -q pruebas/integracion` con clave: Porcupine detecta las frases y rechaza "Ana / mañana".
- En la Pi: `systemctl status andia` activo; `/salud` responde; journal sin errores tras 10 consultas seguidas.
- Punta a punta con el PC como reloj: los 7 pasos del Step 4 de la Task 12, con latencia anotada.
- Con esto, F3b (firmware del reloj) parte de un servidor ya probado: solo cambia el cliente.
