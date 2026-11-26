#include "dns_server.h"
#include <string.h>
#include "lwip/sockets.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

static const char *TAG = "dns";
static volatile bool s_run;
static TaskHandle_t s_task;

static void dns_task(void *arg)
{
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) { ESP_LOGE(TAG, "No se pudo crear el socket"); s_task = NULL; vTaskDelete(NULL); }

    struct sockaddr_in addr = { .sin_family = AF_INET, .sin_port = htons(53),
                                .sin_addr.s_addr = htonl(INADDR_ANY) };
    if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        ESP_LOGE(TAG, "No se pudo abrir el puerto 53");
        close(sock); s_task = NULL; vTaskDelete(NULL);
    }
    struct timeval tv = { .tv_sec = 1 };
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    ESP_LOGI(TAG, "Portal cautivo: DNS activo");

    uint8_t buf[512];
    while (s_run) {
        struct sockaddr_in from;
        socklen_t flen = sizeof(from);
        int n = recvfrom(sock, buf, sizeof(buf) - 16, 0, (struct sockaddr *)&from, &flen);
        if (n < 12) continue;

        /* Recorrer el nombre de la primera pregunta */
        int p = 12;
        while (p < n && buf[p] != 0) p += buf[p] + 1;
        p += 1;                                  /* byte 0 final del nombre */
        if (p + 4 > n) continue;
        uint16_t qtype = (buf[p] << 8) | buf[p + 1];
        p += 4;                                  /* QTYPE + QCLASS */

        buf[2] = 0x81; buf[3] = 0x80;            /* respuesta, sin error */
        buf[4] = 0; buf[5] = 1;                  /* 1 pregunta */
        buf[8] = 0; buf[9] = 0;                  /* sin autoridad */
        buf[10] = 0; buf[11] = 0;                /* sin adicionales */
        if (qtype == 1) {                        /* registro A */
            buf[6] = 0; buf[7] = 1;
            const uint8_t ans[16] = { 0xC0, 0x0C, 0, 1, 0, 1, 0, 0, 0, 60, 0, 4,
                                      192, 168, 4, 1 };
            memcpy(&buf[p], ans, sizeof(ans));
            p += sizeof(ans);
        } else {
            buf[6] = 0; buf[7] = 0;
        }
        sendto(sock, buf, p, 0, (struct sockaddr *)&from, flen);
    }
    close(sock);
    ESP_LOGI(TAG, "DNS detenido");
    s_task = NULL;
    vTaskDelete(NULL);
}

void dns_server_start(void)
{
    s_run = true;
    if (s_task) return;
    xTaskCreate(dns_task, "dns", 3072, NULL, 4, &s_task);
}

void dns_server_stop(void)
{
    s_run = false;     /* la tarea termina sola en ≤ 1 s */
}
