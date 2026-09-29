idf_component_register(
    SRCS "usb_descriptors.c" "hello_vocat.c" "app_ui.c" "clock_ui.c" "voice_ui.c"
         "mouse_ui.c" "air_mouse.c" "voice_usb.c" "vocat_v1_0.c" "espnow_log.c"
    INCLUDE_DIRS "." "usb"
    REQUIRES esp_vocat esp_codec_dev usb_device_uac espressif__tinyusb espressif__bmi270
             esp_common esp_event esp_netif esp_wifi freertos log nvs_flash
    EMBED_FILES "clock_digits.bin"
)

# usb_device_uac can be embedded in a composite USB device.  In that mode the
# application supplies TinyUSB's configuration and descriptors.
idf_component_get_property(tusb_lib espressif__tinyusb COMPONENT_LIB)
idf_component_get_property(uac_dir espressif__usb_device_uac COMPONENT_DIR)
target_include_directories(${tusb_lib} PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/usb"
    "${uac_dir}/tusb_uac")
target_sources(${tusb_lib} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/usb_descriptors.c")
target_include_directories(${COMPONENT_LIB} PRIVATE "${uac_dir}/tusb_uac")
