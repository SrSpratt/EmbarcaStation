#include <stdio.h>
#include "pico/stdlib.h"
#include "pico/bootrom.h"
#include "pico/cyw43_arch.h"
#include "lwip/pbuf.h"
#include "lwip/tcp.h"
#include "lwip/netif.h"
#include "hardware/gpio.h"
#include "hardware/irq.h"
#include "hardware/i2c.h"
#include "hardware/watchdog.h"
#include "lib/ssd1306.h"
#include "lib/font.h"
#include "aht20.h"
#include "bmp280.h"
#include "FreeRTOS.h"
#include "FreeRTOSConfig.h"
#include "task.h"
#include "semphr.h"
#include "queue.h"
#include <math.h>

#define WIFI_SSID "Template"
#define WIFI_PASSWORD "Template"

#define WIFI_LED CYW43_WL_GPIO_LED_PIN
#define button_a 5
#define button_b 6
#define button_j 22

#define I2C_PORT i2c0               // i2c0 
#define I2C_SDA 16                  // 0 ou 2 
#define I2C_SCL 17                  // 1 ou 3
#define SEA_LEVEL_PRESSURE 101325.0 // Pressão ao nível do mar em Pa

#define I2C1_PORT i2c1
#define I2C1_SDA 14
#define I2C1_SCL 15
#define ADDRESS 0x3C

SemaphoreHandle_t xDisplayMutex;
QueueHandle_t xConnectionStateQueue;
QueueHandle_t xBMPReadQueue;
QueueHandle_t xBMPReadWebQueue;
QueueHandle_t xAHTReadQueue;
QueueHandle_t xAHTReadWebQueue;

enum ConnectionState{
    WIFI_CONNECTING,
    WIFI_FAILED,
    WIFI_SUCCEEDED,
    ANOTHER_ERROR
};

uint32_t time_since_last_pressing = 0;
uint16_t regular_tick = 100;

float temperature_limit = 30;
float humidity_level = 60;

ssd1306_t ssd;


void reset_board(uint gpio, uint32_t events);

static err_t tcp_server_accept(void *arg, struct tcp_pcb *new_pcb, err_t err);
static err_t tcp_server_recv(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err);

void vHelloTask();
void vConnectTask();
void vDisplayTask();
void vSensorRead();

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

    gpio_init(11);
    gpio_init(12);
    gpio_init(13);
    gpio_set_dir(11, GPIO_OUT);
    gpio_set_dir(12, GPIO_OUT);
    gpio_set_dir(13, GPIO_OUT);

    // Inicia o I2C0
    i2c_init(I2C_PORT, 400 * 1000);
    gpio_set_function(I2C_SDA, GPIO_FUNC_I2C);
    gpio_set_function(I2C_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(I2C_SDA);
    gpio_pull_up(I2C_SCL);

    // Inicia o I2C1
    i2c_init(I2C1_PORT, 400 * 1000);
    gpio_set_function(I2C1_SDA, GPIO_FUNC_I2C);
    gpio_set_function(I2C1_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(I2C1_SDA);
    gpio_pull_up(I2C1_SCL);

    xDisplayMutex = xSemaphoreCreateMutex();
    xConnectionStateQueue = xQueueCreate(1, sizeof(int));
    xAHTReadQueue = xQueueCreate(1, sizeof(AHT20_Data));
    xBMPReadQueue = xQueueCreate(1, sizeof(BMP280_Data));
    xAHTReadWebQueue = xQueueCreate(1, sizeof(AHT20_Data));
    xBMPReadWebQueue = xQueueCreate(1, sizeof(BMP280_Data));

    //xTaskCreate(vHelloTask, "Hello Task", configMINIMAL_STACK_SIZE, NULL, tskIDLE_PRIORITY, NULL);
    xTaskCreate(vConnectTask, "Connect task", 2048, NULL, tskIDLE_PRIORITY, NULL);
    xTaskCreate(vDisplayTask, "Display Task", 2048, NULL, tskIDLE_PRIORITY, NULL);
    xTaskCreate(vSensorRead, "Sensor Read", 2048, NULL, tskIDLE_PRIORITY, NULL);
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

void draw_connection(uint8_t connected_state, bool color){

    ssd1306_fill_upper(&ssd, !color);                           // Limpa o display
    switch (connected_state)
    {
        case WIFI_SUCCEEDED:
        ssd1306_line(&ssd, 9, 5, 13, 5, color);  // Desenha uma linha
        ssd1306_line(&ssd, 8, 6, 8, 6, color);   // Desenha uma linha
        ssd1306_line(&ssd, 7, 7, 7, 7, color);   // Desenha uma linha
        ssd1306_line(&ssd, 15, 7, 15, 7, color); // Desenha uma linha
        ssd1306_line(&ssd, 14, 6, 14, 6, color); // Desenha uma linha

        ssd1306_line(&ssd, 10, 7, 12, 7, color);  // Desenha uma linha
        ssd1306_line(&ssd, 9, 8, 9, 8, color);    // Desenha uma linha
        ssd1306_line(&ssd, 13, 8, 13, 8, color);  // Desenha uma linha
        ssd1306_line(&ssd, 11, 9, 11, 9, color);  // Desenha uma linha
        ssd1306_draw_string(&ssd, "on", 17, 4); // Desenha uma string
        break;
        case WIFI_FAILED:
        ssd1306_line(&ssd, 9, 5, 13, 5, color);  // Desenha uma linha
        ssd1306_line(&ssd, 8, 6, 8, 6, color);   // Desenha uma linha
        ssd1306_line(&ssd, 7, 7, 7, 7, color);   // Desenha uma linha
        ssd1306_line(&ssd, 15, 7, 15, 7, color); // Desenha uma linha
        ssd1306_line(&ssd, 14, 6, 14, 6, color); // Desenha uma linha

        ssd1306_line(&ssd, 10, 7, 12, 7, color);   // Desenha uma linha
        ssd1306_line(&ssd, 9, 8, 9, 8, color);     // Desenha uma linha
        ssd1306_line(&ssd, 13, 8, 13, 8, color);   // Desenha uma linha
        ssd1306_line(&ssd, 11, 9, 11, 9, color);   // Desenha uma linha
        ssd1306_draw_string(&ssd, "off", 17, 4); // Desenha uma string
        break;
        case WIFI_CONNECTING:
        ssd1306_line(&ssd, 9, 5, 13, 5, color);  // Desenha uma linha
        ssd1306_line(&ssd, 8, 6, 8, 6, color);   // Desenha uma linha
        ssd1306_line(&ssd, 7, 7, 7, 7, color);   // Desenha uma linha
        ssd1306_line(&ssd, 15, 7, 15, 7, color); // Desenha uma linha
        ssd1306_line(&ssd, 14, 6, 14, 6, color); // Desenha uma linha

        ssd1306_line(&ssd, 10, 7, 12, 7, color);   // Desenha uma linha
        ssd1306_line(&ssd, 9, 8, 9, 8, color);     // Desenha uma linha
        ssd1306_line(&ssd, 13, 8, 13, 8, color);   // Desenha uma linha
        ssd1306_line(&ssd, 11, 9, 11, 9, color);   // Desenha uma linha
        ssd1306_draw_string(&ssd, "try", 17, 4); // Desenha uma string
        break;
    }
}


void draw_on_display(bool color, ssd1306_t ssd, AHT20_Data aht_data, BMP280_Data bmp_data){

    char str_tmp1[5];  // Buffer para armazenar a string
    char str_alt[5];  // Buffer para armazenar a string  
    char str_tmp2[5];  // Buffer para armazenar a string
    char str_umi[5];  // Buffer para armazenar a string      

    sprintf(str_tmp1, "%.1fC", bmp_data.temperature / 100.0);  // Converte o inteiro em string
    sprintf(str_alt, "%.0fm", bmp_data.altitude);  // Converte o inteiro em string
    sprintf(str_tmp2, "%.1fC", aht_data.temperature);  // Converte o inteiro em string
    sprintf(str_umi, "%.1f%%", aht_data.humidity);  // Converte o inteiro em string

    ssd1306_fill_lower(&ssd, !color);                           // Limpa o display

    sprintf(str_tmp1, "%.1fC", bmp_data.temperature / 100.0);  // Converte o inteiro em string
    sprintf(str_alt, "%.0fm", bmp_data.altitude);  // Converte o inteiro em string
    sprintf(str_tmp2, "%.1fC", aht_data.temperature);  // Converte o inteiro em string
    sprintf(str_umi, "%.1f%%", aht_data.humidity);  // Converte o inteiro em string        

    //  Atualiza o conteúdo do display com animações
    ssd1306_rect(&ssd, 3, 3, 122, 60, color, !color);       // Desenha um retângulo
    ssd1306_line(&ssd, 3, 25, 123, 25, color);            // Desenha uma linha
    ssd1306_line(&ssd, 3, 37, 123, 37, color);            // Desenha uma linha
    ssd1306_draw_string(&ssd, "BMP280  AHT10", 10, 28); // Desenha uma string
    ssd1306_line(&ssd, 63, 25, 63, 60, color);            // Desenha uma linha vertical
    ssd1306_draw_string(&ssd, str_tmp1, 14, 41);             // Desenha uma string
    ssd1306_draw_string(&ssd, str_alt, 14, 52);             // Desenha uma string
    ssd1306_draw_string(&ssd, str_tmp2, 73, 41);             // Desenha uma string
    ssd1306_draw_string(&ssd, str_umi, 73, 52);            // Desenha uma string
}

// Função para calcular a altitude a partir da pressão atmosférica
double calculate_altitude(double pressure)
{
    return 44330.0 * (1.0 - pow(pressure / SEA_LEVEL_PRESSURE, 0.1903));
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

    AHT20_Data aht_data = {
        .humidity = 0,
        .temperature = 0
    };

    BMP280_Data bmp_data = {
        .altitude = 0,
        .pressure = 0,
        .temperature = 0
    };

    xQueuePeek(xAHTReadWebQueue, &aht_data, 0);
    xQueuePeek(xBMPReadWebQueue, &bmp_data, 0);
    //printf("temperature: %.2f", aht_data.temperature);
    

    printf("Request: %s\n", request);
    if (strstr(request, "GET /level") != NULL) { // Responde a requisição de tensão medida
        char reading[256];
        snprintf(reading, sizeof(reading), 
                "{"
                "\"temperature\": %.2f,"
                "\"humidity\": %.2f,"
                "\"pressure\": %.2f,"
                "\"altitude\": %.2f"
                "}", aht_data.temperature,
                aht_data.humidity,
                bmp_data.pressure /1000.0,
                bmp_data.altitude);

        char response[512];
        snprintf(response, sizeof(response),
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: text/plain\r\n"
            "Content-Length: %d\r\n"
            "Connection: close\r\n"
            "\r\n"
            "%s",
            strlen(reading), reading);

        tcp_write(tpcb, response, strlen(response), TCP_WRITE_FLAG_COPY);
        tcp_output(tpcb);
        printf(response);
    } else if (strstr(request, "POST /form") != NULL) { //responde à inserção de dado via formulário
      char *body = strstr(request, "\r\n\r\n");
      if (body) {
          body += 4; // pula os caracteres de nova linha
          float temp, hum;
          int n = sscanf(body, "temp: %f\nhum: %f", &temp, &hum);
          if (n == 2){
            humidity_level = hum;
            temperature_limit = temp;
            printf("humidity: %.2f\n", humidity_level);
            printf("temperature: %.2f\n", temperature_limit);
          }
          char response_body[64];
          snprintf(response_body, sizeof(response_body), "Temp: %.2f; Hum: %.2f", temp, hum);

          char response[128];
          snprintf(response, sizeof(response),
              "HTTP/1.1 200 OK\r\n"
              "Content-Type: text/plain\r\n"
              "Content-Length: %d\r\n"
              "Connection: close\r\n"
              "\r\n"
              "%s", strlen(response_body), response_body);
          tcp_write(tpcb, response, strlen(response), TCP_WRITE_FLAG_COPY);
          tcp_output(tpcb);
      }
    } else {
        char html[4900];

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
                        "<style>"
                            "body{"
                                "background:#f8f9fa;"
                                "font-family:Arial;"
                                "margin:0;"
                                "min-height:100vh;"
                                "display:flex;"
                                "flex-direction:column;"
                                "align-items:center;}"
                            ".container{"
                                "max-width:800px;"
                                "margin:0 auto;"
                                "padding:20px;"
                                "display:flex;"
                                "flex-direction: row;}"
                            ".section{"
                                "display:flex;"
                                "flex-direction:column;"
                                "background:#f6f6f6;"
                                "border-radius:10px;"
                                "padding: 10px;"
                                "box-shadow:0 4px 6px rgba(0,0,0,0.1);"
                                "}"
                            ".card{background:#fff;"
                                "border-radius:10px;"
                                "box-shadow:0 4px 6px rgba(0,0,0,0.1);"
                                "padding:20px;"
                                "margin-bottom:20px;"
                                "max-width:350px}"
                            ".content{display:flex;"
                                "flex-direction:row;"
                                "flex-wrap:wrap;"
                                "justify-self:center;}"
                            ".btn{"
                                "display:inline-flex;"
                                "align-items:center;"
                                "justify-content:center;"
                                "background:#6c757d;"
                                "color:white;"
                                "border:none;"
                                "border-radius:5px;"
                                "padding:12px 24px;"
                                "font-size:18px;"
                                "margin:8px;"
                                "cursor:pointer;"
                                "transition:all 0.3s;}"
                            ".btn:hover{"
                                "opacity:0.8;"
                                "transform:translateY(-2px);}"
                            ".classifier{"
                                "display:flex;"
                                "gap:15px;"
                                "justify-content:space-around"
                            "}"
                            ".card-label{"
                                "text-align:center;"
                                "display: flex;"
                                "font-size: 22px"
                                "margin:0px 0 15px 0;"
                                "font-weight:600;"
                                "justify-content:center;"
                            "}"
                            ".btn-p{"
                                "background:#0d6efd;}"
                            ".btn-d{"
                                "background:#dc3545;}"
                            ".btn-s{"
                                "background:#198754;}"
                            ".btn-w{"
                                "background:#ffc107;color:#000;}"
                            ".form-group{"
                                "margin-bottom:1rem;"
                                "display:flex;"
                                "align-items:center;}"
                            "select{"
                                "padding:8px;"
                                "border-radius:4px;"
                                "border:1px solid #ced4da;"
                                "margin-left:10px;}"
                            "h1{"
                                "color:#212529;"
                                "margin-bottom:1.5rem;}"
                            "input{"
                                "border-radius:10px;"
                                "display: flex;"
                                "margin:5px 0;"
                            "}"
                            "canvas { max-width: 100%%; height: auto; margin-top: 20px; }\r\n"
                            ".sensor{"
                                "font-size:12px;"
                                "color:#495057;"
                                "margin-top:1rem;"
                                "flex:1 1 50%%}"
                            ".text{"
                                "display:flex;"
                                "flex:1 1 100%%;}"
                        "</style>"
                        "<script src=\"https://cdn.jsdelivr.net/npm/chart.js\"></script>"
                    "</head>"
                    "<body>"
                        "<h1>🏠 Painel</h1>"
                        "<div class=\"container\">"
                            "<div class=\"office section\">"
                                "<h4>Projeto</h4>"
                                "<div class=\"classifier\">"
                                  "<div class=\"card\">"
                                      "<span class=\"card-label\">🌡️Temperatura</span>"
                                      "<div class=\"content\">"
                                        "<span id=\"temperature\">0</span><span>ºC</span>"
                                      "</div>"
                                  "</div>"
                                  "<div class=\"card\">"
                                      "<span class=\"card-label\">☁️ Umidade</span>"
                                      "<div class=\"content\">"
                                        "<span id=\"humidity\">0</span><span>ºC</span>"
                                      "</div>"
                                  "</div>"
                                "</div>"
                                "<div class=\"classifier\">"
                                  "<div class=\"card\">"
                                      "<span class=\"card-label\">☁️ Pressão</span>"
                                      "<div class=\"content\">"
                                        "<span id=\"pressure\">0</span><span>kPa</span>"
                                      "</div>"
                                  "</div>"
                                  "<div class=\"card\">"
                                      "<span class=\"card-label\">⛰️ Altitude</span>"
                                      "<div class=\"content\">"
                                        "<span id=\"altitude\">0</span><span>m</span>"
                                      "</div>"
                                  "</div>"
                                "</div>"
                                "<div class=\"card\">"
                                    "<span class=\"card-label\">🎚️ Alterar nível</span>"
                                    "<div class=\"content\">"
                                        "<form id=\"level-mod\">"
                                          "<label for=\"level-min\">Umidade:</label>"
                                          "<input type=\"text\" id=\"level-hum\" placeholder=\"6\" required />"
                                          "<label for=\"level-max\">:</label>"
                                          "<input type=\"text\" id=\"level-temp\" placeholder=\"10\" required />"
                                          "<button id=\"send-btn\" type=\"button\" class=\"btn btn-p\">Enviar</button>"
                                          "<div id=\"answer\"></div>"
                                        "</form>"
                                    "</div>"
                                "</div>"
                            "<div class=\"card\">"
                                "<span class=\"card-label\">📈 Histórico</span>"
                                "<canvas id=\"myChart\" width=\"200px\" height=\"200px\" style=\"display: block; box-sizing: border-box; height: 300px; width: 300px;\"></canvas>"
                            "</div>"
                            "</div>"
                        "</div>"
                    "</body>"
                    "<script>"
                        "let myChart;"
                        "const ctx=document.getElementById('myChart').getContext('2d');"
                        "const chartData={labels:[],datasets:["
                            "{label:'Temperatura (°C)',data:[],borderColor:'red',fill:false},"
                            "{label:'Umidade (%%)',data:[],borderColor:'blue',fill:false}"
                        "]};"
                        "if (myChart){"
                            "myChart.destroy()"
                        "}"
                        "myChart=new Chart(ctx,{"
                            "type:'line',"
                            "data:chartData,"
                            "options:{responsive:true,animation:false,scales:{y:{beginAtZero:false}}}"
                        "});"

                        "setInterval(()=>{"
                            "fetch('/level')"
                                ".then(res=>res.json())"
                                ".then(data=>{"
                                    "document.getElementById('temperature').innerText=data.temperature;"
                                    "document.getElementById('humidity').innerText=data.humidity;"
                                    "document.getElementById('pressure').innerText=data.pressure;"
                                    "document.getElementById('altitude').innerText=data.altitude;"

                                    "const now=new Date().toLocaleTimeString();"
                                    "if(chartData.labels.length>20){"
                                        "chartData.labels.shift();"
                                        "chartData.datasets[0].data.shift();"
                                        "chartData.datasets[1].data.shift();"
                                    "}"
                                    "chartData.labels.push(now);"
                                    "chartData.datasets[0].data.push(data.temperature);"
                                    "chartData.datasets[1].data.push(data.humidity);"
                                    "myChart.update();"

                                "});"
                        "},2000);"
                      "document.getElementById(\"send-btn\").addEventListener(\"click\",(e)=>{" //cria a ação de envio POST para o botão
                        "e.preventDefault();"
                        "const temp=document.getElementById(\"level-temp\").value;"
                        "const hum=document.getElementById(\"level-hum\").value;"
                        "fetch(\"/form\",{" 
                          "method: \"POST\","
                          "headers:{\"Content-Type\":\"text/plain\"},"
                          "body:\"hum: \"+hum+\"\\ntemp: \"+temp"
                        "})"
                        ".then(res=>res.text())"
                        ".then(data=>"
                          "document.getElementById(\"answer\").innerText=\"enviado\""
                        ")"
                        ".catch(err=>{"
                          "console.error(\"Error! \"+err)"
                        "});"
                      "});"
                    "</script>"
                "</html>");
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
        if (xQueueReceive(xConnectionStateQueue, &connection_state, portMAX_DELAY) == pdTRUE){
            printf("Conexão: %d", connection_state);
            if (connection_state == WIFI_SUCCEEDED)
                printf("Servidor escutando na porta 80\n");
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
        vTaskDelay(pdMS_TO_TICKS(regular_tick));
    }

    cyw43_arch_gpio_put(WIFI_LED, true);
    cyw43_arch_enable_sta_mode();

    while(cyw43_arch_wifi_connect_timeout_ms(WIFI_SSID, WIFI_PASSWORD, CYW43_AUTH_WPA2_AES_PSK, 20000)){
        connection_state = WIFI_FAILED;
        xQueueSend(xConnectionStateQueue, &connection_state, portMAX_DELAY);
        vTaskDelay(pdMS_TO_TICKS(regular_tick));
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

    xQueueSend(xConnectionStateQueue, &connection_state, portMAX_DELAY);
    while (true){
        cyw43_arch_poll();
        vTaskDelay(pdMS_TO_TICKS(500));
    }

    cyw43_arch_deinit();
    connection_state = ANOTHER_ERROR;
    xQueueSend(xConnectionStateQueue, &connection_state, portMAX_DELAY);
}

void vDisplayTask()
{
  ssd1306_init(&ssd, WIDTH, HEIGHT, false, ADDRESS, I2C1_PORT); // Inicializa o display
  ssd1306_config(&ssd);                                         // Configura o display
  ssd1306_send_data(&ssd);                                      // Envia os dados para o display
  ssd1306_fill(&ssd, false);
  ssd1306_send_data(&ssd);
  char str[5]; // Buffer para armazenar a string

  bool color = true;
  uint8_t connected_state = 0;
  AHT20_Data aht_data = {
    .humidity = 0,
    .temperature = 0
  };
  BMP280_Data bmp_data = {
    .altitude = 0,
    .pressure = 0,
    .temperature = 0
  };

  while (true)
  {
    printf("tarefa do display/");
    if (xSemaphoreTake(xDisplayMutex, pdMS_TO_TICKS(500)) == pdTRUE){
        ssd1306_rect(&ssd, 3, 3, 122, 60, color, !color); // Desenha um retângulo

        if (xQueueReceive(xConnectionStateQueue, &connected_state, 0) == pdTRUE)
            draw_connection(connected_state, color);
            
        xQueuePeek(xAHTReadQueue, &aht_data, 0);
        xQueuePeek(xBMPReadQueue, &bmp_data, 0);
        draw_on_display(color, ssd, aht_data, bmp_data ); 

        ssd1306_send_data(&ssd);                            // Atualiza o display
        xSemaphoreGive(xDisplayMutex);
    }
    vTaskDelay(pdMS_TO_TICKS(500));
  }
}

void vSensorRead(){
    // Inicializa o BMP280
    bmp280_init(I2C_PORT);
    struct bmp280_calib_param params;
    bmp280_get_calib_params(I2C_PORT, &params);

    // Inicializa o AHT20
    aht20_reset(I2C_PORT);
    aht20_init(I2C_PORT);

    // Estrutura para armazenar os dados do sensor
    AHT20_Data aht_data;
    BMP280_Data bmp_data;
    int32_t raw_temp_bmp;
    int32_t raw_pressure;

    while(true){
        // Leitura do BMP280
        bmp280_read_raw(I2C_PORT, &raw_temp_bmp, &raw_pressure);
        bmp_data.temperature = bmp280_convert_temp(raw_temp_bmp, &params);
        bmp_data.pressure = bmp280_convert_pressure(raw_pressure, raw_temp_bmp, &params);

        // Cálculo da altitude
        bmp_data.altitude = calculate_altitude(bmp_data.pressure);
        printf("tarefa do sensor!\n");
        //printf("Pressao = %.3f kPa\n", bmp_data.pressure / 1000.0);
        //printf("Temperatura BMP: = %.2f C\n", bmp_data.temperature / 100.0);
        //printf("Altitude estimada: %.2f m\n", bmp_data.altitude);

        // Leitura do AHT20
        if (aht20_read(I2C_PORT, &aht_data))
        {
            //printf("Temperatura AHT: %.2f C\n", aht_data.temperature);
            //printf("Umidade: %.2f %%\n\n\n", aht_data.humidity);
        }
        else
        {
            //printf("Erro na leitura do AHT10!\n\n\n");
        }

        xQueueOverwrite(xAHTReadQueue, &aht_data);
            //printf("FILA AHT CHEIA!");
        xQueueOverwrite(xBMPReadQueue, &bmp_data);
        xQueueOverwrite(xAHTReadWebQueue, &aht_data);
            //printf("FILA AHT CHEIA!");
        xQueueOverwrite(xBMPReadWebQueue, &bmp_data);
            //printf("FILA BMP CHEIA!");
        
        vTaskDelay(pdMS_TO_TICKS(700));
    }
}
