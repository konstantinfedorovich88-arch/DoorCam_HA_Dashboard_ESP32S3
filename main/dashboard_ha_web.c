#include "dashboard_ha.h"
#include "esp_http_server.h"
#include "esp_random.h"
#include "esp_log.h"
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
    cfg.max_uri_handlers=4;
    esp_err_t err=httpd_start(&s_server,&cfg);
    if (err != ESP_OK) return err;
    httpd_uri_t root={.uri="/",.method=HTTP_GET,.handler=page_get};
    httpd_uri_t save={.uri="/ha/save",.method=HTTP_POST,.handler=save_post};
    err=httpd_register_uri_handler(s_server,&root);
    if (err == ESP_OK) err=httpd_register_uri_handler(s_server,&save);
    if (err != ESP_OK) { httpd_stop(s_server); s_server=NULL; return err; }
    ESP_LOGI(TAG,"Local HA setup page available on port 80");
    return ESP_OK;
}
