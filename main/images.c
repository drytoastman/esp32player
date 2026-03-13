#include <math.h>
#ifdef EXTERNAL
#include "lodepng.h"
#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_LOGE(tag, fmt, ...) fprintf(stderr, "ERROR: " fmt "\n", ##__VA_ARGS__)
#define ESP_LOGI(tag, fmt, ...) fprintf(stdout, "INFO: " fmt "\n", ##__VA_ARGS__)
#define EXT_RAM_BSS_ATTR
#define esp_err_t int
#else
#include "esp_log.h"
#include "all.h"
#endif

#define WIDTH 16
#define HEIGHT 16

extern const uint8_t numberoverlagy[];

static const char *TAG = "images";
EXT_RAM_BSS_ATTR uint8_t  icon6[WIDTH * HEIGHT * 3]; // RGB666 framebuffer
EXT_RAM_BSS_ATTR uint8_t icon32[WIDTH * HEIGHT * 4]; // RGBA8888 framebuffer
EXT_RAM_BSS_ATTR uint8_t lodespace[512 * 512 * 4]; // temporary space for lodepng (RGBA8888)
static int lodespace_offset = 0;

void *malloc_lodepng(size_t size) {
    if (lodespace_offset + size > sizeof(lodespace)) {
        ESP_LOGE(TAG, "LodePNG requested too much memory: %d bytes", size);
        return NULL;
    }
    void *ptr = lodespace + lodespace_offset;
    lodespace_offset += size;
    return ptr;
}

void *realloc_lodepng(void *ptr, size_t new_size) {
    ESP_LOGI(TAG, "LodePNG realloc requested: %d bytes", new_size);
    return malloc_lodepng(new_size);
}

void free_lodepng(void *ptr) {
    // no-op since we have a fixed buffer
}

#define LODEPNG_MALLOC malloc_lodepng
#define LODEPNG_FREE free_lodepng
#define LODEPNG_REALLOC realloc_lodepng
#include "lodepng.h"


float srgb_to_linear(float f) {
    if (f <= 0.04045f)
        return f / 12.92f;
    else
        return powf((f + 0.055f) / 1.055f, 2.4f);
}

// Simple nearest-neighbor downscaling
void scale_image_nearest_neighbor(uint8_t* src, int src_w, int src_h,
                                   uint8_t* dst, int dst_w, int dst_h,
                                   int channels) {
    for (int y = 0; y < dst_h; y++) {
        for (int x = 0; x < dst_w; x++) {
            int src_x = (x * src_w) / dst_w;
            int src_y = (y * src_h) / dst_h;

            for (int c = 0; c < channels; c++) {
                dst[(y * dst_w + x) * channels + c] =
                    src[(src_y * src_w + src_x) * channels + c];
            }
        }
    }
}

/**
 * Process a 32-bit RGBA buffer of 8-bit colors into a 24-bit 6-bit color range, also adjusting for input
 * source.  Turns out the LED driver/board already seems to compenstate for the any driver power
 * as well as RGB differences so those are a nop.
 */
void process_image_32to6(uint8_t *infb, const uint8_t *overlay, int width, int height, bool srgb, uint8_t *outfb) {
    float bg_r = 0, bg_g = 0, bg_b = 0; // black background

    for (int ii = 0, jj = 0; ii < width * height * 4; ii+=4, jj+=3) {
        float lr = infb[ii]   / 255.0f;  // 8bit to float
        float lg = infb[ii+1] / 255.0f;
        float lb = infb[ii+2] / 255.0f;
        float la = infb[ii+3] / 255.0f;

        float or = overlay[ii]   / 255.0f;  // 8bit to float
        float og = overlay[ii+1] / 255.0f;
        float ob = overlay[ii+2] / 255.0f;
        float oa = overlay[ii+3] / 255.0f;

        if (srgb) {
            lr = srgb_to_linear(lr);
            lg = srgb_to_linear(lg);
            lb = srgb_to_linear(lb);

            or = srgb_to_linear(or);
            og = srgb_to_linear(og);
            ob = srgb_to_linear(ob);
        }

        // Alpha blend: result = foreground * alpha + background * (1 - alpha)
        lr = (lr * la + bg_r * (1 - la));
        lg = (lg * la + bg_g * (1 - la));
        lb = (lb * la + bg_b * (1 - la));

        // Add the overlay
        lr = (or * oa + lr * (1 - oa));
        lg = (og * oa + lg * (1 - oa));
        lb = (ob * oa + lb * (1 - oa));

        outfb[jj]   = (uint8_t)(lr * 63.0); // float to 6bit
        outfb[jj+1] = (uint8_t)(lg * 63.0);
        outfb[jj+2] = (uint8_t)(lb * 63.0);
    }
}

void draw_digit(uint8_t *icon6, int digit, int x, int y) {
    // Simple 3x5 font for digits 0-9
    static const uint8_t font[10][5] = {
        {0b111, 0b101, 0b101, 0b101, 0b111}, // 0
        {0b010, 0b110, 0b010, 0b010, 0b111}, // 1
        {0b111, 0b001, 0b111, 0b100, 0b111}, // 2
        {0b111, 0b001, 0b111, 0b001, 0b111}, // 3
        {0b101, 0b101, 0b111, 0b001, 0b001}, // 4
        {0b111, 0b100, 0b111, 0b001, 0b111}, // 5
        {0b111, 0b100, 0b111, 0b101, 0b111}, // 6
        {0b111, 0b001, 0b001, 0b001, 0b001}, // 7
        {0b111, 0b101, 0b111, 0b101, 0b111}, // 8
        {0b111, 0b101, 0b111, 0b001, 0b111}  //9
    };

    if (digit < 0 || digit > 9) { return; }

    for (int row = 0; row < 5; row++) {
        uint8_t bits = font[digit][row];
        for (int col = 2; col >= 0; col--) {
            if (bits & (1 << col)) {
                int draw_x = x + (2 - col);
                int draw_y = y + row;
                if (draw_x >= WIDTH || draw_y >= HEIGHT) continue;
                int idx = (draw_y * WIDTH + draw_x) *3;
                icon6[idx] = icon6[idx+1] = icon6[idx+2] = 63; // white pixel in RGB666
            }
        }
    }
}

void draw_number(uint8_t *icon6, int number, int width, int height) {
    // For simplicity, we'll just draw a single digit in the center of the 16x16 icon
    // In a real implementation, you'd want to support multiple digits and better positioning
    if (number > 99 || number < 0 ) { return; }
    if (number < 10) {
        draw_digit(icon6, number, 6, 5);
    } else {
        int tens = number / 10;
        int ones = number % 10;
        draw_digit(icon6, tens, 4, 5);
        draw_digit(icon6, ones, 8, 5);
    }
}


esp_err_t load_icon(char *path) {
    int original_width, original_height;
    lodespace_offset = 0; // reset lodepng buffer
    uint8_t *original = NULL;
    uint8_t *scaled = icon32;

    int ret = lodepng_decode32_file(&original, (unsigned*)&original_width, (unsigned*)&original_height, path);
    if (ret) {
        ESP_LOGE(TAG, "Failed to load image %s: %s", path, lodepng_error_text(ret));
        return ESP_FAIL;
    }

    if (original_height != HEIGHT || original_width != WIDTH) {
        ESP_LOGI(TAG, "Scaling image from %dx%d to %dx%d", original_width, original_height, WIDTH, HEIGHT);
        scale_image_nearest_neighbor(original, original_width, original_height,
                                     icon32, WIDTH, HEIGHT, 4);
        scaled = icon32;
    } else {
        scaled = original; // no scaling needed, can process in-place
    }

    process_image_32to6(scaled, numberoverlagy, WIDTH, HEIGHT, true, icon6);

    draw_number(icon6, system_state.volume, WIDTH, HEIGHT);

    #ifdef EXTERNAL
    // For debugging: save the processed framebuffer back to a PNG (will be 6-bit color but still in 8-bit channels)
    FILE *fp = fopen("outicon.data", "wb");
    if (fp) {
        fwrite(icon6, 1, WIDTH * HEIGHT * 3, fp);
        fclose(fp);
        ESP_LOGI(TAG, "Saved processed icon to outicon.data");
    } else {
        ESP_LOGE(TAG, "Failed to open file for saving processed icon");
    }
    #else
        display_load_fb(display, icon6, WIDTH * HEIGHT * 3);
    #endif

    return ESP_OK;
}


#ifdef EXTERNAL
int main() {
    return load_icon("icon.png");
}
#endif

// Provide an overlay with a 10x10 black area to 'write' our display numbers on to
// alpha gradient the outer area to let some of the 'background' through
const uint8_t numberoverlagy[WIDTH * HEIGHT * 4] = {
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\033\000\000\000\033"
  "\000\000\000\033\000\000\000\033\000\000\000\033\000\000\000\033\000\000\000\033\000\000\000\033\000\000\000\033\000\000\000\033"
  "\000\000\000\033\000\000\000\033\000\000\000\033\000\000\000\033\000\000\000\033\000\000\000\000\000\000\000\033\000\000\000W\000\000"
  "\000W\000\000\000W\000\000\000W\000\000\000W\000\000\000W\000\000\000W\000\000\000W\000\000\000W\000\000\000W\000\000\000W\000\000\000"
  "W\000\000\000W\000\000\000\033\000\000\000\000\000\000\000\033\000\000\000W\000\000\000\377\000\000\000\377\000\000\000\377\000"
  "\000\000\377\000\000\000\377\000\000\000\377\000\000\000\377\000\000\000\377\000\000\000\377\000\000\000\377\000\000"
  "\000\377\000\000\000W\000\000\000\033\000\000\000\000\000\000\000\033\000\000\000W\000\000\000\377\000\000\000\377\000\000\000"
  "\377\000\000\000\377\000\000\000\377\000\000\000\377\000\000\000\377\000\000\000\377\000\000\000\377\000\000\000\377"
  "\000\000\000\377\000\000\000W\000\000\000\033\000\000\000\000\000\000\000\033\000\000\000W\000\000\000\377\000\000\000\377\000"
  "\000\000\377\000\000\000\377\000\000\000\377\000\000\000\377\000\000\000\377\000\000\000\377\000\000\000\377\000\000"
  "\000\377\000\000\000\377\000\000\000W\000\000\000\033\000\000\000\000\000\000\000\033\000\000\000W\000\000\000\377\000\000\000"
  "\377\000\000\000\377\000\000\000\377\000\000\000\377\000\000\000\377\000\000\000\377\000\000\000\377\000\000\000\377"
  "\000\000\000\377\000\000\000\377\000\000\000W\000\000\000\033\000\000\000\000\000\000\000\033\000\000\000W\000\000\000\377\000"
  "\000\000\377\000\000\000\377\000\000\000\377\000\000\000\377\000\000\000\377\000\000\000\377\000\000\000\377\000\000"
  "\000\377\000\000\000\377\000\000\000\377\000\000\000W\000\000\000\033\000\000\000\000\000\000\000\033\000\000\000W\000\000\000"
  "\377\000\000\000\377\000\000\000\377\000\000\000\377\000\000\000\377\000\000\000\377\000\000\000\377\000\000\000\377"
  "\000\000\000\377\000\000\000\377\000\000\000\377\000\000\000W\000\000\000\033\000\000\000\000\000\000\000\033\000\000\000W\000"
  "\000\000\377\000\000\000\377\000\000\000\377\000\000\000\377\000\000\000\377\000\000\000\377\000\000\000\377\000\000"
  "\000\377\000\000\000\377\000\000\000\377\000\000\000\377\000\000\000W\000\000\000\033\000\000\000\000\000\000\000\033\000\000"
  "\000W\000\000\000\377\000\000\000\377\000\000\000\377\000\000\000\377\000\000\000\377\000\000\000\377\000\000\000\377"
  "\000\000\000\377\000\000\000\377\000\000\000\377\000\000\000\377\000\000\000W\000\000\000\033\000\000\000\000\000\000\000\033"
  "\000\000\000W\000\000\000\377\000\000\000\377\000\000\000\377\000\000\000\377\000\000\000\377\000\000\000\377\000\000\000"
  "\377\000\000\000\377\000\000\000\377\000\000\000\377\000\000\000\377\000\000\000W\000\000\000\033\000\000\000\000\000\000"
  "\000\033\000\000\000W\000\000\000\377\000\000\000\377\000\000\000\377\000\000\000\377\000\000\000\377\000\000\000\377"
  "\000\000\000\377\000\000\000\377\000\000\000\377\000\000\000\377\000\000\000\377\000\000\000W\000\000\000\033\000\000\000"
  "\000\000\000\000\033\000\000\000W\000\000\000W\000\000\000W\000\000\000W\000\000\000W\000\000\000W\000\000\000W\000\000\000W\000\000\000"
  "W\000\000\000W\000\000\000W\000\000\000W\000\000\000W\000\000\000\033\000\000\000\000\000\000\000\033\000\000\000\033\000\000\000\033"
  "\000\000\000\033\000\000\000\033\000\000\000\033\000\000\000\033\000\000\000\033\000\000\000\033\000\000\000\033\000\000\000\033"
  "\000\000\000\033\000\000\000\033\000\000\000\033\000\000\000\033\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000",
};