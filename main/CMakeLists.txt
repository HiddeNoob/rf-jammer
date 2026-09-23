idf_component_register(
    SRCS "main.cpp" "EspHal.cpp" "RfSweeper.cpp" "MenuUi.cpp" "WifiScanner.cpp" "tasks/Task.cpp"
    INCLUDE_DIRS "."
    REQUIRES jgromes__radiolib esp_timer nvs_flash esp_wifi esp_netif esp_event driver esp_driver_gpio esp_driver_spi arduino u8g2 GEM
)

# Derleyicinin bu uyarıları hata olarak işlemesini engeller
target_compile_options(${COMPONENT_LIB} PRIVATE 
    -Wno-missing-field-initializers
    -Wno-error=missing-field-initializers
)