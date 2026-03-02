set (SOURCES
    "analog.c"
    "cr95hf.c"
    "digital.c"
    "ht16d35a.c"
    "main.c"
    "pi4ioe5v6416.c"
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
