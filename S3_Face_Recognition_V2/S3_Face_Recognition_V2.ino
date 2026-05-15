/* 
 * PROJECT: ESP32-S3 AI Face Gate Keeper V27 TFT EDITION
 * FEATURES: Face Recognition + Anti-Spoofing + ST7735 Display + mDNS
 */

#include <WebServer.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include "esp_camera.h"
#include <Wire.h>
#include <Preferences.h>
#include <time.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#include <SPI.h>

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

#define RELAY_PIN      3
#define BUILTIN_LED    48

// TFT PINS (ST7735)
#define TFT_CS         42
#define TFT_RST        41
#define TFT_DC         40
#define TFT_MOSI       39
#define TFT_SCLK       38

Adafruit_ST7735 tft = Adafruit_ST7735(TFT_CS, TFT_DC, TFT_MOSI, TFT_SCLK, TFT_RST);

const char* ssid = "VIETSET_TECH";
const char* password = "vs68686868";

WebServer server(80);

// AI Data
#define FACE_SIZE 576
struct FaceRecord {
    char name[32];
    uint8_t template_data[FACE_SIZE];
    bool active;
};
FaceRecord db[10];
uint8_t last_face[FACE_SIZE];
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

// ===========================
// HELPERS
// ===========================

void displayDoorStatus() {
    tft.fillRect(0, 110, 160, 18, ST77XX_BLACK);
    tft.setCursor(5, 115);
    tft.setTextSize(1);
    if (is_door_open) {
        tft.setTextColor(ST77XX_GREEN);
        tft.println("TRANG THAI: CUA MO");
    } else {
        tft.setTextColor(ST77XX_WHITE);
        tft.println("TRANG THAI: CUA DONG");
    }
}

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
        s->set_brightness(s, 2);
        s->set_contrast(s, 1);
        if(format == PIXFORMAT_GRAYSCALE) s->set_gainceiling(s, GAINCEILING_16X);
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
    
    camera_fb_t * fb1 = esp_camera_fb_get();
    if(!fb1) { server.send(503, "text/plain", "Cam Fail"); return; }
    for(int i=0; i<FACE_SIZE; i++) last_face[i] = fb1->buf[(96+(i/24)*2)*320 + (136+(i%24)*2)];
    esp_camera_fb_return(fb1);

    delay(100); 

    camera_fb_t * fb = esp_camera_fb_get();
    if (!fb) return;

    tft.fillRect(0, 40, 160, 40, ST77XX_BLACK);
    tft.setCursor(5, 50);
    tft.setTextColor(ST77XX_YELLOW);
    tft.setTextSize(1);
    tft.println("DANG QUET MAT...");

    uint8_t current_grayscale[FACE_SIZE];
    float q_jitter[4] = {0, 0, 0, 0}; 
    
    for(int i=0; i<FACE_SIZE; i++) {
        current_grayscale[i] = fb->buf[(96+(i/24)*2)*320 + (136+(i%24)*2)];
        int diff = abs((int)current_grayscale[i] - (int)last_face[i]);
        if (diff > 6) {
            int row = (i / 24); int col = (i % 24);
            int quad = (row < 12 ? 0 : 2) + (col < 12 ? 0 : 1);
            q_jitter[quad] += diff;
        }
        last_face[i] = current_grayscale[i];
    }

    float max_q = 0, min_q = 99999, avg_q = 0;
    for(int i=0; i<4; i++) {
        q_jitter[i] /= (FACE_SIZE/4);
        if(q_jitter[i] > max_q) max_q = q_jitter[i];
        if(q_jitter[i] < min_q) min_q = q_jitter[i];
        avg_q += q_jitter[i];
    }
    avg_q /= 4;

    float variance_ratio = (min_q > 0.1) ? (max_q / min_q) : 1.0;
    char metrics[32];
    snprintf(metrics, sizeof(metrics), " [J:%.1f V:%.1f]", avg_q, variance_ratio);

    if (avg_q < 1.2 || variance_ratio < 1.4) {
        latest_msg = "SPOOF DETECTED!" + String(metrics);
        tft.fillRect(0, 40, 160, 70, ST77XX_BLACK);
        tft.setCursor(5, 50);
        tft.setTextColor(ST77XX_RED);
        tft.setTextSize(2);
        tft.println("GIA MAO!");
        tft.setTextSize(1);
        tft.setCursor(5, 80);
        tft.println("Canh bao anh tinh!");
        
        esp_camera_fb_return(fb);
        initCamera(FRAMESIZE_QVGA, PIXFORMAT_JPEG);
        server.send(200, "text/plain", latest_msg);
        return;
    }

    float best_mse = 99999; int found_idx = -1;
    for(int i=0; i<10; i++) {
        if(db[i].active) {
            long err = 0;
            for(int j=0; j<FACE_SIZE; j++) { 
                int d = (int)current_grayscale[j] - (int)db[i].template_data[j]; 
                err += (long)d*d; 
            }
            float mse = (float)err/FACE_SIZE;
            if(mse < best_mse && mse < 1200) { 
                best_mse = mse; 
                found_idx = i; 
            }
        }
    }

    if (found_idx != -1) {
        struct tm timeinfo;
        String t_str = "00:00:00";
        if(getLocalTime(&timeinfo)) {
            char buf[16]; strftime(buf, sizeof(buf), "%H:%M:%S", &timeinfo);
            t_str = String(buf);
        }

        latest_msg = "WELCOME " + String(db[found_idx].name) + String(metrics);
        tft.fillRect(0, 40, 160, 70, ST77XX_BLACK);
        tft.setTextColor(ST77XX_GREEN);
        tft.setCursor(5, 50);
        tft.setTextSize(1);
        tft.println("XAC NHAN THANH CONG!");
        tft.setCursor(5, 65);
        tft.setTextSize(2);
        tft.print("CHAO "); tft.println(db[found_idx].name);
        tft.setCursor(5, 85);
        tft.setTextSize(1);
        tft.setTextColor(ST77XX_WHITE);
        tft.print("Luc: "); tft.println(t_str);

        addLog(db[found_idx].name);
        digitalWrite(RELAY_PIN, HIGH);
        is_door_open = true;
        displayDoorStatus();
        door_timer = millis();
    } else {
        latest_msg = "UNKNOWN PERSON" + String(metrics);
        tft.fillRect(0, 40, 160, 70, ST77XX_BLACK);
        tft.setCursor(5, 50);
        tft.setTextColor(ST77XX_ORANGE);
        tft.setTextSize(2);
        tft.println("NGUOI LA");
        tft.setTextSize(1);
        tft.setCursor(5, 80);
        tft.println("Khong co trong database");
        addLog("Unknown" + String(metrics));
    }
    
    esp_camera_fb_return(fb);
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
                for(int j=0; j<FACE_SIZE; j++) db[i].template_data[j] = fb->buf[(96+(j/24)*2)*320 + (136+(j%24)*2)];
                db[i].active = true; save_db(); break;
            }
        }
        esp_camera_fb_return(fb);
    }
    initCamera(FRAMESIZE_QVGA, PIXFORMAT_JPEG);
    server.send(200, "text/plain", "ENROLLED: " + name);
}

// ===========================
// UI
// ===========================

const char* INDEX_HTML = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <title>AI RELAY GATE</title>
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
        <h3>AI ACCESS CONTROL</h3>
        <img id="view" src="/cam.jpg">
        <div id="msg">SYSTEM READY</div>
        <button onclick="recognize()">SCAN FACE</button>
        <button onclick="enroll()">ENROLL</button>
        <button style="background:#ef4444" onclick="fetch('/clear').then(()=>location.reload())">CLEAR DATA</button>
        <h4>HISTORY</h4>
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
                document.getElementById('msg').innerText = t; refresh(); updateLogs();
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
        setInterval(updateLogs, 5000); updateLogs();
    </script>
</body>
</html>
)rawliteral";

void setup() {
    Serial.begin(115200);
    pinMode(RELAY_PIN, OUTPUT); pinMode(BUILTIN_LED, OUTPUT);
    digitalWrite(RELAY_PIN, LOW);
    
    load_db();
    
    tft.initR(INITR_BLACKTAB);
    tft.setRotation(1);
    tft.fillScreen(ST77XX_BLACK);
    tft.setTextColor(ST77XX_WHITE);
    tft.setTextSize(1);
    tft.setCursor(0, 10);
    tft.println("AI FACE GATE V27");
    tft.drawFastHLine(0, 25, 160, ST77XX_WHITE);
    tft.setCursor(0, 40);
    tft.println("Dang ket noi WiFi...");

    initCamera(FRAMESIZE_QVGA, PIXFORMAT_JPEG);

    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED) delay(500);
    
    tft.fillRect(0, 40, 160, 40, ST77XX_BLACK);
    tft.setCursor(0, 40);
    tft.println("WiFi: OK!");
    tft.println(WiFi.localIP().toString());
    delay(2000);
    tft.fillRect(0, 40, 160, 40, ST77XX_BLACK);
    tft.setCursor(5, 50);
    tft.println("HE THONG SAN SANG");
    displayDoorStatus();
    
    if (MDNS.begin("gatekeeper")) {
        Serial.println("MDNS started: http://gatekeeper.local");
    }
    
    configTime(7 * 3600, 0, "pool.ntp.org", "time.nist.gov");
    
    server.on("/", HTTP_GET, [](){ server.send(200, "text/html", INDEX_HTML); });
    server.on("/cam.jpg", HTTP_GET, handleJpg);
    server.on("/ai", HTTP_GET, handleAI);
    server.on("/history", HTTP_GET, handleLogs);
    server.on("/enroll", HTTP_GET, handleEnroll);
    server.on("/clear", HTTP_GET, [](){ for(int i=0; i<10; i++) db[i].active = false; save_db(); server.send(200); });
    
    server.begin();
}

void loop() {
    server.handleClient();
    if (is_door_open && millis() - door_timer > 5000) {
        digitalWrite(RELAY_PIN, LOW);
        is_door_open = false;
        displayDoorStatus();
    }
}
