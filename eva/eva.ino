/**
 * EVA v3 - Voice Agent on M5StickC Plus2 + SPK2 Hat
 * Transport: WebSocket to pipecat_bot.py (single server, port 8765)
 * Audio out: raw 16kHz PCM streamed in real-time as mic records
 * Audio in:  raw 22050Hz PCM (no WAV header) ← server → Speaker.playRaw()
 *
 * Flow: press A → connect WS + start mic → stream chunks live →
 *       server VAD detects end of speech → server sends TTS PCM → play
 */

#include "secrets.h"

#define WS_HOST "192.168.1.15"
#define WS_PORT  8765
#define WS_PATH  "/ws"

#include <WiFi.h>
#include <M5Unified.h>
#include <WebSocketsClient.h>

// ============================================================
// Audio config
// ============================================================

static constexpr int    MIC_RATE     = 16000;
static constexpr int    TTS_RATE     = 22050;
static constexpr size_t MIC_CHUNK    = 1600;   // 100ms at 16kHz
static constexpr int    MAX_RECORD_S = 15;     // safety cutoff

// PSRAM TTS buffer: 8s at 22050 Hz 16-bit mono ≈ 352 KB
static constexpr size_t TTS_BUF_SIZE = TTS_RATE * 8 * sizeof(int16_t);

// ============================================================
// Buffers
// ============================================================

static uint8_t* tts_buf   = nullptr;
static size_t   tts_bytes = 0;

static void app_mem_init() {
    tts_buf = (uint8_t*)heap_caps_malloc(TTS_BUF_SIZE,
                                          MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!tts_buf)
        tts_buf = (uint8_t*)heap_caps_malloc(TTS_BUF_SIZE,
                                              MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!tts_buf) {
        Serial.println("[MEM] CRITICAL: tts_buf alloc failed");
        while (1) delay(1000);
    }
    Serial.printf("[MEM] tts_buf=%p\n", tts_buf);
}

// ============================================================
// WebSocket
// ============================================================

static WebSocketsClient ws;
static volatile bool    ws_connected  = false;
static volatile bool    ws_audio_done = false; // server closed → all TTS received

static void ws_event(WStype_t type, uint8_t* payload, size_t length) {
    switch (type) {
        case WStype_CONNECTED:
            ws_connected  = true;
            ws_audio_done = false;
            tts_bytes     = 0;
            Serial.println("[WS] connected");
            break;

        case WStype_DISCONNECTED:
            ws_connected  = false;
            ws_audio_done = true;
            Serial.println("[WS] disconnected");
            break;

        case WStype_BIN:
            if (length > 0 && tts_bytes + length <= TTS_BUF_SIZE) {
                memcpy(tts_buf + tts_bytes, payload, length);
                tts_bytes += length;
            }
            break;

        case WStype_TEXT:
            Serial.printf("[WS] text: %.*s\n", (int)length, payload);
            break;

        default:
            break;
    }
}

// ============================================================
// Chime
// ============================================================

static void play_chime() {
    M5.Speaker.begin();
    M5.Speaker.setVolume(80);
    M5.Speaker.tone(1109, 60); delay(80);
    M5.Speaker.tone(1519, 80); delay(100);
    M5.Speaker.stop();
    M5.Speaker.end();
}

// ============================================================
// stream_speak — connect, stream mic live, receive TTS, play
// ============================================================

static void stream_speak() {
    // --- Connect ---
    ws_connected  = false;
    ws_audio_done = false;
    tts_bytes     = 0;

    ws.begin(WS_HOST, WS_PORT, WS_PATH);
    ws.onEvent(ws_event);
    ws.setReconnectInterval(0);

    term_print("[WS] connecting...");
    unsigned long t0 = millis();
    while (!ws_connected && millis() - t0 < 3000) {
        ws.loop();
        delay(5);
    }
    if (!ws_connected) {
        term_print("[WS] connect failed");
        ws.disconnect();
        return;
    }

    // --- Start mic ---
    M5.Speaker.end();
    delay(30);
    M5.Mic.begin();
    term_print("[MIC] streaming...");
    term_show_prompt("> speak now");

    // --- Stream mic chunks live until server disconnects or max time ---
    static int16_t chunk[MIC_CHUNK];
    unsigned long deadline = millis() + MAX_RECORD_S * 1000UL;

    while (!ws_audio_done && millis() < deadline) {
        ws.loop();

        // Check for cancel (Button A while streaming)
        M5.update();
        if (M5.BtnA.wasPressed()) {
            term_print("[MIC] cancelled");
            break;
        }

        if (M5.Mic.record(chunk, MIC_CHUNK, MIC_RATE)) {
            ws.sendBIN((uint8_t*)chunk, MIC_CHUNK * sizeof(int16_t));
        }
    }

    M5.Mic.end();

    // --- If server hasn't closed yet, wait for remaining TTS ---
    if (!ws_audio_done) {
        term_print("[WS] waiting for TTS...");
        deadline = millis() + 20000;
        while (!ws_audio_done && millis() < deadline) {
            ws.loop();
            delay(5);
        }
    }

    ws.disconnect();
    Serial.printf("[WS] received %u bytes of TTS PCM\n", (unsigned)tts_bytes);

    if (tts_bytes == 0) {
        term_print("[TTS] no audio");
        return;
    }

    // --- Play ---
    term_print("[TTS] speaking...");
    M5.Speaker.begin();
    M5.Speaker.setVolume(200);
    M5.Speaker.playRaw((const int16_t*)tts_buf, tts_bytes / 2, TTS_RATE, false, 1, 0);
    while (M5.Speaker.isPlaying()) {
        M5.update();
        delay(10);
    }
    M5.Speaker.end();
}

// ============================================================
// State machine
// ============================================================

enum State { IDLE, STREAMING };
static State state = IDLE;

void setup() {
    auto cfg = M5.config();
    cfg.external_speaker.hat_spk2 = true;
    cfg.internal_mic = true;
    M5.begin(cfg);

    Serial.begin(115200);
    delay(2000);
    Serial.println("\n--- EVA v3 STARTUP ---");

    M5.Display.setRotation(1);
    M5.Display.fillScreen(TFT_BLACK);

    app_mem_init();

    auto mic_cfg = M5.Mic.config();
    mic_cfg.sample_rate   = MIC_RATE;
    mic_cfg.dma_buf_len   = MIC_CHUNK;
    mic_cfg.dma_buf_count = 2;
    M5.Mic.config(mic_cfg);

    play_chime();
    startup_animation();

    term_init();
    term_print("[SYS] EVA v3.0");

    WiFi.begin(WIFI_SSID, WIFI_PASS);
    term_print("[WIFI] connecting...");
    while (WiFi.status() != WL_CONNECTED) delay(500);
    term_printf("[WIFI] %s", WiFi.localIP().toString().c_str());

    term_show_prompt("> press A to talk");
}

void loop() {
    M5.update();

    if (M5.BtnA.wasPressed() && state == IDLE) {
        state = STREAMING;
        stream_speak();
        state = IDLE;
        term_show_prompt("> press A to talk");
    }

    delay(1);
}
