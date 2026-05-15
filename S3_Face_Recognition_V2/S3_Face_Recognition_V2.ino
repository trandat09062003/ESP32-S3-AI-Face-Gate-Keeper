/* 
 * PROJECT: ESP32-S3 AI Face Gate Keeper V23 RELAY EDITION
 * FEATURES: GitHub Core + Relay Control (GPIO 21) + Recognition History
 * NOTE: Servo code removed as requested.
 */

#include <WebServer.h>
#include <WiFi.h>
#include "esp_camera.h"
#include <Wire.h>
#include <Preferences.h>
#include <time.h>

// ===========================
// PIN CONFIGURATION

// ===========================
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

#define RELAY_PIN      3   // Chân D2 - Nối vào chân IN của Relay
#define BUILTIN_LED    48

const char* ssid = "Happy House-2.4GH";
const char* password = "12345689";

WebServer server(80);

// AI Data
#define FACE_SIZE 576
struct FaceRecord {
    char name[32];
    uint8_t template_data[FACE_SIZE];
    bool active;
};
FaceRecord db[10];
uint8_t last_face[FACE_SIZE]; // Thêm biến lưu khuôn mặt trước đó
Preferences prefs;
String latest_msg = "System Ready";

// History Log
struct LogEntry {
    String name;
    String time_str;
};
LogEntry history[10];
int history_idx = 0;

// Door Logic
unsigned long door_timer = 0;
bool is_door_open = false;

void addLog(String name) {
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo)) {
        history[history_idx].time_str = "No Time";
    } else {
        char buf[32];
        strftime(buf, sizeof(buf), "%H:%M:%S", &timeinfo);
        history[history_idx].time_str = String(buf);
    }
    history[history_idx].name = name;
    history_idx = (history_idx + 1) % 10;
}

void save_db() { prefs.begin("fdb", false); prefs.putBytes("data", db, sizeof(db)); prefs.end(); }
void load_db() { 
    prefs.begin("fdb", true); 
    if(prefs.getBytes("data", db, sizeof(db)) == 0) { for(int i=0; i<10; i++) db[i].active = false; }
    prefs.end(); 
}

// ===========================
// CAMERA CORE
// ===========================

bool initCamera(framesize_t frame_size, pixformat_t format) {
    esp_camera_deinit();
    camera_config_t config;
    config.ledc_channel = LEDC_CHANNEL_0; config.ledc_timer = LEDC_TIMER_0;
    config.pin_d0 = Y2_GPIO_NUM; config.pin_d1 = Y3_GPIO_NUM; config.pin_d2 = Y4_GPIO_NUM; config.pin_d3 = Y5_GPIO_NUM;
    config.pin_d4 = Y6_GPIO_NUM; config.pin_d5 = Y7_GPIO_NUM; config.pin_d6 = Y8_GPIO_NUM; config.pin_d7 = Y9_GPIO_NUM;
    config.pin_xclk = XCLK_GPIO_NUM; config.pin_pclk = PCLK_GPIO_NUM; config.pin_vsync = VSYNC_GPIO_NUM;
    config.pin_href = HREF_GPIO_NUM; config.pin_sscb_sda = SIOD_GPIO_NUM; config.pin_sscb_scl = SIOC_GPIO_NUM;
    config.pin_pwdn = -1; config.pin_reset = -1;
    config.xclk_freq_hz = (format == PIXFORMAT_JPEG) ? 20000000 : 10000000;
    config.pixel_format = format; config.frame_size = frame_size;
    config.jpeg_quality = 10; config.fb_count = 1; config.grab_mode = CAMERA_GRAB_LATEST;
    
    esp_err_t err = esp_camera_init(&config);
    if (err == ESP_OK) {
        sensor_t * s = esp_camera_sensor_get();
        s->set_brightness(s, 2); // Tăng độ sáng lên mức 2
        s->set_contrast(s, 1);
        if(format == PIXFORMAT_GRAYSCALE) s->set_gainceiling(s, GAINCEILING_16X); // Tăng Gain lên 16X
    }
    return (err == ESP_OK);
}

// ===========================
// HANDLERS
// ===========================

void handleJpg() {
    camera_fb_t * fb = esp_camera_fb_get();
    if (!fb) { server.send(503, "text/plain", "Fail"); return; }
    server.setContentLength(fb->len);
    server.send(200, "image/jpeg");
    WiFiClient client = server.client();
    client.write(fb->buf, fb->len);
    esp_camera_fb_return(fb);
}

void handleAI() {
    initCamera(FRAMESIZE_QVGA, PIXFORMAT_GRAYSCALE);
    
    // TẤM 1: Chụp để làm mốc
    camera_fb_t * fb1 = esp_camera_fb_get();
    if(!fb1) { server.send(503, "text/plain", "Cam Fail"); return; }
    for(int i=0; i<FACE_SIZE; i++) last_face[i] = fb1->buf[(100+(i/24)*2)*320 + (130+(i%24)*2)];
    esp_camera_fb_return(fb1);

    delay(100); // Đợi 100ms để phát hiện chuyển động con người

    // TẤM 2: Chụp để so sánh
    camera_fb_t * fb = esp_camera_fb_get();
    if(fb) {
        uint8_t current[FACE_SIZE];
        float q_jitter[4] = {0, 0, 0, 0}; 
        
        for(int i=0; i<FACE_SIZE; i++) {
            current[i] = fb->buf[(100+(i/24)*2)*320 + (130+(i%24)*2)];
            int diff = abs((int)current[i] - (int)last_face[i]);
            if (diff > 6) { // Lọc nhiễu nhẹ
                int row = (i / 24); int col = (i % 24);
                int quad = (row < 12 ? 0 : 2) + (col < 12 ? 0 : 1);
                q_jitter[quad] += diff;
            }
            last_face[i] = current[i];
        }

        float max_q = 0, min_q = 99999, avg_q = 0;
        for(int i=0; i<4; i++) {
            q_jitter[i] /= (FACE_SIZE/4);
            if(q_jitter[i] > max_q) max_q = q_jitter[i];
            if(q_jitter[i] < min_q) min_q = q_jitter[i];
            avg_q += q_jitter[i];
        }
        avg_q /= 4;

        // TỶ LỆ BIẾN THIÊN (Variance): Người thật sẽ có tỷ lệ Max/Min cao (chuyển động không đều)
        float variance_ratio = (min_q > 0.1) ? (max_q / min_q) : 1.0;
        char metrics[32];
        snprintf(metrics, sizeof(metrics), " [J:%.1f V:%.1f]", avg_q, variance_ratio);
        Serial.printf("Avg Jitter: %.2f, Var Ratio: %.2f\n", avg_q, variance_ratio);

        // ĐIỀU KIỆN NGHIÊM NGẶT
        if (avg_q < 1.2 || variance_ratio < 1.4) {
            latest_msg = "SPOOF DETECTED!" + String(metrics);
            esp_camera_fb_return(fb);
            initCamera(FRAMESIZE_QVGA, PIXFORMAT_JPEG);
            server.send(200, "text/plain", latest_msg);
            return;
        }

        float best_mse = 99999; int m_idx = -1;
        for(int i=0; i<10; i++) {
            if(db[i].active) {
                long err = 0;
                for(int j=0; j<FACE_SIZE; j++) { int d = (int)current[j] - (int)db[i].template_data[j]; err += (long)d*d; }
                float mse = (float)err/FACE_SIZE;
                if(mse < best_mse) { best_mse = mse; m_idx = i; }
            }
        }
        
        if(m_idx != -1 && best_mse < 2200) {
            String name = String(db[m_idx].name);
            latest_msg = "WELCOME " + name + String(metrics);
            addLog(name + String(metrics));
            digitalWrite(RELAY_PIN, HIGH); digitalWrite(BUILTIN_LED, HIGH);
            is_door_open = true; door_timer = millis();
        } else { 
            latest_msg = "UNKNOWN PERSON" + String(metrics); 
            addLog("Unknown" + String(metrics));
        }
        esp_camera_fb_return(fb);
    }
    initCamera(FRAMESIZE_QVGA, PIXFORMAT_JPEG);
    server.send(200, "text/plain", latest_msg);
}

void handleLogs() {
    String json = "[";
    for(int i=0; i<10; i++) {
        int idx = (history_idx - 1 - i + 10) % 10;
        if(history[idx].name != "") {
            if(json != "[") json += ",";
            json += "{\"name\":\"" + history[idx].name + "\",\"time\":\"" + history[idx].time_str + "\"}";
        }
    }
    json += "]";
    server.send(200, "application/json", json);
}

void handleEnroll() {
    String name = server.arg("name");
    initCamera(FRAMESIZE_QVGA, PIXFORMAT_GRAYSCALE);
    camera_fb_t * fb = esp_camera_fb_get();
    if(fb) {
        for(int i=0; i<10; i++) {
            if(!db[i].active) {
                strncpy(db[i].name, name.c_str(), 31);
                for(int j=0; j<FACE_SIZE; j++) db[i].template_data[j] = fb->buf[(100+(j/24)*2)*320 + (130+(j%24)*2)];
                db[i].active = true; save_db(); break;
            }
        }
        esp_camera_fb_return(fb);
    }
    initCamera(FRAMESIZE_QVGA, PIXFORMAT_JPEG);
    server.send(200, "text/plain", "ENROLLED: " + name);
}

// ===========================
// UI (V23)
// ===========================

const char* INDEX_HTML = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <title>AI RELAY GATE V23</title>
    <style>
        body { font-family: 'Segoe UI', sans-serif; background: #0f172a; color: #f8fafc; text-align: center; padding: 20px; }
        .box { background: #1e293b; border-radius: 15px; padding: 20px; display: inline-block; box-shadow: 0 10px 20px rgba(0,0,0,0.3); width: 380px; }
        img { border-radius: 8px; width: 320px; height: 240px; margin-bottom: 10px; border: 2px solid #38bdf8; }
        button { background: #38bdf8; color: #fff; border: none; padding: 12px; border-radius: 8px; cursor: pointer; margin: 5px; width: 45%; font-weight: bold; }
        button:hover { background: #0ea5e9; }
        .log-table { margin-top: 20px; width: 100%; border-collapse: collapse; background: #020617; border-radius: 10px; overflow: hidden; }
        .log-table th, .log-table td { padding: 10px; border: 1px solid #1e293b; text-align: left; font-size: 13px; }
        .log-table th { background: #334155; color: #38bdf8; }
        #msg { color: #facc15; font-weight: bold; margin: 10px 0; font-size: 18px; }
    </style>
</head>
<body>
    <div class="box">
        <h3>AI ACCESS CONTROL V23</h3>
        <img id="view" src="/cam.jpg">
        <div id="msg">SYSTEM READY</div>
        <button onclick="recognize()">SCAN FACE</button>
        <button onclick="enroll()">ENROLL</button>
        <button style="background:#ef4444" onclick="fetch('/clear').then(()=>location.reload())">CLEAR DATA</button>
        
        <h4>RECOGNITION HISTORY</h4>
        <table class="log-table">
            <thead><tr><th>TIME</th><th>PERSON</th></tr></thead>
            <tbody id="log-body"></tbody>
        </table>
    </div>
    <script>
        function refresh() { document.getElementById('view').src = "/cam.jpg?t=" + Date.now(); }
        function recognize() {
            document.getElementById('msg').innerText = "SCANNING...";
            fetch('/ai').then(r => r.text()).then(t => { 
                document.getElementById('msg').innerText = t; 
                refresh(); updateLogs();
            });
        }
        function enroll() {
            let n = prompt("Name:");
            if(n) fetch('/enroll?name=' + n).then(r => r.text()).then(t => { document.getElementById('msg').innerText = t; refresh(); });
        }
        function updateLogs() {
            fetch('/history').then(r => r.json()).then(data => {
                document.getElementById('log-body').innerHTML = data.map(i => `<tr><td>${i.time}</td><td>${i.name}</td></tr>`).join('');
            });
        }
        setInterval(updateLogs, 5000);
        updateLogs();
    </script>
</body>
</html>
)rawliteral";

void setup() {
    Serial.begin(115200);
    pinMode(RELAY_PIN, OUTPUT); pinMode(BUILTIN_LED, OUTPUT);
    digitalWrite(RELAY_PIN, LOW);
    
    load_db();
    initCamera(FRAMESIZE_QVGA, PIXFORMAT_JPEG);
    
    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED) delay(500);
    
    configTime(7 * 3600, 0, "pool.ntp.org", "time.nist.gov");
    
    server.on("/", HTTP_GET, [](){ server.send(200, "text/html", INDEX_HTML); });
    server.on("/cam.jpg", HTTP_GET, handleJpg);
    server.on("/ai", HTTP_GET, handleAI);
    server.on("/history", HTTP_GET, handleLogs);
    server.on("/enroll", HTTP_GET, handleEnroll);
    server.on("/clear", HTTP_GET, [](){ for(int i=0; i<10; i++) db[i].active = false; save_db(); server.send(200); });
    
    server.begin();
    Serial.println(WiFi.localIP());
}

void loop() {
    server.handleClient();
    // Auto-off Relay after 5 seconds
    if (is_door_open && (millis() - door_timer > 5000)) {
        digitalWrite(RELAY_PIN, LOW); digitalWrite(BUILTIN_LED, LOW);
        is_door_open = false;
    }
}
