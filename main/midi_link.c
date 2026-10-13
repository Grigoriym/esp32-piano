#include "midi_link.h"

#include <string.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/sockets.h"
#include "mdns.h"

#define HELLO_TIMEOUT_US 3000000LL
#define COPIES           2

static const char *TAG = "midi_link";

static portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
static struct sockaddr_in peer; /* where the last HELLO came from */
static int64_t last_hello_us;   /* 0: never */
static int sock = -1;           /* replies go out from the HELLO port too: the PC's
                       * firewall only lets them in as replies */
static uint16_t seq;

static void hello_task(void *param)
{
    char buf[16];
    while (true) {
        struct sockaddr_in from;
        socklen_t from_len = sizeof(from);
        int n = recvfrom(sock, buf, sizeof(buf), 0, (struct sockaddr *)&from, &from_len);
        if (n != 5 || memcmp(buf, "HELLO", 5) != 0) {
            continue;
        }
        bool was = midi_link_connected();
        taskENTER_CRITICAL(&lock);
        peer = from;
        last_hello_us = esp_timer_get_time();
        taskEXIT_CRITICAL(&lock);
        if (!was) {
            ESP_LOGI(TAG, "PC bridge at %s:%u", inet_ntoa(from.sin_addr), ntohs(from.sin_port));
        }
    }
}

void midi_link_init(const char *hostname)
{
    if (mdns_init() == ESP_OK) {
        mdns_hostname_set(hostname);
        mdns_service_add(NULL, "_chicco-midi", "_udp", MIDI_LINK_PORT, NULL, 0);
    } else {
        ESP_LOGW(TAG, "mDNS failed: the PC needs the IP");
    }
    sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_port = htons(MIDI_LINK_PORT),
        .sin_addr.s_addr = htonl(INADDR_ANY),
    };
    if (sock < 0 || bind(sock, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        ESP_LOGE(TAG, "can't listen on UDP %d", MIDI_LINK_PORT);
        sock = -1;
        return;
    }
    xTaskCreate(hello_task, "midi_hello", 3072, NULL, 5, NULL);
}

bool midi_link_connected(void)
{
    taskENTER_CRITICAL(&lock);
    int64_t t = last_hello_us;
    taskEXIT_CRITICAL(&lock);
    return t != 0 && esp_timer_get_time() - t < HELLO_TIMEOUT_US;
}

void midi_link_send(const uint8_t *msg, size_t len)
{
    if (sock < 0 || len == 0 || len > 3 || !midi_link_connected()) {
        return;
    }
    taskENTER_CRITICAL(&lock);
    struct sockaddr_in to = peer;
    taskEXIT_CRITICAL(&lock);
    uint8_t pkt[5] = {seq >> 8, seq & 0xff};
    memcpy(&pkt[2], msg, len);
    seq++;
    for (int i = 0; i < COPIES; i++) {
        sendto(sock, pkt, 2 + len, 0, (struct sockaddr *)&to, sizeof(to));
    }
}
