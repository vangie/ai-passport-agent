idf_component_register(
    SRCS "passport_app.c"
         "passport_power.c"
         "passport_poweroff.c"
         "passport_memory.c"
         "passport_audio.c"
         "passport_clip.c"
         "passport_gateway.c"
         "passport_muse.cpp"
         "passport_muse_store.c"
         "passport_muse_ble.c"
         "passport_clock.c"
         "passport_tls.c"
         "passport_screen.c"
         "passport_link.c"
         "passport_nvs.c"
         "passport_wifi.c"
         "ui_pixel.c"
         "../assets/fonts/passport_font_16.c"
         "../assets/fonts/passport_font_20.c"
         "../assets/images/passport_lobster_22.c"
         "../assets/images/passport_hermes_22.c"
         "../app/passport_store.c"
         "../app/passport_network.c"
         "../app/passport_proxy.c"
         "../app/passport_ss.c"
         "../app/passport_ui.c"
         "../app/passport_usb.c"
         "../app/passport_line.c"
         "../app/passport_ble_session.c"
         "../app/passport_json.c"
         "../app/passport_endpoint.c"
         "../app/passport_radio.c"
         "../app/passport_reply.c"
         "../app/passport_pair.c"
         "../app/openclaw_frame.c"
    INCLUDE_DIRS "." "../app"
    REQUIRES bsp nvs_flash bt esp_event esp_netif esp_wifi esp_app_format esp_partition esp_pm
             tcp_transport esp-tls esp_http_client mbedtls json passport_crypto passport_transport passport_vad passport_muse
)

# The UI and application loop run on the small main-task stack. Fail the
# firmware build if scratch-heavy helpers are inlined back into those frames.
set_source_files_properties(passport_screen.c passport_app.c PROPERTIES
    COMPILE_OPTIONS "-Wframe-larger-than=768;-Werror=frame-larger-than=768")

target_link_options(${COMPONENT_LIB} INTERFACE
    "-Wl,--wrap=esp_tls_conn_new_sync" "-Wl,--undefined=__wrap_esp_tls_conn_new_sync"
    "-Wl,--wrap=esp_tls_conn_destroy"
    "-Wl,--wrap=esp_tls_plain_tcp_connect" "-Wl,--wrap=lwip_connect" "-Wl,--wrap=lwip_send" "-Wl,--wrap=lwip_recv" "-Wl,--wrap=lwip_read" "-Wl,--wrap=lwip_write" "-Wl,--wrap=lwip_close")
