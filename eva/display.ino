/**
 * EVA Terminal Display
 * EVE from WALL-E palette: white/cyan/blue with occasional green.
 * textSize(1): 6x8px chars, 40 chars/line, 11 scroll lines + prompt.
 */

// EVE colour palette
#define TERM_BG         TFT_BLACK
#define TERM_WHITE      M5.Display.color565(220, 235, 255)  // cool white (newest)
#define TERM_CYAN       M5.Display.color565( 80, 210, 230)  // EVE blue-cyan (recent)
#define TERM_BLUE       M5.Display.color565( 60, 120, 200)  // mid blue
#define TERM_BLUE_DIM   M5.Display.color565( 30,  60, 110)  // dim blue (oldest)
#define TERM_GREEN      M5.Display.color565( 60, 200, 100)  // occasional green accent
#define TERM_PROMPT_COL M5.Display.color565(200, 240, 255)  // bright white-blue for prompt

#define TERM_FONT_SIZE  1
#define TERM_CHAR_W     6
#define TERM_CHAR_H     8
#define TERM_LINE_H     12  // line height with 4px gap
#define TERM_LINES      10
#define TERM_CHARS      40
#define TERM_SCROLL_Y   0
#define TERM_SCROLL_H   (TERM_LINES * TERM_LINE_H)  // 120px
#define TERM_PROMPT_Y   122

static char term_buf[TERM_LINES][TERM_CHARS + 1];
static int  term_head = 0;

// Pick colour for line i (0=oldest, TERM_LINES-1=newest)
static uint16_t term_line_color(int i) {
    int age = TERM_LINES - 1 - i; // 0=newest
    if (age == 0) return TERM_WHITE;
    if (age == 1) return TERM_CYAN;
    if (age == 2) return TERM_BLUE;
    if (age % 5 == 0) return TERM_GREEN;
    return TERM_BLUE_DIM;
}

static void term_redraw() {
    M5.Display.fillRect(0, TERM_SCROLL_Y, 240, TERM_SCROLL_H, TERM_BG);
    M5.Display.setTextSize(TERM_FONT_SIZE);
    for (int i = 0; i < TERM_LINES; i++) {
        int idx = (term_head + i) % TERM_LINES;
        if (term_buf[idx][0] == '\0') continue;
        int y = TERM_SCROLL_Y + i * TERM_LINE_H;
        M5.Display.setTextColor(term_line_color(i));
        M5.Display.setCursor(2, y);
        M5.Display.print(term_buf[idx]);
    }
}

void term_print(const char* msg) {
    strncpy(term_buf[term_head], msg, TERM_CHARS);
    term_buf[term_head][TERM_CHARS] = '\0';
    term_head = (term_head + 1) % TERM_LINES;
    term_redraw();
}

void term_printf(const char* fmt, ...) {
    char tmp[TERM_CHARS + 1];
    va_list args;
    va_start(args, fmt);
    vsnprintf(tmp, sizeof(tmp), fmt, args);
    va_end(args);
    term_print(tmp);
}

void term_show_prompt(const char* msg) {
    M5.Display.fillRect(0, TERM_PROMPT_Y, 240, TERM_CHAR_H + 2, TERM_BG);
    M5.Display.setTextSize(TERM_FONT_SIZE);
    M5.Display.setTextColor(TERM_PROMPT_COL);
    M5.Display.setCursor(2, TERM_PROMPT_Y);
    M5.Display.print(msg);
}

void term_clear_prompt() {
    M5.Display.fillRect(0, TERM_PROMPT_Y, 240, TERM_CHAR_H + 2, TERM_BG);
}

// Print text word-wrapped across terminal lines (max TERM_CHARS per line).
void term_print_wrapped(const char* text) {
    char line[TERM_CHARS + 1];
    int  line_len = 0;
    const char* p = text;

    while (*p) {
        // skip spaces between words
        while (*p == ' ') p++;
        if (!*p) break;

        // measure next word
        const char* word = p;
        int wlen = 0;
        while (p[wlen] && p[wlen] != ' ') wlen++;

        // word longer than a full line → hard-break it
        if (wlen > TERM_CHARS) {
            if (line_len > 0) { term_print(line); line_len = 0; }
            strncpy(line, word, TERM_CHARS);
            line[TERM_CHARS] = '\0';
            term_print(line);
            p += wlen;
            continue;
        }

        int need = (line_len == 0) ? wlen : (1 + wlen);
        if (line_len + need > TERM_CHARS) {
            // flush current line first
            line[line_len] = '\0';
            term_print(line);
            line_len = 0;
            need = wlen;
        }

        if (line_len > 0) line[line_len++] = ' ';
        memcpy(line + line_len, word, wlen);
        line_len += wlen;
        p += wlen;
    }

    if (line_len > 0) {
        line[line_len] = '\0';
        term_print(line);
    }
}

void term_init() {
    memset(term_buf, 0, sizeof(term_buf));
    term_head = 0;
    M5.Display.fillScreen(TERM_BG);
}
