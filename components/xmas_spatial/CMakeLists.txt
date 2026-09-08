idf_component_register(SRCS "xmas_spatial_math.c" "xmas_spatial_cluster.c" "xmas_spatial_esp.c"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_wifi esp_event esp_netif esp_timer lwip freertos)
target_compile_features(${COMPONENT_LIB} PRIVATE c_std_11)
