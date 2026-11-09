#include "web_server.h"
#include "wifi_setup.h"

#include <stdbool.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app_config.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "status_led.h"
#include "time_sync.h"

typedef struct {
    bool has_reading;
    bool last_read_succeeded;
    dht11_reading_t reading;
    esp_err_t last_result;
    uint32_t read_count;
    uint32_t failure_count;
} web_reading_state_t;

typedef struct {
    uint32_t seconds_since_boot;
    int16_t temperature_tenths_c;
    uint16_t humidity_tenths_percent;
} history_sample_t;

_Static_assert(sizeof(history_sample_t) == 8, "History samples must remain compact");
_Static_assert(APP_HISTORY_CAPACITY > 0, "History capacity must be positive");
_Static_assert(APP_HISTORY_SAMPLE_INTERVAL_SECONDS > 0, "History interval must be positive");

static const char *TAG = "web";
static httpd_handle_t s_server;
static SemaphoreHandle_t s_state_mutex;
static esp_event_handler_instance_t s_ip_event_instance;
static bool s_ip_event_registered;
static uint32_t s_last_logged_ip;
static web_reading_state_t s_state = {
    .last_result = ESP_ERR_INVALID_STATE,
};

static history_sample_t s_history[APP_HISTORY_CAPACITY];
static size_t s_history_start;
static size_t s_history_count;
static float s_temperature_sum;
static float s_humidity_sum;
static uint32_t s_accumulator_count;
static int64_t s_next_history_sample_seconds;

static const char s_home_page[] =
    "<!DOCTYPE html><html lang=\"en\"><head>"
    "<meta charset=\"utf-8\"><meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
    "<title>Q Air Temp Monitor</title><style>"
    "body{margin:0;font-family:Arial,sans-serif;background:#f5f7fb;color:#18202a}"
    "main{max-width:900px;margin:0 auto;padding:32px 18px}"
    "section{background:#fff;border:1px solid #d9e0ea;border-radius:8px;padding:22px;margin-top:18px}"
    ".title-bar{display:flex;align-items:center;justify-content:space-between;gap:16px;flex-wrap:wrap}.title-bar h1{margin:0}"
    ".title-actions{display:flex;align-items:center;gap:14px}.clock{text-align:right;line-height:1.15;min-width:150px}.clock-time{display:block;font-size:22px;font-variant-numeric:tabular-nums}.clock-date{display:block;margin-top:3px;font-size:12px;color:#65758a}"
    "h1{font-size:28px;margin:0 0 8px}h2{font-size:15px;margin:0 0 10px;color:#4d5d70;text-transform:uppercase}"
    "button{border:1px solid #9baabd;border-radius:999px;background:#fff;color:#26384c;padding:9px 14px;font:600 14px Arial,sans-serif;cursor:pointer;white-space:nowrap}"
    "button[aria-pressed=true]{background:#247447;border-color:#247447;color:#fff}button:disabled{cursor:wait;opacity:.65}"
    ".grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(180px,1fr));gap:14px}"
    ".value{font-size:34px;font-weight:700;margin:0}.label{margin:0 0 6px;color:#65758a}"
    ".status{font-size:15px;color:#39485a}.bad{color:#a53131}"
    ".chart-head{display:flex;flex-wrap:wrap;justify-content:space-between;gap:8px;align-items:center}"
    ".legend{display:flex;gap:16px;font-size:13px;color:#4d5d70}.key:before{content:'';display:inline-block;width:18px;height:3px;margin:0 6px 3px 0;background:var(--colour)}"
    ".chart-wrap{position:relative;width:100%;height:340px;margin-top:12px}canvas{width:100%;height:100%;display:block;cursor:crosshair;touch-action:pan-y}"
    "@media(max-width:520px){main{padding:20px 10px}.chart-wrap{height:270px}.title-bar{align-items:flex-start}.title-bar h1{font-size:24px}.title-actions{width:100%;justify-content:space-between}.clock{text-align:left}}"
    "</style></head><body><main>"
    "<div class=\"title-bar\"><h1>Q Air Temp Monitor</h1><div class=\"title-actions\"><div class=\"clock\" role=\"status\" aria-live=\"polite\"><strong class=\"clock-time\" id=\"clock-time\">--:--:--</strong><span class=\"clock-date\" id=\"clock-date\">Synchronizing time...</span></div><button id=\"blink-toggle\" type=\"button\" aria-pressed=\"true\">Blink: On</button></div></div>"
    "<p class=\"status\">Live readings every 2.5 seconds; trend points are one-minute averages.</p>"
    "<section><h2>Current Reading</h2><div class=\"grid\">"
    "<div><p class=\"label\">Temperature</p><p class=\"value\" id=\"temp-c\">--.- &deg;C</p></div>"
    "<div><p class=\"label\">Temperature</p><p class=\"value\" id=\"temp-f\">--.- &deg;F</p></div>"
    "<div><p class=\"label\">Humidity</p><p class=\"value\" id=\"humidity\">--.- %</p></div>"
    "</div><p class=\"status\" id=\"reading-note\">Waiting for a valid DHT11 reading.</p></section>"
    "<section><div class=\"chart-head\"><div><h2>24-hour trend</h2><span class=\"status\" id=\"history-note\">Loading history...</span></div>"
    "<div class=\"legend\"><span class=\"key\" style=\"--colour:#d34a4a\">Temperature</span><span class=\"key\" style=\"--colour:#2878c8\">Humidity</span></div></div>"
    "<div class=\"chart-wrap\"><canvas id=\"trend\" aria-label=\"Interactive temperature and humidity trend graph\"></canvas></div></section>"
    "<section><h2>Status</h2><p class=\"status\" id=\"status\">Connecting to monitor...</p></section>"
    "</main><script>"
    "const canvas=document.getElementById('trend'),ctx=canvas.getContext('2d');let samples=[],clockSynced=false,graphNow=0,historyTimer,clockReferenceUnix=0,clockReferenceMs=0,chartState=null,hoverIndex=-1,historyInterval=60;"
    "function esc(v){return String(v).replace(/[&<>]/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;'}[c]));}"
    "function showBlink(enabled){const b=document.getElementById('blink-toggle');b.setAttribute('aria-pressed',enabled?'true':'false');b.textContent='Blink: '+(enabled?'On':'Off');}"
    "function updateClock(){const time=document.getElementById('clock-time'),date=document.getElementById('clock-date');if(!clockSynced){time.textContent='--:--:--';date.textContent='Synchronizing time...';return;}const now=new Date((clockReferenceUnix+(performance.now()-clockReferenceMs)/1000)*1000);time.textContent=now.toLocaleTimeString([],{hour:'2-digit',minute:'2-digit',second:'2-digit'});date.textContent=now.toLocaleDateString([],{weekday:'short',day:'2-digit',month:'short',year:'numeric'});}"
    "async function requestJson(url,options={}){const controller=new AbortController(),timeout=setTimeout(()=>controller.abort(),8000);try{const r=await fetch(url,{...options,cache:'no-store',signal:controller.signal});if(!r.ok)throw Error(r.status);return await r.json();}finally{clearTimeout(timeout);}}"
    "let blinkRevision=0;"
    "async function loadReading(){const revision=blinkRevision;try{const d=await requestJson('/api/reading');"
    "if(revision===blinkRevision&&!document.getElementById('blink-toggle').disabled)showBlink(d.blink_enabled);"
    "if(d.has_reading){document.getElementById('temp-c').innerHTML=d.temperature_c.toFixed(1)+' &deg;C';document.getElementById('temp-f').innerHTML=d.temperature_f.toFixed(1)+' &deg;F';document.getElementById('humidity').textContent=d.humidity_percent.toFixed(1)+' %';}else{document.getElementById('temp-c').innerHTML='--.- &deg;C';document.getElementById('temp-f').innerHTML='--.- &deg;F';document.getElementById('humidity').textContent='--.- %';}"
    "const note=document.getElementById('reading-note');note.className='status'+(d.valid?'':' bad');note.textContent=d.valid?'Latest sensor read succeeded.':(d.has_reading?'Showing the last good value; the latest read failed.':'Waiting for a valid DHT11 reading.');"
    "document.getElementById('status').innerHTML='Sensor reads: '+d.read_count+'<br>Failed reads: '+d.failure_count+'<br>Last result: '+esc(d.last_result);"
    "}catch(e){const n=document.getElementById('reading-note');n.className='status bad';n.textContent='Unable to update live reading.';}finally{setTimeout(loadReading,5000);}}"
    "async function toggleBlink(){const b=document.getElementById('blink-toggle');b.disabled=true;try{const d=await requestJson('/api/blink/toggle',{method:'POST'});blinkRevision++;showBlink(d.blink_enabled);}catch(e){document.getElementById('status').textContent='Unable to change blink setting.';}finally{b.disabled=false;}}"
    "async function loadHistory(){let retry=60000;try{const d=await requestJson('/api/history');samples=d.samples||[];historyInterval=d.interval_seconds;hoverIndex=-1;clockSynced=d.time_synced;graphNow=clockSynced?d.current_unix_time:d.current_uptime_seconds;"
    "if(clockSynced){clockReferenceUnix=d.current_unix_time;clockReferenceMs=performance.now();const offset=d.current_unix_time-d.current_uptime_seconds;samples.forEach(s=>s.unix_time=offset+s.seconds_since_boot);}else retry=5000;updateClock();"
    "const count=samples.length? samples.length+' of '+d.capacity+' one-minute samples':'Collecting the first one-minute sample...';document.getElementById('history-note').textContent=count+(clockSynced?' - clock synchronized':' - waiting for time sync');draw();"
    "}catch(e){retry=5000;document.getElementById('history-note').textContent='Unable to load history.';}finally{clearTimeout(historyTimer);historyTimer=setTimeout(loadHistory,retry);}}"
    "function range(values,pad,minSpan){let lo=Math.min(...values),hi=Math.max(...values);if(hi-lo<minSpan){const m=(hi+lo)/2;lo=m-minSpan/2;hi=m+minSpan/2;}const p=(hi-lo)*pad;return[lo-p,hi+p];}"
    "function nearestIndex(value,key){let lo=0,hi=samples.length;while(lo<hi){const mid=(lo+hi)>>1;if(samples[mid][key]<value)lo=mid+1;else hi=mid;}if(lo===0)return 0;if(lo===samples.length)return lo-1;return value-samples[lo-1][key]<=samples[lo][key]-value?lo-1:lo;}"
    "function selectPoint(e){if(!chartState||!samples.length)return;const rect=canvas.getBoundingClientRect(),x=e.clientX-rect.left,y=e.clientY-rect.top,c=chartState;if(x<c.l||x>c.l+c.pw||y<c.t||y>c.t+c.ph){if(hoverIndex!==-1){hoverIndex=-1;draw();}return;}const next=nearestIndex(c.first+(x-c.l)/c.pw*c.span,c.key);if(next!==hoverIndex){hoverIndex=next;draw();}}"
    "function clearPoint(e){if((!e||e.type==='pointercancel'||e.pointerType==='mouse')&&hoverIndex!==-1){hoverIndex=-1;draw();}}"
    "function drawTooltip(){if(!chartState||hoverIndex<0||!samples[hoverIndex])return;const c=chartState,s=samples[hoverIndex],x=c.l+(s[c.key]-c.first)/c.span*c.pw,ty=c.t+(c.tr[1]-s.temperature_c)/(c.tr[1]-c.tr[0])*c.ph,hy=c.t+(c.hr[1]-s.humidity_percent)/(c.hr[1]-c.hr[0])*c.ph;ctx.save();ctx.strokeStyle='#6d7988';ctx.lineWidth=1;ctx.setLineDash([4,4]);ctx.beginPath();ctx.moveTo(x,c.t);ctx.lineTo(x,c.t+c.ph);ctx.stroke();ctx.setLineDash([]);function marker(y,colour){ctx.beginPath();ctx.arc(x,y,5,0,Math.PI*2);ctx.fillStyle='#fff';ctx.fill();ctx.strokeStyle=colour;ctx.lineWidth=3;ctx.stroke();}marker(ty,'#d34a4a');marker(hy,'#2878c8');const label=clockSynced?new Date(s.unix_time*1000).toLocaleString([],{day:'2-digit',month:'short',hour:'2-digit',minute:'2-digit'}):Math.max(0,Math.round((graphNow-s.seconds_since_boot)/60))+' min ago',bw=174,bh=72,bx=Math.max(4,Math.min(x+12,c.w-bw-4)),by=c.t+8;ctx.fillStyle='rgba(255,255,255,.96)';ctx.strokeStyle='#9baabd';ctx.lineWidth=1;ctx.fillRect(bx,by,bw,bh);ctx.strokeRect(bx,by,bw,bh);ctx.textAlign='left';ctx.textBaseline='middle';ctx.font='600 12px Arial';ctx.fillStyle='#26384c';ctx.fillText(label,bx+10,by+15);ctx.font='12px Arial';ctx.fillStyle='#b43d3d';ctx.fillText('Temperature: '+s.temperature_c.toFixed(1)+' C',bx+10,by+36);ctx.fillStyle='#236aaa';ctx.fillText('Humidity: '+s.humidity_percent.toFixed(1)+' %',bx+10,by+56);ctx.restore();}"
    "function draw(){const box=canvas.getBoundingClientRect(),dpr=window.devicePixelRatio||1,w=Math.max(1,box.width),h=Math.max(1,box.height);canvas.width=Math.round(w*dpr);canvas.height=Math.round(h*dpr);ctx.setTransform(dpr,0,0,dpr,0,0);ctx.clearRect(0,0,w,h);"
    "const l=48,r=48,t=18,b=34,pw=w-l-r,ph=h-t-b;ctx.font='12px Arial';ctx.lineWidth=1;ctx.strokeStyle='#dfe5ed';ctx.fillStyle='#65758a';ctx.textBaseline='middle';"
    "for(let i=0;i<=4;i++){const y=t+ph*i/4;ctx.beginPath();ctx.moveTo(l,y);ctx.lineTo(w-r,y);ctx.stroke();}"
    "if(!samples.length){chartState=null;ctx.textAlign='center';ctx.fillText('Trend data will appear after one minute',w/2,h/2);return;}"
    "const tr=range(samples.map(s=>s.temperature_c),.1,2),hr=range(samples.map(s=>s.humidity_percent),.1,5),key=clockSynced?'unix_time':'seconds_since_boot',last=Math.max(graphNow,samples[samples.length-1][key]),first=samples.length>1?samples[0][key]:Math.min(samples[0][key],last-historyInterval),span=Math.max(1,last-first);"
    "chartState={l,t,pw,ph,w,h,tr,hr,key,first,last,span};"
    "ctx.textAlign='right';ctx.fillStyle='#b43d3d';ctx.fillText(tr[1].toFixed(1)+' C',l-7,t);ctx.fillText(tr[0].toFixed(1)+' C',l-7,t+ph);ctx.textAlign='left';ctx.fillStyle='#236aaa';ctx.fillText(hr[1].toFixed(1)+' %',w-r+7,t);ctx.fillText(hr[0].toFixed(1)+' %',w-r+7,t+ph);"
    "function line(valueKey,rg,colour){ctx.beginPath();samples.forEach((s,i)=>{const x=l+(s[key]-first)/span*pw,y=t+(rg[1]-s[valueKey])/(rg[1]-rg[0])*ph;if(i&&s.seconds_since_boot-samples[i-1].seconds_since_boot<=historyInterval*1.5)ctx.lineTo(x,y);else ctx.moveTo(x,y);});ctx.strokeStyle=colour;ctx.lineWidth=2;ctx.stroke();samples.forEach((s,i)=>{const joinsPrevious=i>0&&s.seconds_since_boot-samples[i-1].seconds_since_boot<=historyInterval*1.5,joinsNext=i+1<samples.length&&samples[i+1].seconds_since_boot-s.seconds_since_boot<=historyInterval*1.5;if(!joinsPrevious&&!joinsNext){ctx.beginPath();ctx.arc(l+(s[key]-first)/span*pw,t+(rg[1]-s[valueKey])/(rg[1]-rg[0])*ph,3,0,Math.PI*2);ctx.fillStyle=colour;ctx.fill();}});}"
    "line('temperature_c',tr,'#d34a4a');line('humidity_percent',hr,'#2878c8');const ticks=w<500?2:4;ctx.fillStyle='#65758a';ctx.textAlign='center';for(let i=0;i<=ticks;i++){const ratio=i/ticks,x=l+ratio*pw,value=first+ratio*span,label=clockSynced?new Date(value*1000).toLocaleTimeString([],{hour:'2-digit',minute:'2-digit'}):(i===ticks?'now':'-'+Math.round((last-value)/3600)+'h');ctx.fillText(label,x,h-12);}drawTooltip();}" 
    "canvas.addEventListener('pointermove',selectPoint);canvas.addEventListener('pointerdown',selectPoint);canvas.addEventListener('pointerleave',clearPoint);canvas.addEventListener('pointercancel',clearPoint);document.getElementById('blink-toggle').addEventListener('click',toggleBlink);window.addEventListener('resize',draw);updateClock();loadReading();loadHistory();setInterval(updateClock,1000);"
    "</script></body></html>";

static void log_server_address(const esp_ip4_addr_t *ip)
{
    if (ip == NULL || ip->addr == 0 || ip->addr == s_last_logged_ip) {
        return;
    }

    s_last_logged_ip = ip->addr;
    ESP_LOGI(TAG, "Web server available at http://" IPSTR, IP2STR(ip));
}

static void web_ip_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    (void)arg;
    (void)event_base;
    (void)event_id;

    if (s_server != NULL && event_data != NULL) {
        const ip_event_got_ip_t *event = (const ip_event_got_ip_t *)event_data;
        log_server_address(&event->ip_info.ip);
    }
}

static void log_current_server_address(void)
{
    esp_netif_t *station = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    esp_netif_ip_info_t ip_info = {0};

    if (station != NULL && esp_netif_get_ip_info(station, &ip_info) == ESP_OK && ip_info.ip.addr != 0) {
        log_server_address(&ip_info.ip);
    } else {
        ESP_LOGI(TAG, "HTTP server started; waiting for a local IP address");
    }
}

static bool get_state_snapshot(web_reading_state_t *snapshot)
{
    if (snapshot == NULL || s_state_mutex == NULL ||
        xSemaphoreTake(s_state_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return false;
    }

    *snapshot = s_state;
    xSemaphoreGive(s_state_mutex);
    return true;
}

static int16_t temperature_to_tenths(float temperature_c)
{
    float scaled = temperature_c * 10.0f;
    return (int16_t)(scaled + (scaled >= 0.0f ? 0.5f : -0.5f));
}

static uint16_t humidity_to_tenths(float humidity_percent)
{
    return (uint16_t)(humidity_percent * 10.0f + 0.5f);
}

static void append_history_sample(int64_t sample_seconds, float temperature_c, float humidity_percent)
{
    size_t index;

    if (s_history_count < APP_HISTORY_CAPACITY) {
        index = (s_history_start + s_history_count) % APP_HISTORY_CAPACITY;
        s_history_count++;
    } else {
        index = s_history_start;
        s_history_start = (s_history_start + 1) % APP_HISTORY_CAPACITY;
    }

    s_history[index] = (history_sample_t) {
        .seconds_since_boot = (uint32_t)sample_seconds,
        .temperature_tenths_c = temperature_to_tenths(temperature_c),
        .humidity_tenths_percent = humidity_to_tenths(humidity_percent),
    };
}

static void update_history(const dht11_reading_t *reading)
{
    int64_t now_seconds = esp_timer_get_time() / 1000000;

    if (s_next_history_sample_seconds == 0) {
        s_next_history_sample_seconds = now_seconds + APP_HISTORY_SAMPLE_INTERVAL_SECONDS;
    }

    if (now_seconds >= s_next_history_sample_seconds) {
        if (s_accumulator_count > 0) {
            append_history_sample(s_next_history_sample_seconds,
                                  s_temperature_sum / (float)s_accumulator_count,
                                  s_humidity_sum / (float)s_accumulator_count);
        }

        s_temperature_sum = 0.0f;
        s_humidity_sum = 0.0f;
        s_accumulator_count = 0;

        do {
            s_next_history_sample_seconds += APP_HISTORY_SAMPLE_INTERVAL_SECONDS;
        } while (now_seconds >= s_next_history_sample_seconds);
    }

    if (reading != NULL) {
        s_temperature_sum += reading->temperature_c;
        s_humidity_sum += reading->humidity_percent;
        s_accumulator_count++;
    }
}

static esp_err_t send_home_page(httpd_req_t *req)
{
    if (wifi_setup_request(req)) {
        return wifi_setup_send_page(req);
    }

    httpd_resp_set_type(req, "text/html");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, s_home_page, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t send_reading_json(httpd_req_t *req)
{
    char buffer[384];
    int length;
    web_reading_state_t state;

    if (!get_state_snapshot(&state)) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Reading state unavailable");
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");

    if (state.has_reading) {
        length = snprintf(buffer,
                 sizeof(buffer),
                 "{\"valid\":%s,\"has_reading\":true,\"stale\":%s,\"temperature_c\":%.1f,"
                 "\"temperature_f\":%.1f,\"humidity_percent\":%.1f,"
                 "\"read_count\":%lu,\"failure_count\":%lu,\"last_result\":\"%s\",\"blink_enabled\":%s}",
                 state.last_read_succeeded ? "true" : "false",
                 state.last_read_succeeded ? "false" : "true",
                 state.reading.temperature_c,
                 state.reading.temperature_f,
                 state.reading.humidity_percent,
                 (unsigned long)state.read_count,
                 (unsigned long)state.failure_count,
                 esp_err_to_name(state.last_result),
                 status_led_is_blink_enabled() ? "true" : "false");
    } else {
        length = snprintf(buffer,
                 sizeof(buffer),
                 "{\"valid\":false,\"has_reading\":false,\"stale\":false,\"read_count\":%lu,"
                 "\"failure_count\":%lu,\"last_result\":\"%s\",\"blink_enabled\":%s}",
                 (unsigned long)state.read_count,
                 (unsigned long)state.failure_count,
                 esp_err_to_name(state.last_result),
                 status_led_is_blink_enabled() ? "true" : "false");
    }

    if (length < 0 || (size_t)length >= sizeof(buffer)) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Reading response too large");
    }
    return httpd_resp_send(req, buffer, length);
}

static esp_err_t toggle_blink(httpd_req_t *req)
{
    bool enabled = status_led_toggle_blink_enabled();
    char response[32];

    snprintf(response,
             sizeof(response),
             "{\"blink_enabled\":%s}",
             enabled ? "true" : "false");
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_sendstr(req, response);
}

static esp_err_t send_history_json(httpd_req_t *req)
{
    char buffer[768];
    history_sample_t *snapshot = NULL;
    size_t count;

    if (s_state_mutex == NULL || xSemaphoreTake(s_state_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "History unavailable");
    }

    count = s_history_count;
    if (count > 0) {
        snapshot = malloc(count * sizeof(*snapshot));
        if (snapshot != NULL) {
            size_t first_count = APP_HISTORY_CAPACITY - s_history_start;
            if (first_count > count) {
                first_count = count;
            }
            memcpy(snapshot, &s_history[s_history_start], first_count * sizeof(*snapshot));
            memcpy(snapshot + first_count, s_history, (count - first_count) * sizeof(*snapshot));
        }
    }
    xSemaphoreGive(s_state_mutex);

    if (count > 0 && snapshot == NULL) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Not enough memory for history");
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");

    uint32_t current_uptime_seconds = (uint32_t)(esp_timer_get_time() / 1000000);
    time_t current_epoch = 0;
    bool time_synced = time_sync_get_epoch(&current_epoch);
    int length = snprintf(buffer,
             sizeof(buffer),
             "{\"capacity\":%u,\"interval_seconds\":%u,\"count\":%u,\"time_synced\":%s,"
             "\"current_unix_time\":%lld,\"current_uptime_seconds\":%lu,\"samples\":[",
             APP_HISTORY_CAPACITY,
             APP_HISTORY_SAMPLE_INTERVAL_SECONDS,
             (unsigned)count,
             time_synced ? "true" : "false",
             (long long)(time_synced ? current_epoch : 0),
             (unsigned long)current_uptime_seconds);

    if (length < 0 || (size_t)length >= sizeof(buffer)) {
        free(snapshot);
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "History response too large");
    }

    size_t used = (size_t)length;
    esp_err_t result = ESP_OK;
    for (size_t i = 0; result == ESP_OK && i < count; i++) {
        /* Reserve enough room for the largest sample, batching small JSON records
         * rather than making a separate socket send for every minute. */
        if (sizeof(buffer) - used < 128) {
            result = httpd_resp_send_chunk(req, buffer, used);
            used = 0;
            if (result != ESP_OK) {
                break;
            }
        }
        length = snprintf(buffer + used,
                 sizeof(buffer) - used,
                 "%s{\"seconds_since_boot\":%lu,\"temperature_c\":%.1f,\"humidity_percent\":%.1f}",
                 i == 0 ? "" : ",",
                 (unsigned long)snapshot[i].seconds_since_boot,
                 (double)snapshot[i].temperature_tenths_c / 10.0,
                 (double)snapshot[i].humidity_tenths_percent / 10.0);
        if (length < 0 || (size_t)length >= sizeof(buffer) - used) {
            result = ESP_FAIL;
            break;
        }
        used += (size_t)length;
    }

    free(snapshot);
    if (result == ESP_OK) {
        result = httpd_resp_send_chunk(req, buffer, used);
    }
    if (result == ESP_OK) {
        result = httpd_resp_sendstr_chunk(req, "]}");
    }
    if (result == ESP_OK) {
        result = httpd_resp_sendstr_chunk(req, NULL);
    }
    return result;
}

esp_err_t web_server_start(void)
{
    if (s_server != NULL) {
        return ESP_OK;
    }

    if (s_state_mutex == NULL) {
        s_state_mutex = xSemaphoreCreateMutex();
        if (s_state_mutex == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.lru_purge_enable = true;

    esp_err_t result = httpd_start(&s_server, &config);
    if (result != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start HTTP server: %s", esp_err_to_name(result));
        return result;
    }

    const httpd_uri_t home = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = send_home_page,
        .user_ctx = NULL,
    };
    const httpd_uri_t reading_api = {
        .uri = "/api/reading",
        .method = HTTP_GET,
        .handler = send_reading_json,
        .user_ctx = NULL,
    };
    const httpd_uri_t history_api = {
        .uri = "/api/history",
        .method = HTTP_GET,
        .handler = send_history_json,
        .user_ctx = NULL,
    };
    const httpd_uri_t blink_toggle_api = {
        .uri = "/api/blink/toggle",
        .method = HTTP_POST,
        .handler = toggle_blink,
        .user_ctx = NULL,
    };

    result = httpd_register_uri_handler(s_server, &home);
    if (result == ESP_OK) {
        result = httpd_register_uri_handler(s_server, &reading_api);
    }
    if (result == ESP_OK) {
        result = httpd_register_uri_handler(s_server, &history_api);
    }
    if (result == ESP_OK) {
        result = httpd_register_uri_handler(s_server, &blink_toggle_api);
    }
    if (result == ESP_OK) {
        result = wifi_setup_register_routes(s_server);
    }
    if (result != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register HTTP route: %s", esp_err_to_name(result));
        httpd_stop(s_server);
        s_server = NULL;
        return result;
    }

    if (!s_ip_event_registered) {
        result = esp_event_handler_instance_register(IP_EVENT,
                                                     IP_EVENT_STA_GOT_IP,
                                                     &web_ip_event_handler,
                                                     NULL,
                                                     &s_ip_event_instance);
        if (result != ESP_OK) {
            ESP_LOGE(TAG, "Failed to register web-server IP handler: %s", esp_err_to_name(result));
            httpd_stop(s_server);
            s_server = NULL;
            return result;
        }
        s_ip_event_registered = true;
    }

    log_current_server_address();
    ESP_LOGI(TAG,
             "History buffer ready: %u one-minute samples (%u bytes)",
             APP_HISTORY_CAPACITY,
             (unsigned)sizeof(s_history));

    return ESP_OK;
}

void web_server_update_reading(const dht11_reading_t *reading, esp_err_t result)
{
    if (s_state_mutex == NULL || xSemaphoreTake(s_state_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return;
    }

    if (result == ESP_OK &&
        (reading == NULL || !isfinite(reading->temperature_c) ||
         !isfinite(reading->temperature_f) || !isfinite(reading->humidity_percent) ||
         reading->temperature_c < -3276.0f || reading->temperature_c > 3276.0f ||
         reading->humidity_percent < 0.0f || reading->humidity_percent > 100.0f)) {
        result = ESP_ERR_INVALID_ARG;
    }

    s_state.read_count++;
    s_state.last_result = result;
    s_state.last_read_succeeded = result == ESP_OK;

    if (result == ESP_OK && reading != NULL) {
        s_state.reading = *reading;
        s_state.has_reading = true;
    } else {
        s_state.failure_count++;
    }

    update_history(result == ESP_OK ? reading : NULL);

    xSemaphoreGive(s_state_mutex);
}
