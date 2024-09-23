idf_component_register(SRCS "main.cpp" "midi_task.cpp"
                       INCLUDE_DIRS "."
                       REQUIRES driver esp_timer led_strip nvs_flash tinyusb)

idf_component_get_property(tusb_lib espressif__tinyusb COMPONENT_LIB)
target_include_directories(${tusb_lib} PUBLIC "${CMAKE_CURRENT_LIST_DIR}")