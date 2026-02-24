set (SOURCES
    "input_processor.c"
    "main.c"
    "pi4ioe5v6416.c"
    "rotaries.c"
    "sound_stream.c"
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
                    )
