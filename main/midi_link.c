#include "midi_link.h"

#include <string.h>

#include "driver/uart.h"
#include "driver/uart_vfs.h"
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
static int64_t last_serial_hello_us; /* 0: never; the bridge on the USB cable */

#define SERIAL_UART UART_NUM_0 /* the console: frames go between the log lines */

static bool recent(int64_t t)
{
    return t != 0 && esp_timer_get_time() - t < HELLO_TIMEOUT_US;
}

/* "HELLO" lines from the bridge on the USB cable. */
static void serial_hello_task(void *param)
{
    char line[16];
    size_t n = 0;
    while (true) {
        uint8_t c;
        if (uart_read_bytes(SERIAL_UART, &c, 1, portMAX_DELAY) != 1) continue;
        if (c == '\n' || c == '\r') {
            if (n == 5 && memcmp(line, "HELLO", 5) == 0) {
                bool was = recent(last_serial_hello_us);
                taskENTER_CRITICAL(&lock);
                last_serial_hello_us = esp_timer_get_time();
                taskEXIT_CRITICAL(&lock);
                if (!was) ESP_LOGI(TAG, "PC bridge on the USB cable");
            }
            n = 0;
        } else if (n < sizeof(line)) {
            line[n++] = (char)c;
        }
    }
}

/* The cable: the UART driver (writes queue instead of blocking the scan), and
 * the HELLO reader. Log lines keep going out as before. */
static void serial_init(void)
{
    if (uart_driver_install(SERIAL_UART, 256, 4096, 0, NULL, 0) != ESP_OK) {
        ESP_LOGE(TAG, "no UART driver: no MIDI on the cable");
        return;
    }
    uart_vfs_dev_use_driver(SERIAL_UART);
    xTaskCreate(serial_hello_task, "midi_serial", 2560, NULL, 5, NULL);
}

/* One packet on the cable: 0xF5 (never in UTF-8 text), length, packet, sum. */
static void serial_send(const uint8_t *pkt, size_t len)
{
    uint8_t frame[16] = {0xf5, (uint8_t)len};
    uint8_t sum = 0;
    for (size_t i = 0; i < len; i++) {
        frame[2 + i] = pkt[i];
        sum += pkt[i];
    }
    frame[2 + len] = sum;
    uart_write_bytes(SERIAL_UART, frame, 3 + len); /* one call: no log text inside it */
}

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
        bool was = recent(last_hello_us);
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
    serial_init();
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

static bool wifi_connected(void)
{
    taskENTER_CRITICAL(&lock);
    int64_t t = last_hello_us;
    taskEXIT_CRITICAL(&lock);
    return recent(t);
}

static bool serial_connected(void)
{
    taskENTER_CRITICAL(&lock);
    int64_t t = last_serial_hello_us;
    taskEXIT_CRITICAL(&lock);
    return recent(t);
}

bool midi_link_connected(void)
{
    return wifi_connected() || serial_connected();
}

void midi_link_send(const uint8_t *msg, size_t len)
{
    midi_link_send_at(msg, len, 0);
}

void midi_link_send_at(const uint8_t *msg, size_t len, int64_t at_us)
{
    bool wifi = sock >= 0 && wifi_connected();
    bool cable = serial_connected();
    if (len == 0 || len > 3 || (!wifi && !cable)) {
        return;
    }
    taskENTER_CRITICAL(&lock);
    struct sockaddr_in to = peer;
    taskEXIT_CRITICAL(&lock);
    int64_t now = esp_timer_get_time();
    int64_t delay = (at_us - now) / 100;
    delay = delay < 0 ? 0 : delay > 0xffff ? 0xffff : delay;
    uint32_t sent = (uint32_t)now;
    uint8_t pkt[11] = {
        seq >> 8,           seq & 0xff,  sent >> 24, (sent >> 16) & 0xff,
        (sent >> 8) & 0xff, sent & 0xff, delay >> 8, delay & 0xff,
    };
    memcpy(&pkt[8], msg, len);
    seq++;
    /* Both ways when both are up, the same sequence number: the bridge plays
     * whichever comes first (the cable) and drops the rest. */
    if (cable) serial_send(pkt, 8 + len);
    for (int i = 0; wifi && i < COPIES; i++) {
        sendto(sock, pkt, 8 + len, 0, (struct sockaddr *)&to, sizeof(to));
    }
}
