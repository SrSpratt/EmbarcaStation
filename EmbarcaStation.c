#include <stdio.h>
#include "pico/stdlib.h"
#include "pico/bootrom.h"
#include "pico/cyw43_arch.h"
#include "lwip/pbuf.h"
#include "lwip/tcp.h"
#include "lwip/netif.h"
#include "hardware/gpio.h"
#include "hardware/irq.h"
#include "hardware/watchdog.h"
#include "FreeRTOS.h"
#include "FreeRTOSConfig.h"
#include "task.h"
#include "semphr.h"
#include "queue.h"

#define WIFI_SSID "JRGOMESRDR OI FIBRA"
#define WIFI_PASSWORD "123456jr"

#define WIFI_LED CYW43_WL_GPIO_LED_PIN
#define button_a 5
#define button_b 6
#define button_j 22

SemaphoreHandle_t xDisplayMutex;
QueueHandle_t xConnectionStateQueue;

enum ConnectionState{
    WIFI_CONNECTING,
    WIFI_FAILED,
    WIFI_SUCCEEDED,
    ANOTHER_ERROR
};

uint32_t time_since_last_pressing = 0;
uint16_t regular_tick = 1000;

void reset_board(uint gpio, uint32_t events);

static err_t tcp_server_accept(void *arg, struct tcp_pcb *new_pcb, err_t err);
static err_t tcp_server_recv(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err);
void user_request(char **request);

void vHelloTask();
void vConnectTask();

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

    xConnectionStateQueue = xQueueCreate(3, sizeof(int));

    xTaskCreate(vHelloTask, "Hello Task", configMINIMAL_STACK_SIZE, NULL, tskIDLE_PRIORITY, NULL);
    xTaskCreate(vConnectTask, "Connect task", configMINIMAL_STACK_SIZE, NULL, tskIDLE_PRIORITY, NULL);
    vTaskStartScheduler();
    panic_unsupported();
}



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

static err_t tcp_server_accept(void *arg, struct tcp_pcb *new_pcb, err_t err){
    tcp_recv(new_pcb, tcp_server_recv);
    return ERR_OK;
}

static err_t tcp_server_recv(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err){
    if (!p){
        tcp_close(tpcb);
        tcp_recv(tpcb, NULL);
        return ERR_OK;
    }
    char *request = (char *)malloc(p->len + 1);
    memcpy(request, p->payload, p->len);
    request[p->len] = '\0';

    printf("Request: %s\n", request);

    if(strstr(request, "GET /")){
        printf("Entrou na requisição\n");
        char html[2048];

        snprintf(html, sizeof(html),
                    "HTTP/1.1 200 OK\r\n"
                    "Content-Type: text/html\r\n"
                    "Connection: close\r\n"
                    "\r\n"
                    "<!DOCTYPE html>"
                    "<html>"
                        "<head>"
                            "<title>🏠Painel</title>"
                            "<meta charset=\"UTF-8\">"
                        "</head>"
                        "<body>"
                            "<h1>Olá</h1>"
                        "</body>");
        tcp_write(tpcb, html, strlen(html), TCP_WRITE_FLAG_COPY);
        tcp_output(tpcb);

    }

    free(request);
    pbuf_free(p);
    return ERR_OK;
}

void vHelloTask()
{
    uint8_t connection_state = 0;
    while(true)
    {
        if (xQueueReceive(xConnectionStateQueue, &connection_state, portMAX_DELAY)){
            printf("Conexão: %d", connection_state);
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void vConnectTask()
{
    uint8_t connection_state = WIFI_CONNECTING;
    xQueueSend(xConnectionStateQueue, &connection_state, portMAX_DELAY);
    while (cyw43_arch_init())
    {
        connection_state = WIFI_FAILED;
        xQueueSend(xConnectionStateQueue, &connection_state, portMAX_DELAY);
    }

    cyw43_arch_gpio_put(WIFI_LED, true);
    cyw43_arch_enable_sta_mode();

    while(cyw43_arch_wifi_connect_timeout_ms(WIFI_SSID, WIFI_PASSWORD, CYW43_AUTH_WPA2_AES_PSK, 20000)){
        connection_state = WIFI_FAILED;
        xQueueSend(xConnectionStateQueue, &connection_state, portMAX_DELAY);
    }

    connection_state = WIFI_SUCCEEDED;

    if (netif_default)
        printf("IP: %s\n", ipaddr_ntoa(&netif_default->ip_addr));
    
    struct tcp_pcb * server = tcp_new();

    if (!server)
        connection_state = ANOTHER_ERROR;

    if (tcp_bind(server, IP_ADDR_ANY, 80) != ERR_OK)
        connection_state = ANOTHER_ERROR;

    server = tcp_listen(server);

    tcp_accept(server, tcp_server_accept);
    printf("Servidor escutando na porta 80\n");

    xQueueSend(xConnectionStateQueue, &connection_state, portMAX_DELAY);
    while (true){
        cyw43_arch_poll();
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    cyw43_arch_deinit();
    connection_state = ANOTHER_ERROR;
    xQueueSend(xConnectionStateQueue, &connection_state, portMAX_DELAY);
}
