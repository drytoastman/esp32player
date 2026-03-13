#include "all.h"

static const char *TAG = "gct";

void pcactl(bool level) {
    pi4ioe5v6416_write_pin(&iox, output_params.iox.pactl, level);
}

void nfc_cs(bool level) {
    pi4ioe5v6416_write_pin(&iox, output_params.iox.nfc_cs, level);
}

void nfc_irq(bool level) {
    pi4ioe5v6416_write_pin(&iox, output_params.iox.nfc_irq, level);
}

bool nfc_irq_check() {
    return pi4ioe5v6416_read_pin(&iox, input_params.iox.nfc_irq);
}

void display_cs(int display, bool level) {
    if ((display < 0) || (display >= 4)) {
        ESP_LOGE(TAG, "display cs out of range (%d)", display);
        return;
    }
    pi4ioe5v6416_write_pin(&iox, output_params.iox.display[display], level);
}

void grand_central_task(void *ignored) {

}

