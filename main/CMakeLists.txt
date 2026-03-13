set (SOURCES
    "cr95hf.c"
    "debugio.c"
    "display.c"
    "gct.c"
    "images.c"
    "lis2dh12.c"
    "lodepng.c"
    "main.c"
    "nfc_process.c"
    "pi4ioe5v6416.c"
    "poller.c"
    "rotary.c"
    "sdcard.c"
    "sound_stream.c"
    "webserver.c"
    "wifi.c"
)

idf_component_register(SRCS ${SOURCES}
                    INCLUDE_DIRS "."
                    REQUIRES
                    audio_board
                    audio_hal
                    audio_pipeline
                    audio_stream
                    esp_adc
                    esp-adf-libs
                    esp_peripherals
                    esp_http_server
                    )
