
idf_component_register(
    SRCS "board.c" "board_pins_config.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES audio_sal audio_hal esp_dispatcher esp_peripherals display_service)

idf_component_get_property(audio_board_lib audio_board COMPONENT_LIB)
set_property(TARGET ${audio_board_lib} APPEND PROPERTY INTERFACE_LINK_LIBRARIES ${COMPONENT_LIB})