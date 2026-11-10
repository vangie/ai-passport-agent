#include "passport_memory.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdatomic.h>
#include <stdio.h>

static atomic_uint s_failures, s_failed_bytes, s_failed_caps;
/* Called inside the allocator: record facts only, never log or allocate here. */
static void allocation_failed(size_t bytes, uint32_t caps, const char *function) {
    (void)function;
    atomic_fetch_add(&s_failures, 1);
    atomic_store(&s_failed_bytes, (unsigned)bytes);
    atomic_store(&s_failed_caps, caps);
}
void passport_memory_init(void) {
    (void)heap_caps_register_failed_alloc_callback(allocation_failed);
}
void passport_memory_failures_reset(void) {
    (void)heap_caps_monitor_local_minimum_free_size_stop();
    (void)heap_caps_monitor_local_minimum_free_size_start();
    atomic_store(&s_failures,0); atomic_store(&s_failed_bytes,0); atomic_store(&s_failed_caps,0);
}
void passport_memory_format(char *out, size_t cap, const char *phase) {
    const uint32_t internal = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
    snprintf(out,cap,
        "MEM phase=%s free=%u largest=%u min=%u dma=%u dma_largest=%u stack_free=%u "
        "alloc_failures=%u failed_bytes=%u failed_caps=%u",
        phase, (unsigned)heap_caps_get_free_size(internal),
        (unsigned)heap_caps_get_largest_free_block(internal),
        (unsigned)heap_caps_get_minimum_free_size(internal),
        (unsigned)heap_caps_get_free_size(MALLOC_CAP_DMA),
        (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_DMA),
        (unsigned)uxTaskGetStackHighWaterMark(NULL),
        atomic_load(&s_failures),atomic_load(&s_failed_bytes),atomic_load(&s_failed_caps));
}
void passport_memory_report(const char *phase) {
    char out[320]; passport_memory_format(out,sizeof(out),phase);
    ESP_LOGI("passport_memory","%s",out);
}
