#include <stdio.h>
#include "pico/stdlib.h"
#include "pico/bootrom.h"
#include "hardware/gpio.h"
#include "hardware/irq.h"
#include "hardware/watchdog.h"
#include "FreeRTOS.h"
#include "FreeRTOSConfig.h"
#include "task.h"
#include "semphr.h"

#define button_a 5
#define button_b 6
#define button_j 22

uint32_t time_since_last_pressing = 0;
uint16_t regular_tick = 1000;

void reset_board(uint gpio, uint32_t events)
{
    uint32_t time_now = to_ms_since_boot(get_absolute_time());
    if (time_now - time_since_last_pressing < 250)
        return;

    if (gpio == 6)
        reset_usb_boot(0, 0);
    else if (gpio == 5){
        printf("WATCHDOG!\n");
        watchdog_enable(10, 1);
        while(true);
    } else if (gpio == 22){
        printf("JOYSTICK!\n");
    }

    time_since_last_pressing = time_now;
}

void vHelloTask()
{
    while(true)
    {
        printf("Hello World!\n");
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}


int main()
{
    stdio_init_all();

    gpio_init(button_b);
    gpio_set_dir(button_b, GPIO_IN);
    gpio_pull_up(button_b);

    gpio_init(button_a);
    gpio_set_dir(button_a, GPIO_IN);
    gpio_pull_up(button_a);

    gpio_init(button_j);
    gpio_set_dir(button_j, GPIO_IN);
    gpio_pull_up(button_j);

    gpio_set_irq_enabled(button_b, GPIO_IRQ_EDGE_RISE, true);
    gpio_set_irq_enabled(button_a, GPIO_IRQ_EDGE_RISE, true);
    gpio_set_irq_enabled(button_j, GPIO_IRQ_EDGE_RISE, true);
    gpio_set_irq_callback(&reset_board);
    irq_set_enabled(IO_IRQ_BANK0, true);

    xTaskCreate(vHelloTask, "Hello Task", configMINIMAL_STACK_SIZE, NULL, tskIDLE_PRIORITY, NULL);
    vTaskStartScheduler();
    panic_unsupported();
}
