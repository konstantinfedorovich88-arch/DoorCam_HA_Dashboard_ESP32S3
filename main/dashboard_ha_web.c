#include "dashboard_ha.h"
#include "esp_http_server.h"
#include "esp_random.h"
#include "esp_log.h"
#include "nvs.h"
#include "cJSON.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

static const char *TAG = "DASH_HA_WEB";
static httpd_handle_t s_server;
static char s_code[13];
static unsigned s_failures;

static const char PAGE[] =
"<!doctype html><html lang='en'><meta charset='utf-8'>"
"<meta name='viewport' content='width=device-width,initial-scale=1'>"
"<title>DoorCam Home Assistant setup</title>"
"<style>body{font:16px system-ui;background:#142033;color:#fff;max-width:520px;margin:5vh auto;padding:20px}"
"input,button{box-sizing:border-box;width:100%;padding:13px;margin:8px 0 18px;border-radius:8px;border:1px solid #778}"
"button{background:#2585d0;color:white;border:0}small{color:#ccc}</style>"
"<h2>DoorCam - Home Assistant</h2>"
"<p>Server: http://192.168.0.215:8123</p>"
"<form action='/ha/save' method='POST'>"
"<label>Setup code (shown on dashboard display)</label>"
"<input name='code' autocomplete='off' required maxlength='12'>"
"<label>Long-Lived Access Token</label>"
"<input name='token' type='password' autocomplete='off' required maxlength='511'>"
"<button type='submit'>Save token</button></form>"
"<small>Local HTTP is not encrypted. Use only on a trusted private Wi-Fi network."
" Never expose this setup page to the internet.</small></html>";

static esp_err_t page_get(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, PAGE, HTTPD_RESP_USE_STRLEN);
}

static int hexval(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static bool decode_form_value(const char *body, const char *key, char *out, size_t cap)
{
    size_t klen = strlen(key);
    const char *p = body;
    while (*p) {
        const char *end = strchr(p, '&');
        if (!end) end = p + strlen(p);
        if ((size_t)(end-p) > klen && !memcmp(p,key,klen) && p[klen]=='=') {
            p += klen+1;
            size_t n = 0;
            while (p < end) {
                unsigned char c = (unsigned char)*p++;
                if (c == '+') c = ' ';
                else if (c == '%') {
                    if (end-p < 2) return false;
                    int a=hexval(p[0]),b=hexval(p[1]);
                    if (a<0 || b<0) return false;
                    c = (unsigned char)((a<<4)|b);
                    p+=2;
                }
                if (!c || n+1 >= cap) return false;
                out[n++] = (char)c;
            }
            out[n]=0;
            return true;
        }
        p = *end ? end+1 : end;
    }
    return false;
}

static esp_err_t save_post(httpd_req_t *req)
{
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    if (s_failures >= 5) {
        httpd_resp_set_status(req, "429 Too Many Requests");
        return httpd_resp_sendstr(req, "Setup locked until reboot");
    }
    if (req->content_len <= 0 || req->content_len > 1200) {
        httpd_resp_set_status(req, "413 Payload Too Large");
        return httpd_resp_sendstr(req, "Invalid request size");
    }
    char *body = calloc((size_t)req->content_len + 1,1);
    if (!body) return httpd_resp_send_500(req);
    int total=0;
    while (total < req->content_len) {
        int n = httpd_req_recv(req,body+total,req->content_len-total);
        if (n <= 0) { free(body); return ESP_FAIL; }
        total += n;
    }
    char code[16]={0};
    char token[512]={0};
    bool valid = decode_form_value(body,"code",code,sizeof(code)) &&
                 decode_form_value(body,"token",token,sizeof(token)) &&
                 strlen(code)==12 && strcmp(code,s_code)==0;
    memset(body,0,(size_t)req->content_len);
    free(body);
    if (!valid) {
        s_failures++;
        memset(token,0,sizeof(token));
        httpd_resp_set_status(req, "403 Forbidden");
        return httpd_resp_sendstr(req, "Incorrect setup code or invalid form");
    }
    esp_err_t err = dashboard_ha_save_token(token);
    memset(token,0,sizeof(token));
    if (err != ESP_OK) {
        httpd_resp_set_status(req, "400 Bad Request");
        return httpd_resp_sendstr(req, "Could not save token; check its length");
    }
    /* One successful provisioning per boot: reboot to replace the token. */
    s_failures=5;
    httpd_resp_set_type(req,"text/html; charset=utf-8");
    return httpd_resp_sendstr(req, "<h2>Token saved</h2><p>Use Test auth on the dashboard.</p>");
}


static const char EDITOR[] =
"<!doctype html><html lang='ru'><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>"
"<title>DoorCam — карточки</title><style>body{font:16px system-ui;background:#101827;color:#fff;max-width:850px;margin:auto;padding:20px}"
"input,select,button{box-sizing:border-box;padding:10px;border-radius:8px;border:1px solid #61718a;background:#24354b;color:white}"
"input{width:100%}button{cursor:pointer;background:#2563eb}button:disabled{opacity:.5}"
".grid{display:grid;grid-template-columns:1fr 1fr;gap:10px}.card{background:#24354b;padding:15px;border-radius:10px;cursor:pointer}"
".card.sel{outline:2px solid #60a5fa}.wide{grid-column:span 2}.tools{display:flex;flex-wrap:wrap;gap:8px;margin:12px 0}"
"label{display:block;margin:10px 0 5px}small{color:#cbd5e1}</style>"
"<h2>DoorCam — главный экран</h2><p>Редактор карточек Home Assistant</p>"
"<label>Код настройки с дисплея</label><input id='code' maxlength='12' autocomplete='off' placeholder='12 символов'>"
"<div class='tools'><button onclick='load()'>Загрузить</button><button onclick='save()'>Сохранить в панели</button>"
"<button onclick='add()'>+ Карточка</button></div><div id='grid' class='grid'></div>"
"<h3>Выбранная карточка</h3><label>Название</label><input id='title' maxlength='63' oninput='edit()'>"
"<label>Entity ID Home Assistant</label><input id='entity' maxlength='79' oninput='edit()' placeholder='sensor.temperature'>"
"<label>Второй датчик (влажность)</label><input id='entity2' maxlength='79' oninput='edit()' placeholder='sensor.humidity'>"
"<label>Размер</label><select id='size' onchange='edit()'><option value='normal'>Обычная</option><option value='wide'>Широкая</option></select>"
"<div class='tools'><button onclick='move(-1)'>↑ Выше</button><button onclick='move(1)'>↓ Ниже</button>"
"<button onclick='removeCard()'>Удалить</button></div><p id='status'></p>"
"<small>Только локальная сеть. HTTP не шифрует данные. Код настройки не сохраняется в браузере.</small>"
"<script>let cards=[],selected=-1;"
"const $=id=>document.getElementById(id);"
"function status(t){$('status').textContent=t}"
"function drawGrid(){const g=$('grid');g.replaceChildren();cards.forEach((c,i)=>{"
"const d=document.createElement('div');d.className='card '+(c.size==='wide'?'wide ':'')+(i===selected?'sel':'');"
"const b=document.createElement('b');b.textContent=c.title||'Без названия';"
"const v=document.createElement('p');v.textContent=[c.entity_id,c.entity_id_2].filter(Boolean).join(' + ')||'Выберите датчик';"
"d.append(b,v);d.onclick=()=>{selected=i;draw()};g.append(d)});"
"function draw(){drawGrid();const c=cards[selected];$('title').value=c?c.title:'';$('entity').value=c?c.entity_id:'';$('entity2').value=c?(c.entity_id_2||''):'';"
"$('size').value=c?c.size:'normal'}"
"function edit(){if(selected<0)return;const c=cards[selected];c.title=$('title').value;"
"c.entity_id=$('entity').value;c.entity_id_2=$('entity2').value;c.size=$('size').value;drawGrid()}"
"function add(){if(cards.length>=8){status('Максимум 8 карточек');return}"
"cards.push({title:'Новая карточка',entity_id:'',entity_id_2:'',size:'normal'});selected=cards.length-1;draw()}"
"function move(n){let j=selected+n;if(selected<0||j<0||j>=cards.length)return;"
"[cards[selected],cards[j]]=[cards[j],cards[selected]];selected=j;draw()}"
"function removeCard(){if(selected<0)return;cards.splice(selected,1);selected=Math.min(selected,cards.length-1);draw()}"
"async function load(){try{let r=await fetch('/dashboard/config',{cache:'no-store'});"
"if(!r.ok)throw Error('HTTP '+r.status);let data=await r.json();cards=data.cards||[];"
"selected=cards.length?0:-1;draw();status('Конфигурация загружена')}"
"catch(e){status('Ошибка загрузки: '+e.message)}}"
"async function save(){let code=$('code').value.trim();if(!/^[0-9A-Fa-f]{12}$/.test(code)){status('Введите 12-значный код с дисплея');return}"
"try{let r=await fetch('/dashboard/config',{method:'POST',headers:{'Content-Type':'application/json','X-DoorCam-Code':code},"
"body:JSON.stringify({cards})});let t=await r.text();status(r.ok?'Сохранено в панели':('Ошибка: '+t))}"
"catch(e){status('Ошибка сети: '+e.message)}}load();</script></html>";

#define DASH_CARDS_MAX 8
#define DASH_CONFIG_MAX 2048
static const char *DASH_DEFAULT_CARDS =
    "{\"cards\":[{\"title\":\"Погода\",\"entity_id\":\"\",\"size\":\"wide\"},"
    "{\"title\":\"На улице\",\"entity_id\":\"\",\"size\":\"normal\"},"
    "{\"title\":\"В доме\",\"entity_id\":\"\",\"size\":\"normal\"},"
    "{\"title\":\"Влажность\",\"entity_id\":\"\",\"size\":\"normal\"}]}";

static esp_err_t editor_get(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, EDITOR, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t config_get(httpd_req_t *req)
{
    char *data = malloc(DASH_CONFIG_MAX);
    if (!data) return httpd_resp_send_500(req);
    nvs_handle_t nvs;
    size_t len = DASH_CONFIG_MAX;
    esp_err_t err = nvs_open("doorcam_cards", NVS_READONLY, &nvs);
    if (err == ESP_OK) {
        err = nvs_get_str(nvs, "layout", data, &len);
        nvs_close(nvs);
    }
    if (err != ESP_OK) snprintf(data, DASH_CONFIG_MAX, "%s", DASH_DEFAULT_CARDS);
    httpd_resp_set_type(req, "application/json; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    err = httpd_resp_sendstr(req, data);
    free(data);
    return err;
}

static bool valid_card_string(const char *s, size_t max_len)
{
    if (!s || strlen(s) > max_len) return false;
    for (const unsigned char *p = (const unsigned char *)s; *p; ++p)
        if (*p < 0x20 || *p == 0x7f) return false;
    return true;
}

static esp_err_t config_post(httpd_req_t *req)
{
    const char *code = httpd_req_get_hdr_value_len(req, "X-DoorCam-Code") == 12
                       ? NULL : "";
    char provided[16] = {0};
    if (!code && httpd_req_get_hdr_value_str(req, "X-DoorCam-Code",
                                             provided, sizeof(provided)) == ESP_OK)
        code = provided;
    if (!code || strcmp(code, s_code) != 0 || !s_code[0]) {
        httpd_resp_set_status(req, "403 Forbidden");
        return httpd_resp_sendstr(req, "Incorrect setup code");
    }
    if (req->content_len < 2 || req->content_len >= DASH_CONFIG_MAX) {
        httpd_resp_set_status(req, "413 Payload Too Large");
        return httpd_resp_sendstr(req, "Configuration too large");
    }
    char *body = calloc((size_t)req->content_len + 1, 1);
    if (!body) return httpd_resp_send_500(req);
    int got = 0;
    while (got < req->content_len) {
        int n = httpd_req_recv(req, body + got, req->content_len - got);
        if (n <= 0) { free(body); return ESP_FAIL; }
        got += n;
    }
    cJSON *root = cJSON_Parse(body);
    cJSON *cards = cJSON_GetObjectItemCaseSensitive(root, "cards");
    bool valid = cJSON_IsObject(root) && cJSON_IsArray(cards) &&
                 cJSON_GetArraySize(cards) <= DASH_CARDS_MAX;
    cJSON *card = NULL;
    if (valid) cJSON_ArrayForEach(card, cards) {
        cJSON *title = cJSON_GetObjectItemCaseSensitive(card, "title");
        cJSON *entity = cJSON_GetObjectItemCaseSensitive(card, "entity_id");
        cJSON *secondary = cJSON_GetObjectItemCaseSensitive(card, "entity_id_2");
        cJSON *size = cJSON_GetObjectItemCaseSensitive(card, "size");
        if (!cJSON_IsObject(card) || !cJSON_IsString(title) ||
            !cJSON_IsString(entity) || !cJSON_IsString(size) ||
            !valid_card_string(title->valuestring, 63) ||
            !valid_card_string(entity->valuestring, 79) ||
            (secondary && (!cJSON_IsString(secondary) ||
             !valid_card_string(secondary->valuestring, 79) ||
             (secondary->valuestring[0] && !strchr(secondary->valuestring, '.')))) ||
            (entity->valuestring[0] && !strchr(entity->valuestring, '.')) ||
            (strcmp(size->valuestring, "normal") && strcmp(size->valuestring, "wide"))) {
            valid = false;
            break;
        }
    }
    if (!valid) {
        cJSON_Delete(root);
        free(body);
        httpd_resp_set_status(req, "400 Bad Request");
        return httpd_resp_sendstr(req, "Invalid cards configuration");
    }
    /* Store the validated JSON as-is. The layout is not yet consumed by LVGL. */
    nvs_handle_t nvs;
    esp_err_t err = nvs_open("doorcam_cards", NVS_READWRITE, &nvs);
    if (err == ESP_OK) {
        err = nvs_set_str(nvs, "layout", body);
        if (err == ESP_OK) err = nvs_commit(nvs);
        nvs_close(nvs);
    }
    cJSON_Delete(root);
    free(body);
    if (err != ESP_OK) return httpd_resp_send_500(req);
    ESP_LOGI(TAG, "Dashboard card layout saved");
    return httpd_resp_sendstr(req, "Saved");
}

void dashboard_ha_setup_code(char *out, unsigned capacity)
{
    if (!out || !capacity) return;
    snprintf(out,capacity,"%s",s_code[0]?s_code:"Not started");
}

esp_err_t dashboard_ha_web_start(void)
{
    if (s_server) return ESP_OK;
    for (unsigned i=0;i<3;i++)
        snprintf(s_code+i*4,5,"%04X",(unsigned)(esp_random() & 0xffff));
    httpd_config_t cfg=HTTPD_DEFAULT_CONFIG();
    cfg.server_port=80;
    cfg.stack_size=6144;
    cfg.max_uri_handlers=6;
    esp_err_t err=httpd_start(&s_server,&cfg);
    if (err != ESP_OK) return err;
    httpd_uri_t root={.uri="/",.method=HTTP_GET,.handler=page_get};
    httpd_uri_t save={.uri="/ha/save",.method=HTTP_POST,.handler=save_post};
    err=httpd_register_uri_handler(s_server,&root);
    if (err == ESP_OK) err=httpd_register_uri_handler(s_server,&save);
    if (err == ESP_OK) {
        httpd_uri_t editor={.uri="/dashboard",.method=HTTP_GET,.handler=editor_get};
        httpd_uri_t read_config={.uri="/dashboard/config",.method=HTTP_GET,.handler=config_get};
        httpd_uri_t write_config={.uri="/dashboard/config",.method=HTTP_POST,.handler=config_post};
        err=httpd_register_uri_handler(s_server,&editor);
        if (err == ESP_OK) err=httpd_register_uri_handler(s_server,&read_config);
        if (err == ESP_OK) err=httpd_register_uri_handler(s_server,&write_config);
    }
    if (err != ESP_OK) { httpd_stop(s_server); s_server=NULL; return err; }
    ESP_LOGI(TAG,"Local HA setup page available on port 80");
    return ESP_OK;
}
