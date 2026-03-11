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
#include "all.h"
#endif

#define WIDTH 16
#define HEIGHT 16

extern const uint8_t number5[];


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

    process_image_32to6(scaled, number5, WIDTH, HEIGHT, true, icon6);

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
    ht16d35a_load_icon(ht16d35a, icon6, WIDTH * HEIGHT * 3);
    #endif

    return ESP_OK;
}


#ifdef EXTERNAL
int main() {
    return load_icon("icon.png");
}
#endif


const uint8_t number5[] = {
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\200\200\200\266"
  "\200\200\200\266\200\200\200\266\200\200\200\266\200\200\200\266\200\200"
  "\200\266\200\200\200\266\200\200\200\266\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\200\200\200\266III\377III\377III\377III"
  "\377III\377III\377\200\200\200\266\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\200\200\200\266III\377\016\262J\377\016\262J\377"
  "\016\262J\377\016\262J\377III\377\200\200\200\266\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\200\200\200\266III\377\016\262J\377"
  "III\377III\377III\377III\377\200\200\200\266\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\200\200\200\266III\377\016\262J\377\016"
  "\262J\377\016\262J\377\016\262J\377III\377\200\200\200\266\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\200\200\200\266III\377I"
  "II\377III\377III\377\016\262J\377III\377\200\200\200\266\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\200\200\200\266III\377\016"
  "\262J\377\016\262J\377\016\262J\377\016\262J\377III\377\200\200\200\266\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\200\200\200"
  "\266III\377III\377III\377III\377III\377III\377\200\200\200\266\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\200\200\200\266\200"
  "\200\200\266\200\200\200\266\200\200\200\266\200\200\200\266\200\200\200"
  "\266\200\200\200\266\200\200\200\266\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000",
};