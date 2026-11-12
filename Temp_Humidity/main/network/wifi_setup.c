#include "wifi_setup.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

#include "esp_timer.h"
#include "lwip/sockets.h"
#include "wifi_manager.h"

#define SETUP_HOST "192.168.4.1"
#define MAX_SCAN_RESULTS 16
#define CREDENTIAL_READ_BUDGET_US INT64_C(5000000)

static const char setup_page[] =
    "<!doctype html><html lang='en'><head><meta charset='utf-8'>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>Q Air Monitor Wi-Fi setup</title>"
    "<style>"
    "body{font:1rem system-ui;margin:2rem auto;padding:0 1rem;max-width:30rem}"
    "label{display:block;margin:1rem 0}input,button,select{font:inherit;padding:.5rem}"
    "input,select{box-sizing:border-box;width:100%}#address{overflow-wrap:anywhere}"
    "</style></head><body>"
    "<h1>Q Air Monitor Wi-Fi setup</h1>"
    "<p role='status' id='message'>Loading setup…</p><p id='address'></p>"
    "<form id='setup' autocomplete='off'>"
    "<fieldset id='controls' disabled><legend>Monitor network</legend>"
    "<label>Nearby networks<select id='networks' disabled>"
    "<option value=''>Enter a network below</option></select></label>"
    "<button id='scan' type='button'>Scan for networks</button>"
    "<label>Network name (SSID)<input id='ssid'></label>"
    "<label>Password<input id='password' type='password' autocomplete='new-password'></label>"
    "<p>Leave the password empty for an open network.</p>"
    "<button type='submit'>Test and save Wi-Fi</button></fieldset></form>"
    "<script>"
    "const byId=id=>document.getElementById(id);"
    "const controls=byId('controls'), message=byId('message'), address=byId('address');"
    "const networks=byId('networks'), ssid=byId('ssid'), password=byId('password');"
    "const encoder=new TextEncoder(), decoder=new TextDecoder('utf-8',{fatal:true,ignoreBOM:true});"
    "let working=false,busy=true,saved=false,refreshing=false,feedback='',operation=0,savedAt=0;"
    "async function request(url,options={},json=false){"
    " const controller=new AbortController(),timeout=setTimeout(()=>controller.abort(),8000);"
    " try{"
    "  const response=await fetch(url,{...options,cache:'no-store',signal:controller.signal});"
    "  const body=await (json?response.json():response.text());"
    "  if(!response.ok)throw Error(typeof body==='string'?body:'Setup request failed.');"
    "  return body;"
    " }finally{clearTimeout(timeout);}"
    "}"
    "function update(){controls.disabled=working||busy||saved;}"
    "async function refresh(){"
    " if(working||refreshing)return;"
    " refreshing=true;const version=operation;"
    " try{"
    "  const status=await request('/setup/status',{},true);"
    "  if(version!==operation)return;"
    "  busy=status.busy;saved=status.saved;"
    "  if(saved&&!savedAt)savedAt=Date.now();"
    "  message.textContent=saved?status.message:(feedback||status.message);"
    "  if(status.address){"
    "   const link=document.createElement('a');link.href='http://'+status.address+'/';"
    "   link.textContent=link.href;address.replaceChildren(link);"
    "  }else address.replaceChildren();"
    "  if(saved)password.value='';"
    " }catch{"
    "  if(version===operation&&!address.firstChild){"
    "   message.textContent=saved?'Wi-Fi was saved. Check the serial monitor for the dashboard address.'"
    "    :'Rejoin the setup hotspot to check the connection.';"
    "  }"
    " }finally{refreshing=false;}"
    " update();"
    "}"
    "const fromHex=hex=>Uint8Array.from(hex.match(/../g),byte=>parseInt(byte,16));"
    "networks.addEventListener('change',()=>{"
    " ssid.value=networks.value?networks.options[networks.selectedIndex].textContent:'';"
    "});"
    "ssid.addEventListener('input',()=>{networks.value='';});"
    "byId('scan').addEventListener('click',async()=>{"
    " ++operation;working=true;feedback='';update();message.textContent='Scanning for networks…';"
    " try{"
    "  const response=await request('/setup/networks');"
    "  const selected=networks.value;"
    "  const names=new Set(response.trim().split('\\n').filter(Boolean));"
    "  networks.replaceChildren(new Option('Enter a network below',''));"
    "  for(const hex of names){"
    "   let label;"
    "   try{label=decoder.decode(fromHex(hex));}catch{}if(!label||!label.trim())label='SSID 0x'+hex;"
    "   networks.add(new Option(label,hex));"
    "  }"
    "  if(names.has(selected))networks.value=selected;"
    "  else if(selected)ssid.value='';"
    "  networks.disabled=!names.size;"
    "  if(!names.size)feedback='No networks found. Enter the network name manually.';"
    " }catch{feedback='Could not scan. Try again or enter the network name manually.';}"
    " finally{working=false;await refresh();}"
    "});"
    "byId('setup').addEventListener('submit',async event=>{"
    " event.preventDefault();feedback='';"
    " const name=networks.value?fromHex(networks.value):encoder.encode(ssid.value);"
    " const key=encoder.encode(password.value);"
    " if(name.length<1||name.length>32||key.length>64){"
    "  feedback='Enter an SSID of 1–32 bytes and a password of up to 64 bytes.';"
    "  message.textContent=feedback;return;"
    " }"
    " const payload=new Uint8Array(1+name.length+key.length);"
    " payload[0]=name.length;payload.set(name,1);payload.set(key,1+name.length);"
    " ++operation;working=true;update();message.textContent='Starting connection test…';"
    " try{"
    "  await request('/setup/connect',{method:'POST',body:payload,"
    "   headers:{'Content-Type':'application/octet-stream','X-Q-Air-Setup':'1'}});"
    " }catch(error){feedback=error.name==='AbortError'"
    "  ?'Connection test request timed out. Rejoin the setup hotspot and check the status.'"
    "  :error.message;}"
    " finally{working=false;await refresh();}"
    "});"
    "async function poll(){"
    " await refresh();if(!savedAt||Date.now()-savedAt<150000)setTimeout(poll,1500);"
    "}poll();"
    "</script></body></html>";

bool wifi_setup_request(httpd_req_t *req)
{
    char host[sizeof(SETUP_HOST ":80")];
    if (httpd_req_get_hdr_value_str(req, "Host", host, sizeof(host)) != ESP_OK ||
        (strcmp(host, SETUP_HOST) != 0 && strcmp(host, SETUP_HOST ":80") != 0)) {
        return false;
    }

    struct sockaddr_storage local;
    socklen_t size = sizeof(local);
    if (lwip_getsockname(httpd_req_to_sockfd(req), (struct sockaddr *)&local, &size) != 0) {
        return false;
    }

    uint32_t address;
    if (local.ss_family == AF_INET) {
        address = ((const struct sockaddr_in *)&local)->sin_addr.s_addr;
#if CONFIG_LWIP_IPV6
    } else if (local.ss_family == AF_INET6) {
        const struct in6_addr *ip = &((const struct sockaddr_in6 *)&local)->sin6_addr;
        const uint8_t mapped_prefix[12] = { [10] = 0xff, [11] = 0xff };
        if (memcmp(ip->s6_addr, mapped_prefix, sizeof(mapped_prefix)) != 0) {
            return false;
        }
        memcpy(&address, &ip->s6_addr[sizeof(mapped_prefix)], sizeof(address));
#endif
    } else {
        return false;
    }
    return wifi_manager_is_setup_address(address);
}

static void setup_response(httpd_req_t *req, const char *type)
{
    httpd_resp_set_type(req, type);
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_set_hdr(req, "X-Frame-Options", "DENY");
}

/* A successful handler return tells HTTPD to drain any unread body before
 * reusing the socket. Close rejected requests instead. */
static esp_err_t reject_and_close(httpd_req_t *req, httpd_err_code_t code, const char *message)
{
    httpd_resp_set_hdr(req, "Connection", "close");
    httpd_resp_send_err(req, code, message);
    return ESP_FAIL;
}

esp_err_t wifi_setup_send_page(httpd_req_t *req)
{
    if (req->content_len != 0) {
        return reject_and_close(req, HTTPD_400_BAD_REQUEST, "This setup route does not accept a request body");
    }
    setup_response(req, "text/html; charset=utf-8");
    return httpd_resp_sendstr(req, setup_page);
}

static esp_err_t send_status(httpd_req_t *req)
{
    if (req->content_len != 0) {
        return reject_and_close(req, HTTPD_400_BAD_REQUEST, "This setup route does not accept a request body");
    }
    if (!wifi_setup_request(req)) {
        return httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Not found");
    }

    wifi_setup_status_t status = wifi_manager_setup_status();
    char response[256];
    /* The message is firmware-defined, and the address is a formatted IPv4 address. */
    snprintf(response, sizeof(response),
             "{\"message\":\"%s\",\"address\":\"%s\",\"busy\":%s,\"saved\":%s}",
             status.message, status.address, status.busy ? "true" : "false", status.saved ? "true" : "false");
    setup_response(req, "application/json");
    return httpd_resp_sendstr(req, response);
}

static esp_err_t send_networks(httpd_req_t *req)
{
    if (req->content_len != 0) {
        return reject_and_close(req, HTTPD_400_BAD_REQUEST, "This setup route does not accept a request body");
    }
    if (!wifi_setup_request(req)) {
        return httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Not found");
    }

    wifi_ap_record_t *records = calloc(MAX_SCAN_RESULTS, sizeof(*records));
    if (records == NULL) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Could not allocate scan results");
    }
    uint16_t count = MAX_SCAN_RESULTS;
    esp_err_t result = wifi_manager_scan(records, &count);
    if (result != ESP_OK) {
        free(records);
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Could not scan for networks");
    }

    /* Encode SSIDs as hex so arbitrary bytes cannot alter the response structure. */
    setup_response(req, "text/plain");
    for (uint16_t i = 0; i < count && result == ESP_OK; i++) {
        static const char hex[] = "0123456789abcdef";
        char line[2 * sizeof(records[i].ssid) + 1];
        size_t length = strnlen((const char *)records[i].ssid, sizeof(records[i].ssid));
        if (length == 0) {
            continue;
        }
        for (size_t j = 0; j < length; j++) {
            line[2 * j] = hex[records[i].ssid[j] >> 4];
            line[2 * j + 1] = hex[records[i].ssid[j] & 0x0f];
        }
        line[2 * length] = '\n';
        result = httpd_resp_send_chunk(req, line, 2 * length + 1);
    }
    free(records);
    return result == ESP_OK ? httpd_resp_send_chunk(req, NULL, 0) : result;
}

static esp_err_t receive_credentials(httpd_req_t *req, uint8_t *body)
{
    int fd = httpd_req_to_sockfd(req);
    struct timeval previous_timeout;
    socklen_t option_size = sizeof(previous_timeout);
    if (lwip_getsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &previous_timeout, &option_size) != 0) {
        return reject_and_close(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Could not read socket timeout");
    }

    const int64_t deadline = esp_timer_get_time() + CREDENTIAL_READ_BUDGET_US;
    size_t length = 0;
    while (length < req->content_len) {
        int64_t remaining_us = deadline - esp_timer_get_time();
        if (remaining_us <= 0) {
            return reject_and_close(req, HTTPD_408_REQ_TIMEOUT, "Wi-Fi setup request timed out");
        }
        /* lwIP stores socket timeouts in milliseconds; zero disables it. */
        int64_t remaining_ms = (remaining_us + 999) / 1000;
        struct timeval timeout = {
            .tv_sec = remaining_ms / 1000,
            .tv_usec = (remaining_ms % 1000) * 1000,
        };
        if (lwip_setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) != 0) {
            return reject_and_close(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Could not set socket timeout");
        }
        int received = httpd_req_recv(req, (char *)body + length, req->content_len - length);
        if (received == HTTPD_SOCK_ERR_TIMEOUT || esp_timer_get_time() >= deadline) {
            return reject_and_close(req, HTTPD_408_REQ_TIMEOUT, "Wi-Fi setup request timed out");
        }
        if (received <= 0) {
            return ESP_FAIL;
        }
        length += (size_t)received;
    }

    if (lwip_setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &previous_timeout, option_size) != 0) {
        return reject_and_close(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Could not restore socket timeout");
    }
    return ESP_OK;
}

static esp_err_t connect_network(httpd_req_t *req)
{
    if (!wifi_setup_request(req)) {
        return reject_and_close(req, HTTPD_404_NOT_FOUND, "Not found");
    }

    char header[2];
    if (httpd_req_get_hdr_value_str(req, "X-Q-Air-Setup", header, sizeof(header)) != ESP_OK ||
        strcmp(header, "1") != 0) {
        return reject_and_close(req, HTTPD_400_BAD_REQUEST, "Use the Wi-Fi setup page");
    }

    /* One SSID-length byte, then SSID and password bytes. */
    uint8_t body[1 + 32 + 64];
    size_t length = req->content_len;
    if (length < 2 || length > sizeof(body)) {
        return reject_and_close(req, HTTPD_400_BAD_REQUEST, "Invalid credential lengths");
    }
    if (receive_credentials(req, body) != ESP_OK) {
        return ESP_FAIL;
    }

    size_t ssid_length = body[0];
    if (ssid_length == 0 || ssid_length > 32 || ssid_length > length - 1 ||
        length - 1 - ssid_length > 64 || memchr(body + 1, 0, length - 1) != NULL) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid network name or password");
    }

    wifi_credentials_t credentials = {0};
    memcpy(credentials.ssid, body + 1, ssid_length);
    memcpy(credentials.password, body + 1 + ssid_length, length - 1 - ssid_length);
    esp_err_t result = wifi_manager_submit(&credentials);
    if (result == ESP_ERR_INVALID_ARG) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST,
                                   "Use an empty password, an 8–63 byte passphrase, or a 64-digit hex key.");
    }
    if (result == ESP_ERR_INVALID_STATE) {
        setup_response(req, "text/plain");
        httpd_resp_set_status(req, "503 Service Unavailable");
        return httpd_resp_sendstr(req, "Setup is busy. Try again shortly.");
    }
    if (result != ESP_OK) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Could not start connection test");
    }
    setup_response(req, "text/plain");
    httpd_resp_set_status(req, "202 Accepted");
    return httpd_resp_sendstr(req, "Testing Wi-Fi");
}

esp_err_t wifi_setup_register_routes(httpd_handle_t server)
{
    const httpd_uri_t routes[] = {
        { .uri = "/setup/status", .method = HTTP_GET, .handler = send_status },
        { .uri = "/setup/networks", .method = HTTP_GET, .handler = send_networks },
        { .uri = "/setup/connect", .method = HTTP_POST, .handler = connect_network },
    };
    for (size_t i = 0; i < sizeof(routes) / sizeof(routes[0]); i++) {
        esp_err_t result = httpd_register_uri_handler(server, &routes[i]);
        if (result != ESP_OK) {
            return result;
        }
    }
    return ESP_OK;
}
