#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "spi_bus_lock.h"

static SemaphoreHandle_t s_mutex = NULL;

void shared_spi_lock_init(void)
{
    if (s_mutex == NULL) {
        s_mutex = xSemaphoreCreateMutex();
    }
}

void shared_spi_lock_acquire(void)
{
    /* If init was somehow skipped, fail loudly rather than dereferencing
     * NULL -- this would only happen from a programming error, not at
     * runtime under normal operation. */
    configASSERT(s_mutex != NULL);
    xSemaphoreTake(s_mutex, portMAX_DELAY);
}

void shared_spi_lock_release(void)
{
    configASSERT(s_mutex != NULL);
    xSemaphoreGive(s_mutex);
}
