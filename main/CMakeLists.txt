idf_component_register(
    SRCS "badge_app.c"
         "badge_memory.c"
         "badge_audio.c"
         "badge_clip.c"
         "badge_gateway.c"
         "badge_tls.c"
         "badge_screen.c"
         "badge_link.c"
         "badge_nvs.c"
         "badge_wifi.c"
         "ui_pixel.c"
         "../assets/fonts/badge_font_16.c"
         "../assets/fonts/badge_font_20.c"
         "../assets/images/badge_lobster_22.c"
         "../app/badge_store.c"
         "../app/badge_proxy.c"
         "../app/badge_ss.c"
         "../app/badge_ui.c"
         "../app/badge_usb.c"
         "../app/badge_line.c"
         "../app/badge_json.c"
         "../app/badge_radio.c"
         "../app/badge_reply.c"
         "../app/badge_pair.c"
         "../app/openclaw_frame.c"
    INCLUDE_DIRS "." "../app"
    REQUIRES bsp nvs_flash bt esp_event esp_netif esp_wifi esp_app_format esp_partition
             tcp_transport esp-tls mbedtls json passport_crypto passport_transport
)

set_source_files_properties(badge_gateway.c PROPERTIES COMPILE_OPTIONS "-Os")
set_source_files_properties("../app/badge_usb.c" PROPERTIES COMPILE_OPTIONS "-Os")

target_link_options(${COMPONENT_LIB} INTERFACE
    "-Wl,--wrap=esp_tls_conn_new_sync" "-Wl,--undefined=__wrap_esp_tls_conn_new_sync"
    "-Wl,--wrap=esp_tls_conn_destroy"
    "-Wl,--wrap=esp_tls_plain_tcp_connect" "-Wl,--wrap=lwip_connect" "-Wl,--wrap=lwip_send" "-Wl,--wrap=lwip_recv" "-Wl,--wrap=lwip_read" "-Wl,--wrap=lwip_write" "-Wl,--wrap=lwip_close")
