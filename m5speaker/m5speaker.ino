/**
 * Web Radio Player for M5StickCPlus2 with SPK HAT
 *
 * Features:
 * - Stream MP3 internet radio stations
 * - FFT visualization on display
 * - Button controls: BtnA = next station, BtnB = volume down
 * - Hold BtnA = volume up
 *
 * Required libraries: M5Unified, ESP8266Audio
 */

// Set your WiFi credentials here
#define WIFI_SSID "Lokaal22"
#define WIFI_PASS "worlddominationviarats"

#include <WiFi.h>
#include <esp_system.h>
#include <AudioOutput.h>
#include <AudioFileSourceICYStream.h>
#include <AudioFileSourceBuffer.h>
#include <AudioGeneratorMP3.h>
#include <M5Unified.h>

// M5Speaker virtual channel (0-7)
static constexpr uint8_t m5spk_virtual_channel = 0;

// Web radio station list
static constexpr const char* station_list[][2] = {
  {"Jazz Stream",       "http://wbgo.streamguys.net/thejazzstream"},
  {"181 Beatles",       "http://listen.181fm.com/181-beatles_128k.mp3"},
  {"SomaFM Groove",     "http://ice1.somafm.com/illstreet-128-mp3"},
  {"SomaFM Boot",       "http://ice1.somafm.com/bootliquor-128-mp3"},
  {"SomaFM Drone",      "http://ice1.somafm.com/dronezone-128-mp3"},
  {"Classic FM",        "http://media-ice.musicradio.com:80/ClassicFMMP3"},
  {"Asia Dream",        "http://igor.torontocast.com:1025/;.-mp3"},
};
static constexpr const size_t stations = sizeof(station_list) / sizeof(station_list[0]);

// Custom AudioOutput class for M5Speaker
class AudioOutputM5Speaker : public AudioOutput {
public:
  AudioOutputM5Speaker(m5::Speaker_Class* m5sound, uint8_t virtual_sound_channel = 0) {
    _m5sound = m5sound;
    _virtual_ch = virtual_sound_channel;
  }

  virtual ~AudioOutputM5Speaker(void) {}

  virtual bool begin(void) override { return true; }

  virtual bool ConsumeSample(int16_t sample[2]) override {
    if (_tri_buffer_index < tri_buf_size) {
      _tri_buffer[_tri_index][_tri_buffer_index] = sample[0];
      _tri_buffer[_tri_index][_tri_buffer_index + 1] = sample[1];
      _tri_buffer_index += 2;
      return true;
    }
    flush();
    return false;
  }

  virtual void flush(void) override {
    if (_tri_buffer_index) {
      _m5sound->playRaw(_tri_buffer[_tri_index], _tri_buffer_index, hertz, true, 1, _virtual_ch);
      _tri_index = _tri_index < 2 ? _tri_index + 1 : 0;
      _tri_buffer_index = 0;
      ++_update_count;
    }
  }

  virtual bool stop(void) override {
    flush();
    _m5sound->stop(_virtual_ch);
    for (size_t i = 0; i < 3; ++i) {
      memset(_tri_buffer[i], 0, tri_buf_size * sizeof(int16_t));
    }
    ++_update_count;
    return true;
  }

  const int16_t* getBuffer(void) const { return _tri_buffer[(_tri_index + 2) % 3]; }
  const uint32_t getUpdateCount(void) const { return _update_count; }

protected:
  m5::Speaker_Class* _m5sound;
  uint8_t _virtual_ch;
  static constexpr size_t tri_buf_size = 640;
  int16_t _tri_buffer[3][tri_buf_size];
  size_t _tri_buffer_index = 0;
  size_t _tri_index = 0;
  size_t _update_count = 0;
};

// Simple FFT class for audio visualization
#define FFT_SIZE 256
class fft_t {
  float _wr[FFT_SIZE + 1];
  float _wi[FFT_SIZE + 1];
  float _fr[FFT_SIZE + 1];
  float _fi[FFT_SIZE + 1];
  uint16_t _br[FFT_SIZE + 1];
  size_t _ie;

public:
  fft_t(void) {
#ifndef M_PI
#define M_PI 3.141592653
#endif
    _ie = logf((float)FFT_SIZE) / log(2.0) + 0.5;
    static constexpr float omega = 2.0f * M_PI / FFT_SIZE;
    static constexpr int s4 = FFT_SIZE / 4;
    static constexpr int s2 = FFT_SIZE / 2;
    for (int i = 1; i < s4; ++i) {
      float f = cosf(omega * i);
      _wi[s4 + i] = f;
      _wi[s4 - i] = f;
      _wr[i] = f;
      _wr[s2 - i] = -f;
    }
    _wi[s4] = _wr[0] = 1;

    size_t je = 1;
    _br[0] = 0;
    _br[1] = FFT_SIZE / 2;
    for (size_t i = 0; i < _ie - 1; ++i) {
      _br[je << 1] = _br[je] >> 1;
      je = je << 1;
      for (size_t j = 1; j < je; ++j) {
        _br[je + j] = _br[je] + _br[j];
      }
    }
  }

  void exec(const int16_t* in) {
    memset(_fi, 0, sizeof(_fi));
    for (size_t j = 0; j < FFT_SIZE / 2; ++j) {
      float basej = 0.25 * (1.0 - _wr[j]);
      size_t r = FFT_SIZE - j - 1;
      // Han window + stereo to mono
      _fr[_br[j]] = basej * (in[j * 2] + in[j * 2 + 1]);
      _fr[_br[r]] = basej * (in[r * 2] + in[r * 2 + 1]);
    }

    size_t s = 1;
    size_t i = 0;
    do {
      size_t ke = s;
      s <<= 1;
      size_t je = FFT_SIZE / s;
      size_t j = 0;
      do {
        size_t k = 0;
        do {
          size_t l = s * j + k;
          size_t m = ke * (2 * j + 1) + k;
          size_t p = je * k;
          float Wxmr = _fr[m] * _wr[p] + _fi[m] * _wi[p];
          float Wxmi = _fi[m] * _wr[p] - _fr[m] * _wi[p];
          _fr[m] = _fr[l] - Wxmr;
          _fi[m] = _fi[l] - Wxmi;
          _fr[l] += Wxmr;
          _fi[l] += Wxmi;
        } while (++k < ke);
      } while (++j < je);
    } while (++i < _ie);
  }

  uint32_t get(size_t index) {
    return (index < FFT_SIZE / 2) ? (uint32_t)sqrtf(_fr[index] * _fr[index] + _fi[index] * _fi[index]) : 0u;
  }
};

// Global variables
static constexpr const int preallocateBufferSize = 5 * 1024;
static constexpr const int preallocateCodecSize = 29192;
static void* preallocateBuffer = nullptr;
static void* preallocateCodec = nullptr;

static AudioOutputM5Speaker out(&M5.Speaker, m5spk_virtual_channel);
static AudioGenerator* decoder = nullptr;
static AudioFileSourceICYStream* file = nullptr;
static AudioFileSourceBuffer* buff = nullptr;
static fft_t fft;

static size_t station_index = 0;
static char stream_title[128] = {0};
static volatile size_t playindex = ~0u;
static bool title_updated = false;

// FFT visualization data
static constexpr size_t WAVE_SIZE = 320;
static uint16_t prev_y[(FFT_SIZE / 2) + 1];
static uint16_t peak_y[(FFT_SIZE / 2) + 1];
static int16_t raw_data[WAVE_SIZE * 2];
static int header_height = 24;

// Metadata callback for stream title
static void MDCallback(void* cbData, const char* type, bool isUnicode, const char* string) {
  (void)cbData;
  if ((strcmp(type, "StreamTitle") == 0) && (strcmp(stream_title, string) != 0)) {
    strncpy(stream_title, string, sizeof(stream_title) - 1);
    stream_title[sizeof(stream_title) - 1] = '\0';
    title_updated = true;
  }
}

// Stop current playback
static void stop(void) {
  Serial.println("[Stop] Stopping playback...");
  if (decoder) {
    decoder->stop();
    delete decoder;
    decoder = nullptr;
  }
  if (buff) {
    buff->close();
    delete buff;
    buff = nullptr;
  }
  if (file) {
    file->close();
    delete file;
    file = nullptr;
  }
  out.stop();
  Serial.println("[Stop] Playback stopped");
}

// Request to play a station
static void play(size_t index) {
  playindex = index;
}

// Decode task running on separate core
static void decodeTask(void*) {
  Serial.println("[DecodeTask] Started on core " + String(xPortGetCoreID()));
  uint32_t loopCount = 0;

  for (;;) {
    // Yield periodically to prevent watchdog timeout
    if (++loopCount > 100) {
      loopCount = 0;
      vTaskDelay(1);
    }
    if (playindex != ~0u) {
      auto index = playindex;
      playindex = ~0u;

      Serial.printf("[DecodeTask] Playing station %d: %s\n", index, station_list[index][0]);
      Serial.printf("[DecodeTask] URL: %s\n", station_list[index][1]);
      Serial.printf("[DecodeTask] Free heap before: %d\n", ESP.getFreeHeap());

      stop();
      stream_title[0] = 0;
      title_updated = true;

      Serial.println("[DecodeTask] Creating ICY stream...");
      file = new AudioFileSourceICYStream(station_list[index][1]);
      if (!file) {
        Serial.println("[DecodeTask] ERROR: Failed to create ICY stream!");
        continue;
      }

      Serial.println("[DecodeTask] Registering metadata callback...");
      file->RegisterMetadataCB(MDCallback, (void*)"ICY");

      Serial.println("[DecodeTask] Creating buffer...");
      buff = new AudioFileSourceBuffer(file, preallocateBuffer, preallocateBufferSize);
      if (!buff) {
        Serial.println("[DecodeTask] ERROR: Failed to create buffer!");
        continue;
      }

      Serial.println("[DecodeTask] Creating MP3 decoder...");
      decoder = new AudioGeneratorMP3(preallocateCodec, preallocateCodecSize);
      if (!decoder) {
        Serial.println("[DecodeTask] ERROR: Failed to create decoder!");
        continue;
      }

      Serial.println("[DecodeTask] Starting decoder...");
      bool started = decoder->begin(buff, &out);
      Serial.printf("[DecodeTask] Decoder begin returned: %d\n", started);
      Serial.printf("[DecodeTask] Free heap after: %d\n", ESP.getFreeHeap());
    }

    if (decoder && decoder->isRunning()) {
      if (!decoder->loop()) {
        Serial.println("[DecodeTask] Decoder loop returned false, stopping");
        decoder->stop();
      }
    } else {
      // Yield when not actively decoding to prevent watchdog
      vTaskDelay(1);
    }
  }
}

// Get background color for FFT display
static uint32_t bgcolor(int y) {
  auto h = M5.Display.height();
  auto dh = h - header_height;
  int v = ((h - y) << 5) / dh;
  return M5.Display.color888(v + 2, v, v + 6);
}

// Initialize display for visualization
static void gfxSetup(void) {
  M5.Display.setRotation(1);
  M5.Display.setFont(&fonts::Font0);
  M5.Display.setTextSize(1);
  M5.Display.setTextWrap(false);
  M5.Display.fillScreen(TFT_BLACK);

  // Draw header
  M5.Display.setTextColor(TFT_CYAN);
  M5.Display.setCursor(4, 2);
  M5.Display.print("Web Radio");
  M5.Display.drawFastHLine(0, header_height - 2, M5.Display.width(), TFT_DARKGREY);

  // Initialize FFT background
  for (int y = header_height; y < M5.Display.height(); ++y) {
    M5.Display.drawFastHLine(0, y, M5.Display.width(), bgcolor(y));
  }

  for (int x = 0; x < (FFT_SIZE / 2) + 1; ++x) {
    prev_y[x] = INT16_MAX;
    peak_y[x] = INT16_MAX;
  }
}

// Update station/title display
static void updateHeader(void) {
  M5.Display.startWrite();

  // Station name
  M5.Display.fillRect(0, 2, M5.Display.width(), 10, TFT_BLACK);
  M5.Display.setTextColor(TFT_CYAN);
  M5.Display.setCursor(4, 2);
  M5.Display.print(station_list[station_index][0]);

  // Stream title (scrolling)
  M5.Display.fillRect(0, 12, M5.Display.width(), 10, TFT_BLACK);
  M5.Display.setTextColor(TFT_WHITE);
  M5.Display.setCursor(4, 12);
  if (stream_title[0]) {
    M5.Display.print(stream_title);
  } else {
    M5.Display.print("Connecting...");
  }

  M5.Display.endWrite();
}

// Draw FFT visualization
static void gfxLoop(void) {
  auto buf = out.getBuffer();
  if (!buf) return;

  memcpy(raw_data, buf, WAVE_SIZE * 2 * sizeof(int16_t));

  M5.Display.startWrite();

  // Draw stereo level meter (top bar)
  for (size_t i = 0; i < 2; ++i) {
    int32_t level = 0;
    for (size_t j = i; j < 640; j += 32) {
      uint32_t lv = abs(raw_data[j]);
      if (level < lv) level = lv;
    }
    int32_t x = (level * M5.Display.width()) / INT16_MAX;
    M5.Display.fillRect(0, header_height - 1, x, 1, TFT_GREEN);
    M5.Display.fillRect(x, header_height - 1, M5.Display.width() - x, 1, TFT_BLACK);
  }

  // Draw FFT bars
  fft.exec(raw_data);

  int32_t dsp_height = M5.Display.height();
  int32_t fft_height = dsp_height - header_height - 1;
  size_t bw = M5.Display.width() / 40;  // Bar width
  if (bw < 2) bw = 2;
  size_t xe = M5.Display.width() / bw;
  if (xe > (FFT_SIZE / 2)) xe = (FFT_SIZE / 2);

  uint32_t bar_color_low = 0x000033u;
  uint32_t bar_color_high = 0x99AAFFu;

  for (size_t bx = 0; bx < xe; ++bx) {
    size_t x = bx * bw;
    int32_t f = fft.get(bx);
    int32_t y = (f * fft_height) >> 18;
    if (y > fft_height) y = fft_height;
    y = dsp_height - y;

    int32_t py = prev_y[bx];
    if (y != py) {
      M5.Display.fillRect(x, y, bw - 1, py - y, (y < py) ? bar_color_high : bar_color_low);
      prev_y[bx] = y;
    }

    // Peak indicator
    int32_t pky = peak_y[bx] + 1;
    if (pky < y) {
      M5.Display.writeFastHLine(x, pky - 1, bw - 1, bgcolor(pky - 1));
      pky--;
    } else {
      pky = y - 1;
    }
    if (peak_y[bx] != pky) {
      peak_y[bx] = pky;
      M5.Display.writeFastHLine(x, pky, bw - 1, TFT_WHITE);
    }
  }

  M5.Display.endWrite();
}

// Draw volume bar
static void drawVolumeBar(void) {
  static int px = 0;
  uint8_t v = M5.Speaker.getVolume();
  int x = v * M5.Display.width() / 255;
  if (px != x) {
    M5.Display.fillRect(x, header_height - 2, px - x, 1, px < x ? TFT_YELLOW : TFT_DARKGREY);
    px = x;
  }
}

void setup(void) {
  auto cfg = M5.config();

  // Enable SPK HAT
  cfg.external_speaker.hat_spk = true;

  M5.begin(cfg);

  // Initialize Serial for debugging
  Serial.begin(115200);
  delay(100);
  Serial.println();
  Serial.println("=== Web Radio Starting ===");
  Serial.printf("Reset reason: %d\n", esp_reset_reason());
  Serial.printf("Free heap: %d bytes\n", ESP.getFreeHeap());
  Serial.printf("Chip model: %s, Rev: %d\n", ESP.getChipModel(), ESP.getChipRevision());

  // Allocate audio buffers
  Serial.println("Allocating audio buffers...");
  preallocateBuffer = malloc(preallocateBufferSize);
  preallocateCodec = malloc(preallocateCodecSize);
  if (!preallocateBuffer || !preallocateCodec) {
    Serial.println("ERROR: Memory allocation failed!");
    M5.Display.setRotation(1);
    M5.Display.fillScreen(TFT_RED);
    M5.Display.setTextColor(TFT_WHITE);
    M5.Display.setCursor(10, 50);
    M5.Display.print("Memory allocation failed!");
    for (;;) { M5.delay(1000); }
  }
  Serial.printf("Buffers allocated. Free heap: %d bytes\n", ESP.getFreeHeap());

  // Configure speaker
  Serial.println("Configuring speaker...");
  auto spk_cfg = M5.Speaker.config();
  spk_cfg.sample_rate = 48000;
  spk_cfg.task_pinned_core = APP_CPU_NUM;
  M5.Speaker.config(spk_cfg);
  M5.Speaker.begin();
  M5.Speaker.setVolume(128);
  Serial.printf("Speaker configured. Enabled: %d\n", M5.Speaker.isEnabled());

  // Setup display
  gfxSetup();

  // Connect to WiFi
  Serial.println("Connecting to WiFi...");
  Serial.printf("SSID: %s\n", WIFI_SSID);
  M5.Display.fillRect(0, 12, M5.Display.width(), 10, TFT_BLACK);
  M5.Display.setTextColor(TFT_YELLOW);
  M5.Display.setCursor(4, 12);
  M5.Display.print("WiFi connecting...");

  WiFi.disconnect();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  int dots = 0;
  while (WiFi.status() != WL_CONNECTED) {
    Serial.printf("WiFi status: %d\n", WiFi.status());
    M5.Display.print(".");
    if (++dots > 30) {
      M5.Display.fillRect(0, 12, M5.Display.width(), 10, TFT_BLACK);
      M5.Display.setCursor(4, 12);
      M5.Display.print("WiFi connecting...");
      dots = 0;
    }
    M5.delay(500);
  }

  Serial.println("WiFi connected!");
  Serial.printf("IP address: %s\n", WiFi.localIP().toString().c_str());
  M5.Display.fillRect(0, 12, M5.Display.width(), 10, TFT_BLACK);
  M5.Display.setTextColor(TFT_GREEN);
  M5.Display.setCursor(4, 12);
  M5.Display.print("Connected!");
  M5.delay(500);

  // Initialize FFT background
  for (int y = header_height; y < M5.Display.height(); ++y) {
    M5.Display.drawFastHLine(0, y, M5.Display.width(), bgcolor(y));
  }

  // Start playing first station
  Serial.println("Starting playback...");
  updateHeader();
  play(station_index);

  // Start decode task on APP CPU (CPU 1) to avoid watchdog issues on CPU 0
  Serial.println("Creating decode task...");
  xTaskCreatePinnedToCore(decodeTask, "decodeTask", 8192, nullptr, 1, nullptr, APP_CPU_NUM);
  Serial.println("Setup complete!");
}

void loop(void) {
  // Periodic status log
  static unsigned long lastLog = 0;
  if (millis() - lastLog > 5000) {
    lastLog = millis();
    Serial.printf("[Loop] Running. Decoder: %s, Heap: %d\n",
      (decoder && decoder->isRunning()) ? "playing" : "stopped",
      ESP.getFreeHeap());
  }

  // Update title if changed
  if (title_updated) {
    title_updated = false;
    updateHeader();
  }

  // Update FFT visualization
  gfxLoop();

  // Draw volume bar
  drawVolumeBar();

  // Frame rate control (~8ms cycle)
  {
    static int prev_frame;
    int frame;
    do {
      M5.delay(1);
    } while (prev_frame == (frame = millis() >> 3));
    prev_frame = frame;
  }

  M5.update();

  // Button feedback
  if (M5.BtnA.wasPressed()) {
    M5.Speaker.tone(440, 50);
  }

  // BtnA click: next station
  if (M5.BtnA.wasClicked()) {
    M5.Speaker.tone(1000, 100);
    if (++station_index >= stations) {
      station_index = 0;
    }
    updateHeader();
    play(station_index);
  }

  // BtnA double-click: previous station
  if (M5.BtnA.wasDecideClickCount()) {
    int cc = M5.BtnA.getClickCount();
    if (cc == 2) {
      M5.Speaker.tone(800, 100);
      if (station_index == 0) {
        station_index = stations;
      }
      play(--station_index);
      updateHeader();
    }
  }

  // Volume control: BtnA hold = up, BtnB = down
  if (M5.BtnA.isHolding() || M5.BtnB.isPressed()) {
    size_t v = M5.Speaker.getVolume();
    int add = M5.BtnB.isPressed() ? -2 : 2;
    v += add;
    if (v <= 255) {
      M5.Speaker.setVolume(v);
    }
  }
}
