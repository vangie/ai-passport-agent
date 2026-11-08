# Based on ESP-IDF v5.5.3 tcp_transport/transport_ws.c (Apache-2.0).
# Keep the SDK untouched; only this application opts into the corrected reader.
idf_component_register(SRCS "transport_ws.c" INCLUDE_DIRS "."
                       REQUIRES tcp_transport PRIV_REQUIRES esp_timer)
idf_component_get_property(tcp_transport_dir tcp_transport COMPONENT_DIR)
target_include_directories(${COMPONENT_LIB} PRIVATE "${tcp_transport_dir}/private_include")
