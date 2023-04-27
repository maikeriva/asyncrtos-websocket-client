// https://github.com/ThrowTheSwitch/Unity/blob/master/docs/UnityAssertionsReference.md
#include <esp_heap_trace.h>
#include <esp_heap_caps.h>

#define TEST_HEAP_START                                                                  \
    {                                                                                    \
        printf("Heap tracing started (%u)\n", heap_caps_get_free_size(MALLOC_CAP_8BIT)); \
        ESP_ERROR_CHECK(heap_trace_start(HEAP_TRACE_LEAKS));                             \
    }

#define TEST_HEAP_STOP                                                                 \
    {                                                                                  \
        ESP_ERROR_CHECK(heap_trace_stop());                                            \
        printf("Heap tracing ended (%u)\n", heap_caps_get_free_size(MALLOC_CAP_8BIT)); \
    }
