# register the component and set "RadioLib", "esp_timer" and "driver" as required
idf_component_register(SRCS "main.cpp" "EspHal.cpp"
                       INCLUDE_DIRS "."
                       REQUIRES jgromes__radiolib esp_timer driver esp_driver_gpio esp_driver_spi)
