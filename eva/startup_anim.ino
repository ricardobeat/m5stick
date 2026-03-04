/**
 * EVA Startup Animation
 * EVE-style eyes (Wall-E): dark blue with scanlines, open from black, blink twice.
 * Display: 240x135 landscape
 */

#define EYE_CX1   80
#define EYE_CX2  160
#define EYE_CY    55
#define EYE_RX    30
#define EYE_RY    24

// Dark navy base, mid blue fill, bright scanline highlight
#define EYE_COL_BG    M5.Display.color565(10,  30,  90)
#define EYE_COL_MID   M5.Display.color565(30,  80, 180)
#define EYE_COL_SCAN  M5.Display.color565(60, 120, 220)
#define EYE_COL_DIM   M5.Display.color565( 5,  15,  50)

static void draw_scanline_eye(int cx, int cy, int rx, int ry) {
    // Base fill
    M5.Display.fillEllipse(cx, cy, rx, ry, EYE_COL_BG);
    // Scanlines: alternating mid/scan rows inside ellipse bounding box
    for (int dy = -ry + 1; dy <= ry - 1; dy += 2) {
        // Compute half-width of ellipse at this row
        float t = 1.0f - ((float)(dy * dy)) / ((float)(ry * ry));
        if (t < 0) continue;
        int hw = (int)(rx * sqrtf(t));
        uint16_t col = ((dy & 2) == 0) ? EYE_COL_SCAN : EYE_COL_MID;
        M5.Display.drawFastHLine(cx - hw, cy + dy, hw * 2, col);
    }
}

// Draw eye with lids: close_amt=0 fully open, close_amt=ry fully shut
static void draw_eye_lidded(int cx, int cy, int rx, int ry, int close_amt) {
    draw_scanline_eye(cx, cy, rx, ry);
    if (close_amt > 0) {
        // Upper lid
        M5.Display.fillRect(cx - rx - 1, cy - ry - 1, rx * 2 + 2, close_amt + 1, TFT_BLACK);
        // Lower lid
        M5.Display.fillRect(cx - rx - 1, cy + ry - close_amt, rx * 2 + 2, close_amt + 2, TFT_BLACK);
    }
}

static void draw_both_lidded(int close_amt) {
    draw_eye_lidded(EYE_CX1, EYE_CY, EYE_RX, EYE_RY, close_amt);
    draw_eye_lidded(EYE_CX2, EYE_CY, EYE_RX, EYE_RY, close_amt);
}

static void anim_blink() {
    // Close
    for (int c = 0; c <= EYE_RY; c += 4) {
        draw_both_lidded(c);
        delay(14);
    }
    delay(50);
    // Open
    for (int c = EYE_RY; c >= 0; c -= 4) {
        draw_both_lidded(c);
        delay(14);
    }
}

void startup_animation() {
    M5.Display.fillScreen(TFT_BLACK);

    // Open from fully closed over ~700ms
    for (int c = EYE_RY; c >= 0; c--) {
        draw_both_lidded(c);
        delay(700 / (EYE_RY + 1));
    }

    delay(500);
    anim_blink();
    delay(400);
    anim_blink();
    delay(300);

    // Fade out: dim eyes then erase
    draw_both_lidded(0);
    // Overdraw with dim color
    M5.Display.fillEllipse(EYE_CX1, EYE_CY, EYE_RX, EYE_RY, EYE_COL_DIM);
    M5.Display.fillEllipse(EYE_CX2, EYE_CY, EYE_RX, EYE_RY, EYE_COL_DIM);
    delay(150);
    M5.Display.fillEllipse(EYE_CX1, EYE_CY, EYE_RX, EYE_RY, TFT_BLACK);
    M5.Display.fillEllipse(EYE_CX2, EYE_CY, EYE_RX, EYE_RY, TFT_BLACK);
}
