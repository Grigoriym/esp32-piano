/*
 * BLE-MIDI peripheral on NimBLE.
 *
 * One service, one characteristic (read, write without response, notify), as
 * the BLE-MIDI spec asks. Each MIDI message goes out as its own notification:
 * header (0x80 | timestamp bits 12-7), timestamp (0x80 | bits 6-0), message.
 * "Just works" pairing with bonding; a central that lost its bond is let in
 * again by deleting the old one (as in the IDF bleprph example).
 */
#include "ble_midi.h"

#include <string.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "nvs_flash.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

static const char *TAG = "ble_midi";

/* 03B80E5A-EDE8-4B33-A751-6CE34EC4C700 */
static const ble_uuid128_t MIDI_SVC_UUID =
    BLE_UUID128_INIT(0x00, 0xc7, 0xc4, 0x4e, 0xe3, 0x6c, 0x51, 0xa7, 0x33, 0x4b, 0xe8, 0xed, 0x5a, 0x0e, 0xb8, 0x03);
/* 7772E5DB-3868-4112-A1A9-F2669D106BF3 */
static const ble_uuid128_t MIDI_CHR_UUID =
    BLE_UUID128_INIT(0xf3, 0x6b, 0x10, 0x9d, 0x66, 0xf2, 0xa9, 0xa1, 0x12, 0x41, 0x68, 0x38, 0xdb, 0xe5, 0x72, 0x77);

static uint16_t chr_handle;
static uint16_t conn_handle = BLE_HS_CONN_HANDLE_NONE;
static volatile bool subscribed;
static uint8_t own_addr_type;

void ble_store_config_init(void);
static void advertise(void);

static int chr_access(uint16_t conn, uint16_t attr, struct ble_gatt_access_ctxt *ctxt, void *arg) {
    /* Reads return an empty packet; incoming MIDI is ignored. */
    return 0;
}

static const struct ble_gatt_svc_def SERVICES[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &MIDI_SVC_UUID.u,
        .characteristics =
            (struct ble_gatt_chr_def[]){
                {
                    .uuid = &MIDI_CHR_UUID.u,
                    .access_cb = chr_access,
                    .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE_NO_RSP | BLE_GATT_CHR_F_NOTIFY,
                    .val_handle = &chr_handle,
                },
                {0},
            },
    },
    {0},
};

static int gap_event(struct ble_gap_event *event, void *arg) {
    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status == 0) {
            conn_handle = event->connect.conn_handle;
            ESP_LOGI(TAG, "connected");
            /* BlueZ connects with a 420 ms supervision timeout and the link
             * kept dropping on it (reason 0x208): ask for 4 s. */
            struct ble_gap_upd_params upd = {
                .itvl_min = 6,  /* 7.5 ms */
                .itvl_max = 12, /* 15 ms */
                .latency = 0,
                .supervision_timeout = 400, /* 4 s */
            };
            ble_gap_update_params(conn_handle, &upd);
        } else {
            advertise();
        }
        return 0;
    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGI(TAG, "disconnected (reason 0x%x)", event->disconnect.reason);
        conn_handle = BLE_HS_CONN_HANDLE_NONE;
        subscribed = false;
        advertise();
        return 0;
    case BLE_GAP_EVENT_ADV_COMPLETE:
        advertise();
        return 0;
    case BLE_GAP_EVENT_SUBSCRIBE:
        if (event->subscribe.attr_handle == chr_handle) {
            subscribed = event->subscribe.cur_notify;
            ESP_LOGI(TAG, "MIDI notifications %s", subscribed ? "on" : "off");
        }
        return 0;
    case BLE_GAP_EVENT_REPEAT_PAIRING: {
        struct ble_gap_conn_desc desc;
        ble_gap_conn_find(event->repeat_pairing.conn_handle, &desc);
        ble_store_util_delete_peer(&desc.peer_id_addr);
        return BLE_GAP_REPEAT_PAIRING_RETRY;
    }
    case BLE_GAP_EVENT_CONN_UPDATE: {
        struct ble_gap_conn_desc desc;
        if (ble_gap_conn_find(event->conn_update.conn_handle, &desc) == 0) {
            ESP_LOGI(TAG, "conn params: interval %d x1.25 ms, timeout %d x10 ms (status %d)",
                     desc.conn_itvl, desc.supervision_timeout, event->conn_update.status);
        }
        return 0;
    }
    case BLE_GAP_EVENT_MTU:
        ESP_LOGI(TAG, "MTU %d", event->mtu.value);
        return 0;
    default:
        return 0;
    }
}

static void advertise(void) {
    /* Flags + the 128-bit service UUID fill most of the 31 bytes: the name goes
     * in the scan response. */
    struct ble_hs_adv_fields fields = {0};
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.uuids128 = (ble_uuid128_t *)&MIDI_SVC_UUID;
    fields.num_uuids128 = 1;
    fields.uuids128_is_complete = 1;
    int rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0) {
        ESP_LOGE(TAG, "adv fields: %d", rc);
        return;
    }

    struct ble_hs_adv_fields rsp = {0};
    const char *name = ble_svc_gap_device_name();
    rsp.name = (const uint8_t *)name;
    rsp.name_len = strlen(name);
    rsp.name_is_complete = 1;
    rc = ble_gap_adv_rsp_set_fields(&rsp);
    if (rc != 0) {
        ESP_LOGE(TAG, "scan response: %d", rc);
        return;
    }

    struct ble_gap_adv_params params = {0};
    params.conn_mode = BLE_GAP_CONN_MODE_UND;
    params.disc_mode = BLE_GAP_DISC_MODE_GEN;
    rc = ble_gap_adv_start(own_addr_type, NULL, BLE_HS_FOREVER, &params, gap_event, NULL);
    if (rc != 0 && rc != BLE_HS_EALREADY) {
        ESP_LOGE(TAG, "adv start: %d", rc);
    }
}

static void on_sync(void) {
    ble_hs_util_ensure_addr(0);
    ble_hs_id_infer_auto(0, &own_addr_type);
    advertise();
    ESP_LOGI(TAG, "advertising as \"%s\"", ble_svc_gap_device_name());
}

static void on_reset(int reason) {
    ESP_LOGE(TAG, "host reset, reason %d", reason);
}

static void host_task(void *param) {
    nimble_port_run();
    nimble_port_freertos_deinit();
}

void ble_midi_init(const char *name) {
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
    ESP_ERROR_CHECK(nimble_port_init());

    ble_hs_cfg.sync_cb = on_sync;
    ble_hs_cfg.reset_cb = on_reset;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;
    ble_hs_cfg.sm_io_cap = BLE_HS_IO_NO_INPUT_OUTPUT;
    ble_hs_cfg.sm_bonding = 1;
    ble_hs_cfg.sm_our_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_hs_cfg.sm_their_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;

    ble_svc_gap_init();
    ble_svc_gatt_init();
    ESP_ERROR_CHECK(ble_gatts_count_cfg(SERVICES));
    ESP_ERROR_CHECK(ble_gatts_add_svcs(SERVICES));
    ESP_ERROR_CHECK(ble_svc_gap_device_name_set(name));
    ble_store_config_init();

    nimble_port_freertos_init(host_task);
}

bool ble_midi_connected(void) {
    return conn_handle != BLE_HS_CONN_HANDLE_NONE && subscribed;
}

void ble_midi_send(const uint8_t *msg, size_t len) {
    if (!ble_midi_connected() || len == 0 || len > 3) {
        return;
    }
    uint32_t ts = (uint32_t)(esp_timer_get_time() / 1000) & 0x1fff;
    uint8_t pkt[5];
    pkt[0] = 0x80 | (ts >> 7);
    pkt[1] = 0x80 | (ts & 0x7f);
    memcpy(&pkt[2], msg, len);
    struct os_mbuf *om = ble_hs_mbuf_from_flat(pkt, 2 + len);
    if (om == NULL) {
        return;
    }
    ble_gatts_notify_custom(conn_handle, chr_handle, om);
}
