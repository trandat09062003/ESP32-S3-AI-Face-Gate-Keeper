/* 
 * PROJECT: ESP32-S3 AI Face Gate Keeper V15 PRO MAX
 * FEATURES: YUV422 Hybrid Mode (Color Stream + Grayscale AI), High Speed, Stable Jitter
 */

#include "Arduino.h"
#include "esp_camera.h"
#include <WiFi.h>
#include <Wire.h>
#include "esp_http_server.h"
#include "esp_partition.h"
#include <Preferences.h>

// ===========================
// HARDWARE PINS (S3-N16R8)
// ===========================
#define PWDN_GPIO_NUM  -1
#define RESET_GPIO_NUM -1
#define XCLK_GPIO_NUM  15
#define SIOD_GPIO_NUM  4
#define SIOC_GPIO_NUM  5
#define Y9_GPIO_NUM    16
#define Y8_GPIO_NUM    17
#define Y7_GPIO_NUM    18
#define Y6_GPIO_NUM    12
#define Y5_GPIO_NUM    10
#define Y4_GPIO_NUM    8
#define Y3_GPIO_NUM    9
#define Y2_GPIO_NUM    11
#define VSYNC_GPIO_NUM 6
#define HREF_GPIO_NUM  7
#define PCLK_GPIO_NUM  13

#define BUILTIN_LED 48 
#define RELAY_PIN   21 

const char *ssid = "Kata";
const char *password = "Katana3936@";

#define FACE_SIZE 576 
#define MAX_FACES 10

struct FaceRecord {
    char name[32];
    uint8_t template_data[FACE_SIZE];
    bool active;
};

FaceRecord db[MAX_FACES];
Preferences prefs;

volatile bool is_enrolling = false;
String enroll_name = "";
String latest_log = "";
int box_x, box_y, box_w, box_h;
bool face_detected = false;
bool door_unlocked = false;
unsigned long door_timer = 0;

String display_name = "Unknown";
unsigned long name_latch_timer = 0;
uint8_t prev_face[FACE_SIZE];
bool is_live = false;
float jitter_avg = 0; 

void save_db() {
    prefs.begin("fdb", false);
    prefs.putBytes("data", db, sizeof(db));
    prefs.end();
}

void load_db() {
    prefs.begin("fdb", true);
    if(prefs.getBytes("data", db, sizeof(db)) == 0) {
        for(int i=0; i<MAX_FACES; i++) db[i].active = false;
    }
    prefs.end();
}

// ===========================
// AI ENGINE V15 (YUV HYBRID)
// ===========================

// In YUV422, the buffer is [Y0, U0, Y1, V0, Y2, U1, Y3, V1...]
// Y is the luminance (Grayscale). It's the 1st and 3rd byte.
inline uint8_t get_y_channel(uint8_t *buf, int x, int y, int width) {
    return buf[(y * width + x) * 2];
}

void detect_face_v15(camera_fb_t *fb) {
    if(!fb) return;
    int step = 15;
    int best_v = 0, bx = 0, by = 0;
    for(int y=20; y<fb->height-70; y+=step) {
        for(int x=20; x<fb->width-70; x+=step) {
            long sum = 0, sq_sum = 0;
            for(int iy=0; iy<30; iy+=6) {
                for(int ix=0; ix<30; ix+=6) {
                    uint8_t v = get_y_channel(fb->buf, x+ix, y+iy, fb->width);
                    sum += v; sq_sum += (long)v*v;
                }
            }
            int var = (sq_sum/25) - (sum/25)*(sum/25);
            if(var > best_v) { best_v = var; bx = x; by = y; }
        }
    }
    if(best_v > 250) { // Slightly more sensitive
        face_detected = true; box_x = bx; box_y = by; box_w = 60; box_h = 60;
    } else { face_detected = false; }
}

float compare_v15(uint8_t* f1, uint8_t* f2) {
    long err = 0;
    for(int i=0; i<FACE_SIZE; i++) { int d = (int)f1[i] - (int)f2[i]; err += (long)d*d; }
    return (float)err / FACE_SIZE;
}

// ===========================
// WEB UI V15 (COLOR)
// ===========================

const char* INDEX_HTML = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <title>AI Face Pro Max V15</title>
    <meta charset="utf-8">
    <style>
        body { font-family: sans-serif; background: #0a0f1e; color: #fff; margin: 0; padding: 15px; text-align: center; }
        .card { background: #161c2d; border-radius: 20px; padding: 20px; display: inline-block; width: 100%; max-width: 420px; border: 1px solid #1e293b; }
        .stream-box { position: relative; width: 320px; height: 240px; margin: auto; border: 3px solid #00d2ff; border-radius: 12px; overflow: hidden; background: #000; box-shadow: 0 0 20px rgba(0,210,255,0.3); }
        #canvas { position: absolute; top: 0; left: 0; width: 100%; height: 100%; }
        .btn { padding: 15px; border-radius: 10px; border: none; cursor: pointer; width: 100%; margin-top: 15px; background: #00d2ff; color: #fff; font-weight: bold; }
        .log-box { height: 130px; overflow-y: auto; background: #000; padding: 10px; margin-top: 15px; text-align: left; border-radius: 8px; font-size: 12px; border: 1px solid #1e293b; }
    </style>
</head>
<body>
    <div class="card">
        <h2 style="color:#00d2ff; margin:0 0 10px 0;">AI PRO MAX V15</h2>
        <div id="status" style="font-weight:bold; color:#ef4444; margin-bottom:10px;">LOCKED</div>
        <div class="stream-box">
            <img src="/stream" width="320" height="240">
            <canvas id="canvas"></canvas>
            <div id="jitter" style="position:absolute; bottom:5px; right:5px; font-size:12px; color:#00ff00;">LIVE: 0.0</div>
        </div>
        <button class="btn" onclick="enroll()">+ ENROLL USER</button>
        <div id="logs" class="log-box"></div>
        <div id="users" style="margin-top:20px; text-align:left; background:#0d1117; padding:12px; border-radius:10px;">Syncing users...</div>
    </div>
    <script>
        const canvas = document.getElementById('canvas');
        const ctx = canvas.getContext('2d');
        function enroll() { let n = prompt("Enter Name:"); if(n) fetch('/enroll?name=' + encodeURIComponent(n)); }
        function updateUsers() {
            fetch('/list').then(r => r.json()).then(data => {
                document.getElementById('users').innerHTML = data.map(i => `<div style="display:flex; justify-content:space-between; margin-bottom:8px;"><span>${i.name}</span><button style="background:#ef4444; color:white; border:none; padding:2px 8px; border-radius:4px; cursor:pointer;" onclick="fetch('/delete?id=${i.id}').then(updateUsers)">Del</button></div>`).join('') || "No users found.";
            });
        }
        setInterval(updateUsers, 3000);
        setInterval(() => {
            fetch('/info').then(r => r.json()).then(d => {
                document.getElementById('status').innerText = d.open ? "ACCESS GRANTED" : "LOCKED";
                document.getElementById('status').style.color = d.open ? "#22c55e" : "#ef4444";
                document.getElementById('jitter').innerText = "LIVE: " + d.jitter.toFixed(1);
                canvas.width = 320; canvas.height = 240;
                ctx.clearRect(0,0,320,240);
                if(d.detected) {
                    ctx.strokeStyle = d.live ? "#00ff00" : "#ff4444";
                    ctx.lineWidth = 4; ctx.strokeRect(d.x, d.y, d.w, d.h);
                    ctx.fillStyle = ctx.strokeStyle; ctx.font = "bold 14px sans-serif";
                    ctx.fillText(d.live ? d.name : "PHOTO!", d.x, d.y - 5);
                }
                if(d.log) {
                    const l = document.getElementById('logs');
                    l.innerHTML = `<div>[${new Date().toLocaleTimeString()}] ${d.log}</div>` + l.innerHTML;
                }
            });
        }, 700);
        updateUsers();
    </script>
</body>
</html>
)rawliteral";

// ===========================
// HANDLERS
// ===========================

static esp_err_t list_handler(httpd_req_t *req) {
    httpd_resp_set_type(req, "application/json");
    String json = "["; bool first = true;
    for(int i=0; i<MAX_FACES; i++) {
        if(db[i].active) {
            if(!first) json += ",";
            json += "{\"id\":" + String(i) + ",\"name\":\"" + String(db[i].name) + "\"}";
            first = false;
        }
    }
    json += "]"; return httpd_resp_send(req, json.c_str(), json.length());
}

static esp_err_t delete_handler(httpd_req_t *req) {
    char buf[64]; if (httpd_req_get_url_query_str(req, buf, sizeof(buf)) == ESP_OK) {
        char p[8]; if (httpd_query_key_value(buf, "id", p, sizeof(p)) == ESP_OK) {
            int id = atoi(p); if(id >= 0 && id < MAX_FACES) { db[id].active = false; save_db(); }
        }
    }
    return httpd_resp_send(req, "OK", 2);
}

static esp_err_t info_handler(httpd_req_t *req) {
    httpd_resp_set_type(req, "application/json");
    char j[512]; if (millis() > name_latch_timer) display_name = "Unknown";
    snprintf(j, sizeof(j), "{\"open\":%s, \"detected\":%s, \"x\":%d, \"y\":%d, \"w\":%d, \"h\":%d, \"name\":\"%s\", \"log\":\"%s\", \"live\":%s, \"jitter\":%.1f}", 
             door_unlocked ? "true" : "false", face_detected ? "true" : "false", box_x, box_y, box_w, box_h, display_name.c_str(), latest_log.c_str(), is_live ? "true" : "false", jitter_avg);
    latest_log = ""; return httpd_resp_send(req, j, strlen(j));
}

static esp_err_t enroll_handler(httpd_req_t *req) {
    char buf[64]; if (httpd_req_get_url_query_str(req, buf, sizeof(buf)) == ESP_OK) {
        char p[32]; if (httpd_query_key_value(buf, "name", p, sizeof(p)) == ESP_OK) {
            enroll_name = String(p); is_enrolling = true;
        }
    }
    return httpd_resp_send(req, "OK", 2);
}

static esp_err_t stream_handler(httpd_req_t *req) {
    camera_fb_t * fb = NULL; esp_err_t res = ESP_OK; char part_buf[128];
    httpd_resp_set_type(req, "multipart/x-mixed-replace;boundary=frame");
    while(true) {
        fb = esp_camera_fb_get();
        if (!fb) { vTaskDelay(10); continue; }
        uint8_t * j_buf = NULL; size_t j_len = 0;
        // Convert YUV422 to JPEG for color stream
        if (frame2jpg(fb, 12, &j_buf, &j_len)) {
            size_t hlen = snprintf(part_buf, 128, "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n", j_len);
            res = httpd_resp_send_chunk(req, part_buf, hlen);
            if (res == ESP_OK) res = httpd_resp_send_chunk(req, (const char *)j_buf, j_len);
            if (res == ESP_OK) res = httpd_resp_send_chunk(req, "\r\n--frame\r\n", 11);
            free(j_buf);
        }
        esp_camera_fb_return(fb);
        if(res != ESP_OK) break;
        vTaskDelay(1);
    }
    return res;
}

void RecognitionTask(void * pvParameters) {
    while(1) {
        // Force CAMERA_GRAB_LATEST to avoid Jitter=0.0
        camera_fb_t * fb = esp_camera_fb_get();
        if (fb) {
            detect_face_v15(fb);
            if (face_detected) {
                uint8_t current_face[FACE_SIZE];
                float diff = 0;
                for (int i = 0; i < FACE_SIZE; i++) {
                    int y = i / 24; int x = i % 24;
                    current_face[i] = get_y_channel(fb->buf, box_x + x*2, box_y + y*2, fb->width);
                    diff += abs((int)current_face[i] - (int)prev_face[i]);
                    prev_face[i] = current_face[i];
                }
                jitter_avg = diff / FACE_SIZE;
                is_live = (jitter_avg > 4.5);

                if (is_enrolling) {
                    for(int i=0; i<MAX_FACES; i++) {
                        if(!db[i].active) {
                            strncpy(db[i].name, enroll_name.c_str(), 31);
                            memcpy(db[i].template_data, current_face, FACE_SIZE);
                            db[i].active = true; save_db();
                            latest_log = "Saved: " + enroll_name;
                            is_enrolling = false; break;
                        }
                    }
                } else if (is_live) {
                    float best_mse = 99999; int m_idx = -1;
                    for(int i=0; i<MAX_FACES; i++) {
                        if(db[i].active) {
                            float mse = compare_v15(db[i].template_data, current_face);
                            if(mse < best_mse) { best_mse = mse; m_idx = i; }
                        }
                    }
                    if(m_idx != -1 && best_mse < 2200) {
                        display_name = String(db[m_idx].name);
                        name_latch_timer = millis() + 4000;
                        if(!door_unlocked) {
                            latest_log = "Welcome, " + display_name;
                            door_unlocked = true; door_timer = millis();
                            digitalWrite(RELAY_PIN, HIGH); digitalWrite(BUILTIN_LED, HIGH);
                        }
                    } else { display_name = "Unknown"; }
                }
            } 
            esp_camera_fb_return(fb);
        }
        vTaskDelay(200 / portTICK_PERIOD_MS);
    }
}

void setup() {
    Serial.begin(115200);
    pinMode(BUILTIN_LED, OUTPUT); pinMode(RELAY_PIN, OUTPUT);
    digitalWrite(RELAY_PIN, LOW); digitalWrite(BUILTIN_LED, LOW);
    load_db();
    Wire.begin(SIOD_GPIO_NUM, SIOC_GPIO_NUM, 100000);
    camera_config_t config;
    config.ledc_channel = LEDC_CHANNEL_0; config.ledc_timer = LEDC_TIMER_0;
    config.pin_d0 = Y2_GPIO_NUM; config.pin_d1 = Y3_GPIO_NUM; config.pin_d2 = Y4_GPIO_NUM; config.pin_d3 = Y5_GPIO_NUM;
    config.pin_d4 = Y6_GPIO_NUM; config.pin_d5 = Y7_GPIO_NUM; config.pin_d6 = Y8_GPIO_NUM; config.pin_d7 = Y9_GPIO_NUM;
    config.pin_xclk = XCLK_GPIO_NUM; config.pin_pclk = PCLK_GPIO_NUM; config.pin_vsync = VSYNC_GPIO_NUM;
    config.pin_href = HREF_GPIO_NUM; config.pin_sscb_sda = SIOD_GPIO_NUM; config.pin_sscb_scl = SIOC_GPIO_NUM;
    config.pin_pwdn = PWDN_GPIO_NUM; config.pin_reset = RESET_GPIO_NUM;
    config.xclk_freq_hz = 10000000;
    config.frame_size = FRAMESIZE_QVGA;
    config.pixel_format = PIXFORMAT_YUV422; // HYBRID MODE
    config.grab_mode = CAMERA_GRAB_LATEST;  // ENSURE NEW FRAMES
    config.fb_count = 2; config.fb_location = CAMERA_FB_IN_PSRAM;
    if (esp_camera_init(&config) != ESP_OK) { delay(2000); ESP.restart(); }
    
    sensor_t * s = esp_camera_sensor_get();
    if(s) { 
        s->set_vflip(s,0); s->set_hmirror(s,0); 
        s->set_brightness(s,2); s->set_contrast(s,2);
        s->set_whitebal(s, 1); // Auto white balance
        s->set_exposure_ctrl(s, 1); // Auto exposure
    }
    
    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED) delay(500);
    Serial.print("IP: "); Serial.println(WiFi.localIP());
    
    httpd_config_t server_config = HTTPD_DEFAULT_CONFIG();
    server_config.server_port = 80;
    httpd_handle_t server = NULL;
    if (httpd_start(&server, &server_config) == ESP_OK) {
        httpd_uri_t uri_idx = { "/", HTTP_GET, [](httpd_req_t *r){ return httpd_resp_send(r, INDEX_HTML, strlen(INDEX_HTML)); }, NULL };
        httpd_uri_t uri_str = { "/stream", HTTP_GET, stream_handler, NULL };
        httpd_uri_t uri_inf = { "/info", HTTP_GET, info_handler, NULL };
        httpd_uri_t uri_enr = { "/enroll", HTTP_GET, enroll_handler, NULL };
        httpd_uri_t uri_lst = { "/list", HTTP_GET, list_handler, NULL };
        httpd_uri_t uri_del = { "/delete", HTTP_GET, delete_handler, NULL };
        httpd_register_uri_handler(server, &uri_idx); httpd_register_uri_handler(server, &uri_str);
        httpd_register_uri_handler(server, &uri_inf); httpd_register_uri_handler(server, &uri_enr);
        httpd_register_uri_handler(server, &uri_lst); httpd_register_uri_handler(server, &uri_del);
    }
    xTaskCreatePinnedToCore(RecognitionTask, "RecTask", 10240, NULL, 1, NULL, 1);
}

void loop() {
    if (door_unlocked && (millis() - door_timer > 5000)) {
        digitalWrite(RELAY_PIN, LOW); digitalWrite(BUILTIN_LED, LOW);
        door_unlocked = false;
    }
    delay(100);
}
