#include "dashboard_ha.h"
#include "esp_websocket_client.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs.h"
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define HA_WS_TOKEN_CAP 512
#define HA_WS_MESSAGE_CAP 131072
static const char *TAG = "DASH_HA_LIVE";
static esp_websocket_client_handle_t s_ws;
static SemaphoreHandle_t s_mutex;
static dashboard_ha_snapshot_t s_snapshot;
static char *s_message;
static int s_received, s_expected;
static bool s_started;
static bool s_person_detected;

static dashboard_ha_entity_t s_watched[DASH_HA_WATCH_MAX];
static unsigned s_watched_count;

void dashboard_ha_watch_set(const char ids[DASH_HA_WATCH_MAX][80], unsigned count)
{
    if (!s_mutex) return;
    if (count > DASH_HA_WATCH_MAX) count = DASH_HA_WATCH_MAX;
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    dashboard_ha_entity_t previous[DASH_HA_WATCH_MAX];
    memcpy(previous, s_watched, sizeof(previous));
    unsigned old_count = s_watched_count;
    memset(s_watched, 0, sizeof(s_watched));
    s_watched_count = count;
    for (unsigned i = 0; i < count; ++i) {
        snprintf(s_watched[i].entity_id, sizeof(s_watched[i].entity_id), "%s", ids[i]);
        for (unsigned j = 0; j < old_count; ++j)
            if (!strcmp(ids[i], previous[j].entity_id)) {
                snprintf(s_watched[i].state, sizeof(s_watched[i].state), "%s", previous[j].state);
                break;
            }
    }
    xSemaphoreGive(s_mutex);
    if (s_ws && dashboard_ha_is_connected()) {
        const char *request = "{\"id\":4,\"type\":\"get_states\"}";
        esp_websocket_client_send_text(s_ws, request, strlen(request), pdMS_TO_TICKS(1000));
    }
}

bool dashboard_ha_watch_get(const char *id, char *out, unsigned capacity)
{
    if (!id || !out || !capacity) return false;
    out[0] = 0;
    if (!s_mutex) return false;
    bool found = false;
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    if (s_snapshot.connected)
        for (unsigned i = 0; i < s_watched_count; ++i)
            if (!strcmp(id, s_watched[i].entity_id) && s_watched[i].state[0]) {
                snprintf(out, capacity, "%s", s_watched[i].state);
                found = true;
                break;
            }
    xSemaphoreGive(s_mutex);
    return found;
}


bool dashboard_ha_person_detected(void)
{
    if (!s_mutex) return false;
    xSemaphoreTake(s_mutex,portMAX_DELAY);
    bool detected=s_snapshot.connected && s_person_detected;
    xSemaphoreGive(s_mutex);
    return detected;
}

void dashboard_ha_live_snapshot(dashboard_ha_snapshot_t *out)
{
    if (!out) return;
    memset(out,0,sizeof(*out));
    if (!s_mutex) return;
    xSemaphoreTake(s_mutex,portMAX_DELAY);
    *out=s_snapshot;
    xSemaphoreGive(s_mutex);
}

static void save_entity(const char *id, const char *state, const char *friendly, const char *unit)
{
    if (!id || !state) return;
    if (strncmp(id,"light.",6) && strncmp(id,"switch.",7) &&
        strncmp(id,"sensor.",7) && strncmp(id,"binary_sensor.",14) &&
        strncmp(id,"climate.",8)) return;
    xSemaphoreTake(s_mutex,portMAX_DELAY);
    if (!strcmp(id,"binary_sensor.doorcam_chelovek")) {
        bool detected=!strcmp(state,"on");
        if (detected!=s_person_detected)
            ESP_LOGI(TAG,"DoorCam person: %s",state);
        s_person_detected=detected;
    }
    for (unsigned k = 0; k < s_watched_count; ++k)
        if (!strcmp(s_watched[k].entity_id, id))
            snprintf(s_watched[k].state, sizeof(s_watched[k].state),
                     "%s%s", state, unit ? unit : "");
    for (unsigned k=0;k<s_snapshot.panel_count;k++) {
        dashboard_ha_entity_t *card=&s_snapshot.panel_cards[k];
        if (!strcmp(card->entity_id,id)) {
            snprintf(card->state,sizeof(card->state),"%s%s",state,unit?unit:"");
            if (friendly && (!card->title[0] || !strcmp(card->title,id)))
                snprintf(card->title,sizeof(card->title),"%s",friendly);
        }
    }
    unsigned index=0;
    for (;index<s_snapshot.count;index++)
        if (!strcmp(s_snapshot.entities[index].entity_id,id)) break;
    if (index==s_snapshot.count) {
        if (s_snapshot.count<DASH_HA_MAX_ENTITIES) s_snapshot.count++;
        else {
            /* Keep the initial eight cards stable. Never rotate the whole
             * grid when HA reports another entity or a state changes. */
            xSemaphoreGive(s_mutex);
            return;
        }
    }
    if (strcmp(s_snapshot.entities[index].entity_id,id) != 0)
        snprintf(s_snapshot.entities[index].entity_id,sizeof(s_snapshot.entities[index].entity_id),"%s",id);
    if (strcmp(s_snapshot.entities[index].state,state) != 0)
        snprintf(s_snapshot.entities[index].state,sizeof(s_snapshot.entities[index].state),"%s%s",state,unit?unit:"");
    if (friendly)
        snprintf(s_snapshot.entities[index].title,sizeof(s_snapshot.entities[index].title),"%s",friendly);
    xSemaphoreGive(s_mutex);
}

static void parse_state(cJSON *item)
{
    if (!cJSON_IsObject(item)) return;
    cJSON *id=cJSON_GetObjectItemCaseSensitive(item,"entity_id");
    cJSON *state=cJSON_GetObjectItemCaseSensitive(item,"state");
    cJSON *attrs=cJSON_GetObjectItemCaseSensitive(item,"attributes");
    cJSON *friendly=cJSON_GetObjectItemCaseSensitive(attrs,"friendly_name");
    cJSON *unit=cJSON_GetObjectItemCaseSensitive(attrs,"unit_of_measurement");
    if (cJSON_IsString(id) && cJSON_IsString(state))
        save_entity(id->valuestring,state->valuestring,
                    cJSON_IsString(friendly)?friendly->valuestring:NULL,
                    cJSON_IsString(unit)?unit->valuestring:NULL);
}


static void panel_add(cJSON *card, dashboard_ha_snapshot_t *panel)
{
    if (!cJSON_IsObject(card) || panel->panel_count >= DASH_HA_MAX_ENTITIES) return;
    cJSON *type=cJSON_GetObjectItemCaseSensitive(card,"type");
    cJSON *entity=cJSON_GetObjectItemCaseSensitive(card,"entity");
    cJSON *title=cJSON_GetObjectItemCaseSensitive(card,"name");
    if (!cJSON_IsString(title)) title=cJSON_GetObjectItemCaseSensitive(card,"title");
    if (cJSON_IsString(type) && !strcmp(type->valuestring,"entities")) {
        cJSON *items=cJSON_GetObjectItemCaseSensitive(card,"entities");
        cJSON *entry=NULL;
        cJSON_ArrayForEach(entry,items) {
            if (panel->panel_count>=DASH_HA_MAX_ENTITIES) break;
            cJSON *eid=cJSON_IsString(entry)?entry:cJSON_GetObjectItemCaseSensitive(entry,"entity");
            cJSON *ename=cJSON_IsObject(entry)?cJSON_GetObjectItemCaseSensitive(entry,"name"):NULL;
            if (!cJSON_IsString(eid)) continue;
            dashboard_ha_entity_t *dst=&panel->panel_cards[panel->panel_count++];
            snprintf(dst->entity_id,sizeof(dst->entity_id),"%s",eid->valuestring);
            snprintf(dst->title,sizeof(dst->title),"%s",cJSON_IsString(ename)?ename->valuestring:eid->valuestring);
        }
    } else if (cJSON_IsString(type) && cJSON_IsString(entity) &&
               (!strcmp(type->valuestring,"tile") || !strcmp(type->valuestring,"sensor") ||
                !strcmp(type->valuestring,"button"))) {
        dashboard_ha_entity_t *dst=&panel->panel_cards[panel->panel_count++];
        snprintf(dst->entity_id,sizeof(dst->entity_id),"%s",entity->valuestring);
        snprintf(dst->title,sizeof(dst->title),"%s",cJSON_IsString(title)?title->valuestring:entity->valuestring);
    }
}

static void panel_collect(cJSON *node, dashboard_ha_snapshot_t *panel, int depth)
{
    if (depth>5 || panel->panel_count>=DASH_HA_MAX_ENTITIES || !cJSON_IsObject(node)) return;
    cJSON *cards=cJSON_GetObjectItemCaseSensitive(node,"cards");
    cJSON *item=NULL;
    cJSON_ArrayForEach(item,cards) {
        panel_add(item,panel);
        panel_collect(item,panel,depth+1);
    }
    cJSON *sections=cJSON_GetObjectItemCaseSensitive(node,"sections");
    cJSON_ArrayForEach(item,sections) panel_collect(item,panel,depth+1);
}

static void parse_panel_config(cJSON *config)
{
    if (!cJSON_IsObject(config)) return;
    dashboard_ha_snapshot_t panel={0};
    cJSON *views=cJSON_GetObjectItemCaseSensitive(config,"views");
    cJSON *first=cJSON_GetArrayItem(views,0);
    panel_collect(first,&panel,0);
    xSemaphoreTake(s_mutex,portMAX_DELAY);
    for (unsigned i=0;i<panel.panel_count;i++) {
        for (unsigned j=0;j<s_snapshot.panel_count;j++) {
            if (!strcmp(panel.panel_cards[i].entity_id,s_snapshot.panel_cards[j].entity_id)) {
                snprintf(panel.panel_cards[i].state,sizeof(panel.panel_cards[i].state),"%s",s_snapshot.panel_cards[j].state);
                if (!strcmp(panel.panel_cards[i].title,panel.panel_cards[i].entity_id))
                    snprintf(panel.panel_cards[i].title,sizeof(panel.panel_cards[i].title),"%s",s_snapshot.panel_cards[j].title);
                break;
            }
        }
    }
    s_snapshot.panel_count=panel.panel_count;
    memcpy(s_snapshot.panel_cards,panel.panel_cards,sizeof(panel.panel_cards));
    s_snapshot.panel_loaded=true;
    xSemaphoreGive(s_mutex);
    ESP_LOGI(TAG,"DoorCam Panel config: %u supported entities",panel.panel_count);
}

static void process_message(char *message)
{
    cJSON *root=cJSON_Parse(message);
    if (!root) return;
    cJSON *type=cJSON_GetObjectItemCaseSensitive(root,"type");
    if (!cJSON_IsString(type)) { cJSON_Delete(root); return; }
    if (!strcmp(type->valuestring,"auth_required")) {
        char token[HA_WS_TOKEN_CAP]={0};
        nvs_handle_t h;
        if (nvs_open("doorcam_ha",NVS_READONLY,&h)==ESP_OK) {
            size_t n=sizeof(token);
            if (nvs_get_str(h,"token",token,&n)==ESP_OK) {
                cJSON *auth=cJSON_CreateObject();
                cJSON_AddStringToObject(auth,"type","auth");
                cJSON_AddStringToObject(auth,"access_token",token);
                char *payload=cJSON_PrintUnformatted(auth);
                if (payload) {
                    esp_websocket_client_send_text(s_ws,payload,strlen(payload),pdMS_TO_TICKS(1000));
                    memset(payload,0,strlen(payload)); free(payload);
                }
                cJSON_Delete(auth);
            }
            nvs_close(h);
        }
        memset(token,0,sizeof(token));
    } else if (!strcmp(type->valuestring,"auth_ok")) {
        xSemaphoreTake(s_mutex,portMAX_DELAY);
        s_snapshot.connected=true;
        xSemaphoreGive(s_mutex);
        ESP_LOGI(TAG,"Home Assistant WebSocket authenticated");
        const char *subscribe="{\"id\":1,\"type\":\"subscribe_events\",\"event_type\":\"state_changed\"}";
        esp_websocket_client_send_text(s_ws,subscribe,strlen(subscribe),pdMS_TO_TICKS(1000));
        const char *get_panel="{\"id\":3,\"type\":\"lovelace/config\",\"url_path\":\"doorcam-panel\"}";
        esp_websocket_client_send_text(s_ws,get_panel,strlen(get_panel),pdMS_TO_TICKS(1000));
    } else if (!strcmp(type->valuestring,"auth_invalid")) {
        ESP_LOGW(TAG,"Home Assistant WebSocket authentication rejected");
    } else if (!strcmp(type->valuestring,"result")) {
        cJSON *id=cJSON_GetObjectItemCaseSensitive(root,"id");
        cJSON *success=cJSON_GetObjectItemCaseSensitive(root,"success");
        if (cJSON_IsNumber(id) && id->valueint==3) {
            if (cJSON_IsTrue(success)) {
                parse_panel_config(cJSON_GetObjectItemCaseSensitive(root,"result"));
            }
            else ESP_LOGW(TAG,"DoorCam Panel config unavailable (check dashboard URL path and permissions)");
            /* Request the full state snapshot only once, after panel config.
             * Previously auth_ok and panel config both requested large JSON
             * snapshots, increasing PSRAM traffic during RGB scanout. */
            const char *states_request="{\"id\":4,\"type\":\"get_states\"}";
            ESP_LOGI(TAG,"Requesting one initial HA state snapshot");
            esp_websocket_client_send_text(s_ws,states_request,strlen(states_request),pdMS_TO_TICKS(1000));
        }
        if (cJSON_IsNumber(id) && (id->valueint==2 || id->valueint==4) && cJSON_IsTrue(success)) {
            cJSON *states=cJSON_GetObjectItemCaseSensitive(root,"result");
            if (cJSON_IsArray(states)) {
                int64_t started_us = esp_timer_get_time();
                unsigned processed = 0;
                cJSON *item=NULL;
                cJSON_ArrayForEach(item,states) {
                    parse_state(item);
                    processed++;
                }
                ESP_LOGI(TAG,"HA states processed: %u, elapsed=%lld ms",
                         processed, (long long)((esp_timer_get_time()-started_us)/1000));
            }
        }
    } else if (!strcmp(type->valuestring,"event")) {
        cJSON *event=cJSON_GetObjectItemCaseSensitive(root,"event");
        cJSON *data=cJSON_GetObjectItemCaseSensitive(event,"data");
        cJSON *next=cJSON_GetObjectItemCaseSensitive(data,"new_state");
        cJSON *eid=cJSON_GetObjectItemCaseSensitive(data,"entity_id");
        if (cJSON_IsNull(next) && cJSON_IsString(eid) &&
            !strcmp(eid->valuestring,"binary_sensor.doorcam_chelovek"))
            save_entity(eid->valuestring,"unavailable",NULL,NULL);
        else parse_state(next);
    }
    cJSON_Delete(root);
}

static void ws_event(void *arg, esp_event_base_t base, int32_t event_id, void *event_data)
{
    (void)arg; (void)base;
    esp_websocket_event_data_t *e=(esp_websocket_event_data_t *)event_data;
    if (event_id==WEBSOCKET_EVENT_DISCONNECTED || event_id==WEBSOCKET_EVENT_ERROR) {
        xSemaphoreTake(s_mutex,portMAX_DELAY);
        s_snapshot.connected=false;
        s_person_detected=false;
        for (unsigned i = 0; i < s_watched_count; ++i) s_watched[i].state[0] = 0;
        xSemaphoreGive(s_mutex);
    } else if (event_id==WEBSOCKET_EVENT_DATA && e && e->op_code==0x1) {
        /* Reassemble text frames, including fragmented TCP/WebSocket delivery. */
        if (e->payload_offset==0) {
            s_received=0; s_expected=e->payload_len;
        }
        if (s_message && e->payload_offset==s_received &&
            s_expected>0 && s_expected<HA_WS_MESSAGE_CAP &&
            e->data_len>=0 && s_received+e->data_len<=s_expected) {
            memcpy(s_message+s_received,e->data_ptr,e->data_len);
            s_received+=e->data_len;
            if (s_received==s_expected) {
                s_message[s_received]=0;
                process_message(s_message);
                s_received=0; s_expected=0;
            }
        }
    }
}

esp_err_t dashboard_ha_live_start(void)
{
    if (s_started) return ESP_OK;
    if (!dashboard_ha_has_token()) return ESP_ERR_NOT_FOUND;
    s_mutex=xSemaphoreCreateMutex();
    if (!s_mutex) return ESP_ERR_NO_MEM;
    s_message=malloc(HA_WS_MESSAGE_CAP);
    if (!s_message) return ESP_ERR_NO_MEM;
    esp_websocket_client_config_t cfg={
        .uri="ws://192.168.0.215:8123/api/websocket",
        .buffer_size=2048,
        .reconnect_timeout_ms=5000,
        .network_timeout_ms=10000,
        .task_stack=8192,
    };
    s_ws=esp_websocket_client_init(&cfg);
    if (!s_ws) return ESP_FAIL;
    esp_err_t err=esp_websocket_register_events(s_ws,WEBSOCKET_EVENT_ANY,ws_event,NULL);
    if (err!=ESP_OK) return err;
    err=esp_websocket_client_start(s_ws);
    if (err==ESP_OK) s_started=true;
    return err;
}


bool dashboard_ha_is_connected(void)
{
    if (!s_mutex) return false;
    xSemaphoreTake(s_mutex,portMAX_DELAY);
    bool connected=s_snapshot.connected;
    xSemaphoreGive(s_mutex);
    return connected;
}
