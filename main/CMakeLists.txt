idf_component_register(
    SRCS "main.c"
         "workout_model.c"
         "ai_quota.c"
         "workout_json.c"
         "workout_store.c"
         "workout_network.c"
         "workout_portal.c"
         "workout_ui.c"
         "../assets/fonts/workout_font_12.c"
         "../assets/fonts/workout_font_16.c"
         "../assets/fonts/workout_font_20.c"
         "../assets/fonts/workout_digits_35.c"
    INCLUDE_DIRS "." "../assets/fonts"
    REQUIRES bsp esp_event esp_hw_support esp_netif esp_timer esp_wifi nvs_flash
             esp_http_client esp_http_server json mbedtls esp_pm
    EMBED_TXTFILES "workout_setup.html"
)
