# ElevenLabs Conversational AI + ESP32 Audio over WebSocket - Reference

## 1. ElevenLabs Conversational AI WebSocket Protocol

### Endpoint

```
wss://api.elevenlabs.io/v1/convai/conversation?agent_id={AGENT_ID}
```

### Authentication

**Method A - Public agent (no auth):** Include `agent_id` in query string. Only works for agents configured as public.

**Method B - Signed URL (recommended for private agents):** Your backend calls:
```
GET https://api.elevenlabs.io/v1/convai/conversation/get-signed-url?agent_id={AGENT_ID}
Header: xi-api-key: {YOUR_API_KEY}
```
Returns `{ "signed_url": "wss://..." }`. Expires in 15 minutes but active connections are not dropped.

**Method C - Direct API key header (best for ESP32):** Pass `xi-api-key: {API_KEY}` as a custom HTTP upgrade header. Not possible from browsers but works from embedded systems and server code.

---

## 2. Initialization Handshake

### Step 1 - Client opens connection

### Step 2 - Client sends `conversation_initiation_client_data` immediately after connect:

```json
{
  "type": "conversation_initiation_client_data",
  "conversation_config_override": {
    "agent": {
      "prompt": { "prompt": "You are a helpful assistant." },
      "first_message": "Hello! How can I help you?",
      "language": "en"
    },
    "tts": {
      "voice_id": "21m00Tcm4TlvDq8ikWAM"
    }
  },
  "dynamic_variables": {
    "user_name": "John"
  }
}
```
All `conversation_config_override` fields are optional - omitting them uses the agent's dashboard defaults.

### Step 3 - Server responds with `conversation_initiation_metadata`:

```json
{
  "type": "conversation_initiation_metadata",
  "conversation_initiation_metadata_event": {
    "conversation_id": "conv_abc123",
    "agent_output_audio_format": "pcm_16000",
    "user_input_audio_format": "pcm_16000"
  }
}
```

### Step 4 - Bidirectional audio streaming begins

---

## 3. Audio Format

### Input (microphone: ESP32 → ElevenLabs)
- Encoding: 16-bit signed PCM, little-endian
- Sample rate: 16,000 Hz
- Channels: Mono
- Chunk size: 4,000 samples = 8,000 bytes = 250ms per chunk
- Transmission: Base64-encoded, inside JSON

**Input message format** (note: NO `type` field, identified by key presence):
```json
{ "user_audio_chunk": "<base64-encoded-PCM-bytes>" }
```

### Output (TTS audio: ElevenLabs → ESP32)
- Default: 16-bit signed PCM, little-endian, Mono
- Available sample rates: `pcm_8000`, `pcm_16000`, `pcm_22050`, `pcm_24000`, `pcm_44100`, `pcm_48000`, `ulaw_8000`
- **Must be configured in the agent dashboard** (Voice tab → TTS output format) to get PCM; otherwise the default may be MP3
- Transmission: Base64-encoded inside JSON text frames (NOT raw binary WebSocket frames)

**Output message format:**
```json
{
  "type": "audio",
  "audio_event": {
    "audio_base_64": "<base64-encoded-PCM-bytes>",
    "event_id": 42
  }
}
```

---

## 4. All Message Types

### Client → Server

| Message Type | Description |
|---|---|
| `conversation_initiation_client_data` | Session config/overrides, sent immediately on connect |
| `user_audio_chunk` | Continuous microphone audio (base64 PCM, no `type` key) |
| `user_message` | Text input (can interrupt agent speech) |
| `user_activity` | Ping to prevent session timeout |
| `pong` | Response to server `ping` |
| `client_tool_result` | Result of a client-side tool call |
| `contextual_update` | Non-interrupting context update |

### Server → Client

| Message Type | Description |
|---|---|
| `conversation_initiation_metadata` | Session ID + negotiated audio formats |
| `audio` | TTS audio chunks (base64 PCM) |
| `agent_response` | Full agent text response |
| `user_transcript` | Transcribed user speech |
| `interruption` | Stop playback immediately |
| `ping` | Latency measurement request |
| `vad_score` | Voice activity detection confidence |
| `client_tool_call` | Request to execute a client-side tool |

### Key server messages

**Interruption:**
```json
{
  "type": "interruption",
  "interruption_event": { "event_id": 42 }
}
```
On receiving: immediately flush the audio output buffer. Discard any queued audio with `event_id` ≤ the interrupt's `event_id`.

**Ping/Pong:**
```json
// Server sends:
{ "type": "ping", "ping_event": { "event_id": 12345, "ping_ms": 50 } }

// Client must reply:
{ "type": "pong", "event_id": 12345 }
```

**User transcript:**
```json
{ "type": "user_transcript", "user_transcription_event": { "user_transcript": "What is the weather?" } }
```

### WebSocket close codes

| Code | Meaning |
|---|---|
| 1000 | Normal closure (agent ended conversation, max duration reached) |
| 1002 | Protocol error (timeout, safety violation, ASR/LLM/TTS failure) |
| 1008 | Policy violation (invalid messages, auth errors) |
| 1011 | Internal server error |

---

## 5. ESP32 WebSocket Library

**Links2004/arduinoWebSockets** is the recommended library.

Reasons over alternatives:
- Mature WSS (TLS) support via `WiFiClientSecure`
- `webSocket.beginSSL()` for `wss://` connections
- Custom header support for `xi-api-key`
- Fragment event types for large frames
- `WEBSOCKETS_MAX_DATA_SIZE` compile-time cap (default 15KB) - override if needed

```cpp
#include <WebSocketsClient.h>

WebSocketsClient webSocket;

void webSocketEvent(WStype_t type, uint8_t* payload, size_t length) {
    switch(type) {
        case WStype_DISCONNECTED:        break;
        case WStype_CONNECTED:           break;
        case WStype_TEXT:                /* JSON messages arrive here */ break;
        case WStype_BIN:                 /* raw binary frames (not used by ElevenLabs) */ break;
        case WStype_ERROR:               break;
        case WStype_FRAGMENT_TEXT_START:
        case WStype_FRAGMENT_BIN_START:
        case WStype_FRAGMENT:
        case WStype_FRAGMENT_FIN:        break;
    }
}

void setup() {
    // Option A: public agent (no auth header needed)
    webSocket.beginSSL("api.elevenlabs.io", 443,
                       "/v1/convai/conversation?agent_id=YOUR_AGENT_ID");

    // Option B: authenticated with API key header
    webSocket.setExtraHeaders("xi-api-key: YOUR_API_KEY");
    webSocket.beginSSL("api.elevenlabs.io", 443,
                       "/v1/convai/conversation?agent_id=YOUR_AGENT_ID");

    webSocket.onEvent(webSocketEvent);
}

void loop() {
    webSocket.loop();
    delay(1);  // yield to WiFi tasks on Core 0
}
```

Override `WEBSOCKETS_MAX_DATA_SIZE` in `platformio.ini` if frames exceed 15KB:
```ini
build_flags = -D WEBSOCKETS_MAX_DATA_SIZE=65536
```

---

## 6. Base64 Decoding on ESP32

Use `mbedtls_base64_decode()` - built into the ESP32 Arduino core, no extra library:

```cpp
#include <mbedtls/base64.h>

// Pre-allocate decode buffer (avoid per-frame heap alloc/free)
static uint8_t work_buf[65536];

size_t out_len = 0;
int ret = mbedtls_base64_decode(
    work_buf,
    sizeof(work_buf),
    &out_len,
    (const unsigned char*)b64_ptr,
    b64_len
);
// ret == 0 on success
// work_buf now contains raw PCM bytes
// out_len / 2 = number of int16_t samples
```

Avoid the `crypto/base64.h` alternative - it heap-allocates the decoded buffer requiring a `free()` call, fragmenting memory during rapid audio streaming.

---

## 7. I2S Audio Output

### M5Unified Speaker API (M5StickC Plus2 + SPK2 Hat)

```cpp
#include <M5Unified.h>

M5.Speaker.begin();
M5.Speaker.setVolume(180);  // 0-255

// Non-blocking enqueue (returns immediately):
M5.Speaker.playRaw(samples, count, sample_rate, /*stereo=*/false, /*repeat=*/1, /*channel=*/0);

// Check if still playing:
bool playing = M5.Speaker.isPlaying(0) > 0;
```

### Raw ESP-IDF I2S (external DAC, e.g. MAX98357A)

Uses the ESP-IDF v5 / Arduino-ESP32 v3.x `i2s_std` API:

```cpp
#include "driver/i2s_std.h"

i2s_chan_handle_t tx_handle;

// Create channel:
i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
i2s_new_channel(&chan_cfg, &tx_handle, NULL);

// Configure standard mode (16-bit mono, Philips format):
i2s_std_config_t std_cfg = {
    .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(16000),  // sample rate
    .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(
                    I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
    .gpio_cfg = {
        .mclk = I2S_GPIO_UNUSED,
        .bclk = GPIO_NUM_26,  // MAX98357A BCLK
        .ws   = GPIO_NUM_25,  // MAX98357A LRC
        .dout = GPIO_NUM_22,  // MAX98357A DIN
        .din  = I2S_GPIO_UNUSED,
    },
};
i2s_channel_init_std_mode(tx_handle, &std_cfg);
i2s_channel_enable(tx_handle);

// Write PCM data:
size_t bytes_written;
i2s_channel_write(tx_handle, pcm_buf, byte_count, &bytes_written, portMAX_DELAY);
```

---

## 8. Buffering Strategy

### Ring Buffer Pattern

```cpp
static constexpr size_t RING_SIZE   = 16384;  // samples (~2s at 8kHz, ~1s at 16kHz)
static constexpr size_t CHUNK_SIZE  = 1024;   // samples per I2S write

// Must be in INTERNAL RAM (not PSRAM) for I2S DMA access:
static int16_t* ring = nullptr;

void spk_init() {
    ring = (int16_t*)heap_caps_malloc(
        RING_SIZE * sizeof(int16_t),
        MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT
    );
    // ... initialize head/tail/count
}

// Producer (WebSocket callback):
void spk_push(const int16_t* samples, size_t count) {
    // write samples into ring, update tail
}

// Consumer (main loop / audio task):
void spk_drain() {
    // read CHUNK_SIZE samples from ring, feed to M5.Speaker.playRaw or i2s_channel_write
}
```

**Critical:** The I2S DMA controller on ESP32 **cannot access PSRAM**. Buffers used for DMA writes must be in internal SRAM. Use `MALLOC_CAP_INTERNAL`. Large staging buffers for network data (base64 decode scratch space) can safely use PSRAM:

```cpp
// Base64 decode buffer: large, safe to put in PSRAM
uint8_t* work_buf = (uint8_t*)heap_caps_malloc(
    65536,
    MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT
);

// PCM playback ring buffer: must be internal SRAM for I2S DMA
int16_t* ring = (int16_t*)heap_caps_malloc(
    RING_SIZE * sizeof(int16_t),
    MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT
);
```

### FreeRTOS Queue Pattern (multi-core)

```cpp
#define AUDIO_CHUNK_SAMPLES 1024
#define QUEUE_DEPTH         8

typedef struct {
    int16_t samples[AUDIO_CHUNK_SAMPLES];
    size_t count;
} AudioChunk;

QueueHandle_t audioQueue = xQueueCreate(QUEUE_DEPTH, sizeof(AudioChunk));

// Producer (Core 0 - WiFi/WebSocket):
AudioChunk chunk;
memcpy(chunk.samples, decoded_pcm, num_samples * sizeof(int16_t));
chunk.count = num_samples;
xQueueSend(audioQueue, &chunk, pdMS_TO_TICKS(0));  // 0ms timeout = drop if full

// Consumer task (Core 1 - I2S):
void audioTask(void*) {
    AudioChunk chunk;
    while (true) {
        xQueueReceive(audioQueue, &chunk, portMAX_DELAY);
        i2s_channel_write(tx_handle, chunk.samples,
                          chunk.count * 2, &written, portMAX_DELAY);
    }
}

// Pin audio task to Core 1 (away from WiFi on Core 0):
xTaskCreatePinnedToCore(audioTask, "audio", 4096, NULL,
                        17,    // priority below lwIP (18)
                        NULL, 1);
```

---

## 9. Performance and Reliability

### Core assignment
WiFi/lwIP runs on Core 0 at priority 18. Audio competing on Core 0 will stutter. Pin the audio consumer task to **Core 1 at priority 17**.

### yield in main loop
`delay(1)` in the `loop()` function is correct and necessary - it yields execution to WiFi tasks on Core 0. Without it, watchdog triggers and WebSocket drops occur.

### Heap monitoring
Keep ≥20KB free heap at all times. Below this, WebSocket library mallocs fail silently:
```cpp
Serial.printf("Free heap: %u bytes\n", ESP.getFreeHeap());
Serial.printf("Free PSRAM: %u bytes\n", ESP.getFreePsram());
```

### Buffer sizing (at 16kHz, 16-bit mono)
- 1 sample = 2 bytes
- 16,000 samples = 1 second = 32,000 bytes
- `RING_SIZE = 16384` samples ≈ 1 second of audio - adequate for most WiFi conditions
- Increase to 32768 samples if underruns occur on slow/congested WiFi

### WEBSOCKETS_MAX_DATA_SIZE
Default is 15KB. ElevenLabs JSON audio frames are typically 1–4KB (base64 of ~250ms PCM chunk), safely within limit. Override only if receiving unusually large frames:
```ini
# platformio.ini
build_flags = -D WEBSOCKETS_MAX_DATA_SIZE=65536
```

### PSRAM availability check
```cpp
if (psramFound()) {
    Serial.printf("PSRAM: %u bytes\n", ESP.getPsramSize());
} else {
    // Reduce work_buf and ring sizes
}
```

---

## 10. Minimal Pseudocode Flow (ESP32)

```cpp
// 1. Connect with auth
webSocket.setExtraHeaders("xi-api-key: YOUR_KEY");
webSocket.beginSSL("api.elevenlabs.io", 443,
                   "/v1/convai/conversation?agent_id=YOUR_ID");

// 2. On WStype_CONNECTED, send initiation message
webSocket.sendTXT("{\"type\":\"conversation_initiation_client_data\","
                  "\"conversation_config_override\":{}}");

// 3. On WStype_TEXT, parse type field:
//    "conversation_initiation_metadata" -> confirm audio format (pcm_16000)
//    "audio"       -> base64-decode audio_event.audio_base_64, push to I2S
//    "interruption"-> flush audio output buffer immediately
//    "ping"        -> reply { "type": "pong", "event_id": N }

// 4. In capture loop: every 250ms
//    - read 4000 samples (8000 bytes) from I2S microphone
//    - base64-encode
//    - send {"user_audio_chunk": "<base64>"}
```

---

## 11. Known Working Architectures

### Direct ESP32 ↔ ElevenLabs (simpler, limited)
- ESP32 connects directly via WSS to `api.elevenlabs.io`
- Audio: 16kHz PCM (mic input) / 16kHz PCM or 8kHz PCM (speaker output)
- Constraints: base64 encoding overhead, JSON parsing overhead, single-core WiFi contention

### Proxy/Relay Architecture (ElatoAI approach)
- ESP32 → Deno edge server (Opus 12kbps/24kHz over WSS) → ElevenLabs API
- Proxy transcodes between compressed Opus (for the bandwidth-constrained ESP32 link) and raw PCM (for ElevenLabs)
- More complex to deploy but lower bandwidth and better audio quality on the ESP32 link

---

## 12. References

- [ElevenLabs Agent WebSockets Documentation](https://elevenlabs.io/docs/agents-platform/api-reference/agents-platform/websocket)
- [ElevenLabs WebSocket Libraries Documentation](https://elevenlabs.io/docs/agents-platform/libraries/web-sockets)
- [ElevenLabs Get Signed URL](https://elevenlabs.io/docs/agents-platform/api-reference/conversations/get-signed-url)
- [ElevenLabs Agent Authentication](https://elevenlabs.io/docs/agents-platform/customization/authentication)
- [ElatoAI - ESP32 AI Voice Agents (GitHub)](https://github.com/akdeb/ElatoAI)
- [Links2004/arduinoWebSockets](https://github.com/Links2004/arduinoWebSockets)
- [Links2004 WebSocketClient ESP32 Example](https://github.com/Links2004/arduinoWebSockets/blob/master/examples/esp32/WebSocketClient/WebSocketClient.ino)
- [pschatzmann/arduino-audio-tools Discussion #1743 (WebSocket audio)](https://github.com/pschatzmann/arduino-audio-tools/discussions/1743)
- [schreibfaul1/ESP32-audioI2S](https://github.com/schreibfaul1/ESP32-audioI2S)
- [Espressif I2S API Reference](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/peripherals/i2s.html)
- [Espressif Audio Development Framework Design Considerations](https://docs.espressif.com/projects/esp-adf/en/latest/design-guide/design-considerations.html)
- [freeswitch-elevenlabs-bridge (GitHub)](https://github.com/os11k/freeswitch-elevenlabs-bridge)
- [ElevenLabs Python SDK - conversation.py](https://github.com/elevenlabs/elevenlabs-python/blob/main/src/elevenlabs/conversational_ai/conversation.py)
