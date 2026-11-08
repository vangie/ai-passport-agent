#pragma once
#define ESP_LOGE(tag,...) ((void)(tag))
#define ESP_LOGW(tag,...) ((void)(tag))
#define ESP_LOGI(tag,...) do { (void)(tag); if(0) fprintf(stderr,__VA_ARGS__); } while(0)
#define ESP_LOGD(tag,...) ((void)(tag))
#define ESP_LOGV(tag,...) ((void)(tag))
#define ESP_STATIC_ANALYZER_CHECK(condition,value) do { if (condition) return value; } while(0)
