#include <M5Unified.h>
#include <LittleFS.h>
#include <vector>
#include <string>
#include "AudioFileSourceLittleFS.h"
#include "AudioFileSourceID3.h"
#include "AudioGeneratorOpus.h"
#include "AudioOutputI2S.h"

// Audio components
AudioGeneratorOpus *opus;
AudioFileSourceLittleFS *file;
AudioFileSourceID3 *id3;
AudioOutputI2S *out;

// Browser state
enum BrowserMode { FOLDER_VIEW, FILE_VIEW };
BrowserMode currentMode = FOLDER_VIEW;

// Folder and file lists
std::vector<String> folders;
std::vector<String> soundFiles;
int currentFolderIndex = 0;
int currentFileIndex = 0;
String currentFolder = "";
bool isPlaying = false;

// Display settings
const int ITEM_HEIGHT = 28;
const int VISIBLE_ITEMS = 4;
const int HEADER_HEIGHT = 16;
const float TEXT_SIZE = 2.0;
int scrollOffset = 0;

// Color theme - Cyberpunk/Synthwave
const uint16_t COLOR_BG = 0x0000;        // Black
const uint16_t COLOR_HEADER = 0xF81F;    // Magenta
uint16_t currentSelectionBg = 0x4810;    // Dark purple (will be randomized)
uint16_t currentSelectionText = 0x07FF;  // Cyan (will be randomized)
const uint16_t COLOR_NORMAL_TEXT = 0xC618;  // Light gray
const uint16_t COLOR_PLAYING = 0x07E0;   // Green
const uint16_t COLOR_SCROLL = 0x8410;    // Gray

// Rainbow colors for selection - vibrant palette
const uint16_t RAINBOW_COLORS[] = {
    0xF800,  // Red
    0xFD20,  // Orange
    0xFFE0,  // Yellow
    0x07E0,  // Green
    0x07FF,  // Cyan
    0x001F,  // Blue
    0x780F,  // Purple
    0xF81F,  // Magenta
    0xFBE0,  // Gold
    0x87FF,  // Light cyan
    0xFC9F,  // Pink
    0xAFE5,  // Mint
};
const int RAINBOW_COLOR_COUNT = sizeof(RAINBOW_COLORS) / sizeof(RAINBOW_COLORS[0]);

// Function to get a darker version of a color for background
uint16_t getDarkerColor(uint16_t color) {
    // Extract RGB565 components
    uint8_t r = (color >> 11) & 0x1F;
    uint8_t g = (color >> 5) & 0x3F;
    uint8_t b = color & 0x1F;
    // Darken by shifting right (divide by 4)
    r = r >> 2;
    g = g >> 2;
    b = b >> 2;
    return (r << 11) | (g << 5) | b;
}

// Function to randomize selection colors
void randomizeSelectionColor() {
    int idx = random(RAINBOW_COLOR_COUNT);
    currentSelectionText = RAINBOW_COLORS[idx];
    currentSelectionBg = getDarkerColor(currentSelectionText);
}

// Text scrolling for long names
int textScrollOffset = 0;
unsigned long lastScrollTime = 0;
unsigned long selectionTime = 0; // When the current item was selected
bool scrollPausedAtEnd = false;
unsigned long scrollEndPauseTime = 0;
const unsigned long SCROLL_END_DELAY = 700; // Pause at start and end
const unsigned long SCROLL_DELAY = 80; // ms between scroll steps
const int CHAR_WIDTH = 12; // approximate width per character at TEXT_SIZE 2.0

// Volume control
float currentVolume = 0.5;
const float VOLUME_STEP = 0.05;
const unsigned long HOLD_THRESHOLD = 500; // ms to trigger hold
const unsigned long VOLUME_REPEAT_DELAY = 150; // ms between volume changes while holding
unsigned long lastVolumeChangeTime = 0;
bool volumeDisplayActive = false;
unsigned long volumeDisplayTimeout = 0;
bool btnAWasHeld = false; // Track if BtnA was used for volume
bool btnBWasHeld = false; // Track if BtnB was used for volume

// Helper to extract just the name from a full path
String getBaseName(const String& path) {
    int lastSlash = path.lastIndexOf('/');
    if (lastSlash >= 0) {
        return path.substring(lastSlash + 1);
    }
    return path;
}

// Function to scan folders in /data root
void scanFolders() {
    folders.clear();
    File root = LittleFS.open("/");
    if (!root || !root.isDirectory()) {
        return;
    }

    File entry = root.openNextFile();
    while (entry) {
        if (entry.isDirectory()) {
            String dirName = getBaseName(entry.name());
            // Skip hidden folders
            if (dirName.length() > 0 && !dirName.startsWith(".")) {
                folders.push_back(dirName);
            }
        }
        entry = root.openNextFile();
    }
}

// Function to scan files in selected folder
void scanFilesInFolder(const String& folderPath) {
    soundFiles.clear();
    File root = LittleFS.open("/" + folderPath);
    if (!root || !root.isDirectory()) {
        return;
    }

    File entry = root.openNextFile();
    while (entry) {
        if (!entry.isDirectory()) {
            String fileName = getBaseName(entry.name());
            if (fileName.endsWith(".opus") || fileName.endsWith(".OPUS")) {
                soundFiles.push_back(fileName);
            }
        }
        entry = root.openNextFile();
    }
}

// Function to get display name (remove extension)
String getDisplayName(const String& fileName) {
    String displayName = fileName;
    displayName.replace(".opus", "");
    displayName.replace(".OPUS", "");
    return displayName;
}

// Function to get scrolled text for display
String getScrolledText(const String& text, int maxChars, int scrollPos) {
    if (text.length() <= maxChars) {
        return text;
    }
    return text.substring(scrollPos, scrollPos + maxChars);
}

// Function to display folder browser
void displayFolderBrowser() {
    M5.Display.fillScreen(COLOR_BG);

    // Header
    M5.Display.setTextColor(COLOR_HEADER);
    M5.Display.setTextSize(1.5);
    M5.Display.setCursor(4, 2);
    M5.Display.print("/ ROOT");

    if (folders.size() == 0) {
        M5.Display.setTextColor(COLOR_HEADER);
        M5.Display.setCursor(4, 50);
        M5.Display.setTextSize(TEXT_SIZE);
        M5.Display.print("No folders");
        return;
    }

    // Calculate scroll offset to keep selection visible
    if (currentFolderIndex < scrollOffset) {
        scrollOffset = currentFolderIndex;
    } else if (currentFolderIndex >= scrollOffset + VISIBLE_ITEMS) {
        scrollOffset = currentFolderIndex - VISIBLE_ITEMS + 1;
    }

    // Display folder list
    int startY = HEADER_HEIGHT + 4;
    int folderCount = (int)folders.size();
    int maxChars = (M5.Display.width() - 20) / CHAR_WIDTH;

    M5.Display.setTextSize(TEXT_SIZE);

    for (int i = 0; i < VISIBLE_ITEMS && (scrollOffset + i) < folderCount; i++) {
        int idx = scrollOffset + i;
        int y = startY + i * ITEM_HEIGHT;

        String folderName = folders[idx];
        bool isSelected = (idx == currentFolderIndex);

        if (isSelected) {
            // Highlight selected item
            M5.Display.fillRect(0, y - 2, M5.Display.width(), ITEM_HEIGHT, currentSelectionBg);
            M5.Display.setTextColor(currentSelectionText);
            M5.Display.setCursor(4, y);

            // Scroll text if too long
            String displayText = getScrolledText(folderName, maxChars, textScrollOffset);
            M5.Display.print(displayText);
        } else {
            M5.Display.setTextColor(COLOR_NORMAL_TEXT);
            M5.Display.setCursor(4, y);

            // Truncate non-selected items
            String displayText = folderName.substring(0, maxChars);
            M5.Display.print(displayText);
        }
    }

    // Scroll indicators
    M5.Display.setTextSize(2);
    M5.Display.setTextColor(COLOR_SCROLL);
    if (scrollOffset > 0) {
        M5.Display.setCursor(M5.Display.width() - 16, startY);
        M5.Display.print("^");
    }
    if (scrollOffset + VISIBLE_ITEMS < folderCount) {
        M5.Display.setCursor(M5.Display.width() - 16, startY + (VISIBLE_ITEMS - 1) * ITEM_HEIGHT);
        M5.Display.print("v");
    }
}

// Function to display file browser
void displayFileBrowser() {
    M5.Display.fillScreen(COLOR_BG);

    // Header with current folder
    M5.Display.setTextColor(COLOR_HEADER);
    M5.Display.setTextSize(1.5);
    M5.Display.setCursor(4, 2);
    M5.Display.print("/ ");
    M5.Display.print(currentFolder);

    if (soundFiles.size() == 0) {
        M5.Display.setTextColor(COLOR_HEADER);
        M5.Display.setCursor(4, 50);
        M5.Display.setTextSize(TEXT_SIZE);
        M5.Display.print("Empty");
        return;
    }

    // Calculate scroll offset to keep selection visible
    if (currentFileIndex < scrollOffset) {
        scrollOffset = currentFileIndex;
    } else if (currentFileIndex >= scrollOffset + VISIBLE_ITEMS) {
        scrollOffset = currentFileIndex - VISIBLE_ITEMS + 1;
    }

    // Display file list
    int startY = HEADER_HEIGHT + 4;
    int fileCount = (int)soundFiles.size();
    int maxChars = (M5.Display.width() - 20) / CHAR_WIDTH;

    M5.Display.setTextSize(TEXT_SIZE);

    for (int i = 0; i < VISIBLE_ITEMS && (scrollOffset + i) < fileCount; i++) {
        int idx = scrollOffset + i;
        int y = startY + i * ITEM_HEIGHT;

        String fileName = getDisplayName(soundFiles[idx]);
        bool isSelected = (idx == currentFileIndex);

        if (isSelected) {
            // Highlight selected item
            M5.Display.fillRect(0, y - 2, M5.Display.width(), ITEM_HEIGHT, currentSelectionBg);
            M5.Display.setTextColor(currentSelectionText);
            M5.Display.setCursor(4, y);

            // Scroll text if too long
            String displayText = getScrolledText(fileName, maxChars, textScrollOffset);
            M5.Display.print(displayText);
        } else {
            M5.Display.setTextColor(COLOR_NORMAL_TEXT);
            M5.Display.setCursor(4, y);

            // Truncate non-selected items
            String displayText = fileName.substring(0, maxChars);
            M5.Display.print(displayText);
        }
    }

    // Scroll indicators
    M5.Display.setTextSize(2);
    M5.Display.setTextColor(COLOR_SCROLL);
    if (scrollOffset > 0) {
        M5.Display.setCursor(M5.Display.width() - 16, startY);
        M5.Display.print("^");
    }
    if (scrollOffset + VISIBLE_ITEMS < fileCount) {
        M5.Display.setCursor(M5.Display.width() - 16, startY + (VISIBLE_ITEMS - 1) * ITEM_HEIGHT);
        M5.Display.print("v");
    }

    // Playing indicator
    if (isPlaying) {
        M5.Display.fillRect(0, M5.Display.height() - 16, M5.Display.width(), 16, COLOR_PLAYING);
        M5.Display.setTextColor(COLOR_BG);
        M5.Display.setTextSize(1.5);
        M5.Display.setCursor(4, M5.Display.height() - 14);
        M5.Display.print("PLAYING");
    }
}

// Function to display volume overlay
void displayVolumeOverlay() {
    int barWidth = M5.Display.width() - 20;
    int barHeight = 14;
    int barX = 10;
    int barY = M5.Display.height() - 30;

    // Background
    M5.Display.fillRect(barX - 4, barY - 18, barWidth + 8, barHeight + 24, COLOR_BG);

    // Label
    M5.Display.setTextColor(COLOR_HEADER);
    M5.Display.setTextSize(1.5);
    M5.Display.setCursor(barX, barY - 16);
    M5.Display.printf("VOL %d%%", (int)(currentVolume * 100));

    // Bar outline
    M5.Display.drawRect(barX, barY, barWidth, barHeight, COLOR_HEADER);

    // Bar fill
    int fillWidth = (int)(barWidth * currentVolume);
    M5.Display.fillRect(barX + 1, barY + 1, fillWidth - 2, barHeight - 2, currentSelectionText);
}

// Function to adjust volume
void adjustVolume(float delta) {
    currentVolume += delta;
    if (currentVolume < 0.0) currentVolume = 0.0;
    if (currentVolume > 1.0) currentVolume = 1.0;
    out->SetGain(currentVolume);

    volumeDisplayActive = true;
    volumeDisplayTimeout = millis() + 1500; // Show for 1.5 seconds
    displayVolumeOverlay();
}

// Function to refresh display based on current mode
void refreshDisplay() {
    if (currentMode == FOLDER_VIEW) {
        displayFolderBrowser();
    } else {
        displayFileBrowser();
    }
}

// Function to play current sound
void playCurrentSound() {
    if (soundFiles.size() == 0) return;

    // Stop any currently playing sound
    if (isPlaying && opus->isRunning()) {
        opus->stop();
        delete file;
        delete id3;
    }

    String fullPath = "/" + currentFolder + "/" + soundFiles[currentFileIndex];

    file = new AudioFileSourceLittleFS(fullPath.c_str());
    id3 = new AudioFileSourceID3(file);
    opus->begin(id3, out);
    isPlaying = true;
    refreshDisplay();
}

// Function to enter a folder
void enterFolder() {
    if (folders.size() == 0) return;

    currentFolder = folders[currentFolderIndex];
    scanFilesInFolder(currentFolder);
    currentFileIndex = 0;
    scrollOffset = 0;
    textScrollOffset = 0;
    scrollPausedAtEnd = false;
    selectionTime = millis();
    randomizeSelectionColor();
    currentMode = FILE_VIEW;
    refreshDisplay();
}

// Function to go back to folder view
void goBack() {
    currentMode = FOLDER_VIEW;
    scrollOffset = 0;
    textScrollOffset = 0;
    scrollPausedAtEnd = false;
    selectionTime = millis();
    randomizeSelectionColor();
    refreshDisplay();
}

// Function to get current selected item text length
int getCurrentItemTextLength() {
    if (currentMode == FOLDER_VIEW && folders.size() > 0) {
        return folders[currentFolderIndex].length();
    } else if (currentMode == FILE_VIEW && soundFiles.size() > 0) {
        return getDisplayName(soundFiles[currentFileIndex]).length();
    }
    return 0;
}

// Function to redraw just the selected item text (no flicker)
void redrawSelectedItemText() {
    int maxChars = (M5.Display.width() - 20) / CHAR_WIDTH;
    int startY = HEADER_HEIGHT + 4;

    // Find which row the selected item is on
    int selectedIdx = (currentMode == FOLDER_VIEW) ? currentFolderIndex : currentFileIndex;
    int row = selectedIdx - scrollOffset;
    int y = startY + row * ITEM_HEIGHT;

    // Clear just the text area
    M5.Display.fillRect(4, y - 2, M5.Display.width() - 20, ITEM_HEIGHT, currentSelectionBg);

    // Redraw text
    M5.Display.setTextSize(TEXT_SIZE);
    M5.Display.setTextColor(currentSelectionText);
    M5.Display.setCursor(4, y);

    String text;
    if (currentMode == FOLDER_VIEW && folders.size() > 0) {
        text = folders[currentFolderIndex];
    } else if (currentMode == FILE_VIEW && soundFiles.size() > 0) {
        text = getDisplayName(soundFiles[currentFileIndex]);
    }

    String displayText = getScrolledText(text, maxChars, textScrollOffset);
    M5.Display.print(displayText);
}

// Function to update text scrolling
void updateTextScroll() {
    int maxChars = (M5.Display.width() - 20) / CHAR_WIDTH;
    int textLen = getCurrentItemTextLength();

    if (textLen <= maxChars) {
        textScrollOffset = 0;
        return;
    }

    unsigned long currentTime = millis();

    // Wait for initial delay before starting to scroll
    if (currentTime - selectionTime < SCROLL_END_DELAY) {
        return;
    }

    // Handle pause at end
    if (scrollPausedAtEnd) {
        if (currentTime - scrollEndPauseTime >= SCROLL_END_DELAY) {
            scrollPausedAtEnd = false;
            textScrollOffset = 0;
            selectionTime = millis(); // Reset for initial delay at start
            redrawSelectedItemText();
        }
        return;
    }

    if (currentTime - lastScrollTime >= SCROLL_DELAY) {
        lastScrollTime = currentTime;
        textScrollOffset++;

        // Pause at end before restarting
        if (textScrollOffset > textLen - maxChars) {
            scrollPausedAtEnd = true;
            scrollEndPauseTime = millis();
            return;
        }

        redrawSelectedItemText();
    }
}

void setup() {
    auto cfg = M5.config();
    M5.begin(cfg);

    M5.Display.setRotation(1);
    M5.Display.setBrightness(128);
    M5.Display.fillScreen(BLACK);
    M5.Display.setTextColor(WHITE);
    M5.Display.setTextSize(1);
    M5.Display.setCursor(10, 40);
    M5.Display.println("Initializing...");

    // Initialize LittleFS
    if (!LittleFS.begin(true)) {
        M5.Display.setCursor(10, 60);
        M5.Display.setTextColor(RED);
        M5.Display.println("LittleFS failed!");
        while (1) delay(100);
    }

    // Initialize audio output for SPK2 Hat
    out = new AudioOutputI2S();
    out->SetPinout(26, 0, 25); // BCLK=26, LRC=0, DOUT=25 for M5StickC Plus with SPK2 Hat
    out->SetGain(0.5);

    opus = new AudioGeneratorOpus();

    delay(500);

    // Scan for folders
    scanFolders();

    if (folders.size() == 0) {
        M5.Display.fillScreen(BLACK);
        M5.Display.setCursor(10, 40);
        M5.Display.setTextColor(RED);
        M5.Display.println("No folders found!");
        M5.Display.setCursor(10, 60);
        M5.Display.println("Add folders to /data");
        while (1) delay(100);
    }

    delay(500);
    randomSeed(analogRead(0) ^ millis()); // Seed random for color variety
    randomizeSelectionColor();
    selectionTime = millis();
    refreshDisplay();
}

void loop() {
    M5.update();
    unsigned long currentTime = millis();

    // Handle playback
    if (isPlaying && opus->isRunning()) {
        if (!opus->loop()) {
            opus->stop();
            isPlaying = false;
            refreshDisplay();
        }
    }

    // Clear volume display after timeout
    if (volumeDisplayActive && currentTime > volumeDisplayTimeout) {
        volumeDisplayActive = false;
        refreshDisplay();
    }

    // Update text scrolling for long names
    if (!volumeDisplayActive) {
        updateTextScroll();
    }

    // Volume control - hold BtnA for volume up, hold BtnB for volume down
    if (M5.BtnA.isHolding() && M5.BtnA.pressedFor(HOLD_THRESHOLD)) {
        btnAWasHeld = true;
        if (currentTime - lastVolumeChangeTime > VOLUME_REPEAT_DELAY) {
            adjustVolume(VOLUME_STEP);
            lastVolumeChangeTime = currentTime;
        }
    }

    if (M5.BtnB.isHolding() && M5.BtnB.pressedFor(HOLD_THRESHOLD)) {
        btnBWasHeld = true;
        if (currentTime - lastVolumeChangeTime > VOLUME_REPEAT_DELAY) {
            adjustVolume(-VOLUME_STEP);
            lastVolumeChangeTime = currentTime;
        }
    }

    // Reset hold flags on release
    if (M5.BtnA.wasReleased()) {
        if (!btnAWasHeld) {
            // Short press action on release
            if (currentMode == FOLDER_VIEW) {
                enterFolder();
            } else {
                playCurrentSound();
            }
        }
        btnAWasHeld = false;
    }

    if (M5.BtnB.wasReleased()) {
        if (!btnBWasHeld) {
            // Short press action on release
            if (currentMode == FOLDER_VIEW) {
                if (folders.size() > 0) {
                    currentFolderIndex = (currentFolderIndex - 1 + folders.size()) % folders.size();
                    textScrollOffset = 0;
                    scrollPausedAtEnd = false;
                    selectionTime = millis();
                    randomizeSelectionColor();
                    refreshDisplay();
                }
            } else {
                if (currentFileIndex == 0) {
                    goBack();
                } else {
                    currentFileIndex--;
                    textScrollOffset = 0;
                    scrollPausedAtEnd = false;
                    selectionTime = millis();
                    randomizeSelectionColor();
                    refreshDisplay();
                }
            }
        }
        btnBWasHeld = false;
    }

    if (currentMode == FOLDER_VIEW) {
        // BtnPWR: Next folder
        if (M5.BtnPWR.wasPressed() && folders.size() > 0) {
            currentFolderIndex = (currentFolderIndex + 1) % folders.size();
            textScrollOffset = 0;
            scrollPausedAtEnd = false;
            selectionTime = millis();
            randomizeSelectionColor();
            refreshDisplay();
        }
    } else {

        // BtnPWR: Next file
        if (M5.BtnPWR.wasPressed() && soundFiles.size() > 0) {
            currentFileIndex = (currentFileIndex + 1) % soundFiles.size();
            textScrollOffset = 0;
            scrollPausedAtEnd = false;
            selectionTime = millis();
            randomizeSelectionColor();
            refreshDisplay();
        }
    }

    delay(10);
}
