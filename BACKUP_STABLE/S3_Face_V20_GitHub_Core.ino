/* 
 * PROJECT: ESP32-S3 AI Face Gate Keeper V20 GITHUB CORE
 * STYLE: Exact implementation of Infineon-X GitHub with AI features added
 */

#include <WebServer.h>
#include <WiFi.h>
#include "esp_camera.h"
#include <Wire.h>
#include <Preferences.h>

// ===========================
// XIAO SENSE PINS (GITHUB)
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

#define RELAY_PIN      21
#define BUILTIN_LED    48

const char* ssid = "Kata";
const char* password = "Katana3936@";

WebServer server(80);

// AI Data
#define FACE_SIZE 576
struct FaceRecord {
    char name[32];
    uint8_t template_data[FACE_SIZE];
    bool active;
};
FaceRecord db[10];
Preferences prefs;
uint8_t last_face[FACE_SIZE];
String latest_msg = "Ready";

void save_db() { prefs.begin("fdb", false); prefs.putBytes("data", db, sizeof(db)); prefs.end(); }
void load_db() { 
    prefs.begin("fdb", true); 
    if(prefs.getBytes("data", db, sizeof(db)) == 0) { for(int i=0; i<10; i++) db[i].active = false; }
    prefs.end(); 
}

// ===========================
// CAMERA CORE (GITHUB STYLE)
// ===========================

bool initCamera(framesize_t frame_size) {
    esp_camera_deinit();
    camera_config_t config;
    config.ledc_channel = LEDC_CHANNEL_0;
    config.ledc_timer = LEDC_TIMER_0;
    config.pin_d0 = Y2_GPIO_NUM; config.pin_d1 = Y3_GPIO_NUM; config.pin_d2 = Y4_GPIO_NUM; config.pin_d3 = Y5_GPIO_NUM;
    config.pin_d4 = Y6_GPIO_NUM; config.pin_d5 = Y7_GPIO_NUM; config.pin_d6 = Y8_GPIO_NUM; config.pin_d7 = Y9_GPIO_NUM;
    config.pin_xclk = XCLK_GPIO_NUM; config.pin_pclk = PCLK_GPIO_NUM; config.pin_vsync = VSYNC_GPIO_NUM;
    config.pin_href = HREF_GPIO_NUM; config.pin_sscb_sda = SIOD_GPIO_NUM; config.pin_sscb_scl = SIOC_GPIO_NUM;
    config.pin_pwdn = PWDN_GPIO_NUM; config.pin_reset = RESET_GPIO_NUM;
    config.xclk_freq_hz = 20000000;
    config.pixel_format = PIXFORMAT_JPEG;
    config.frame_size = frame_size;
    config.jpeg_quality = 10;
    config.fb_count = 1;
    config.grab_mode = CAMERA_GRAB_LATEST;
    
    esp_err_t err = esp_camera_init(&config);
    if (err == ESP_OK) {
        sensor_t * s = esp_camera_sensor_get();
        s->set_brightness(s, 1);
        s->set_contrast(s, 1);
    }
    return (err == ESP_OK);
}

void handleJpg() {
    camera_fb_t * fb = esp_camera_fb_get();
    if (!fb) { server.send(503, "text/plain", "Camera Capture Failed"); return; }
    server.setContentLength(fb->len);
    server.send(200, "image/jpeg");
    WiFiClient client = server.client();
    client.write(fb->buf, fb->len);
    esp_camera_fb_return(fb);
}

// ===========================
// AI RECOGNITION (CUSTOM ADD)
// ===========================

void handleAI() {
    // 1. Switch to Grayscale for AI
    esp_camera_deinit();
    camera_config_t config;
    config.ledc_channel = LEDC_CHANNEL_0; config.ledc_timer = LEDC_TIMER_0;
    config.pin_d0 = Y2_GPIO_NUM; config.pin_d1 = Y3_GPIO_NUM; config.pin_d2 = Y4_GPIO_NUM; config.pin_d3 = Y5_GPIO_NUM;
    config.pin_d4 = Y6_GPIO_NUM; config.pin_d5 = Y7_GPIO_NUM; config.pin_d6 = Y8_GPIO_NUM; config.pin_d7 = Y9_GPIO_NUM;
    config.pin_xclk = XCLK_GPIO_NUM; config.pin_pclk = PCLK_GPIO_NUM; config.pin_vsync = VSYNC_GPIO_NUM;
    config.pin_href = HREF_GPIO_NUM; config.pin_sscb_sda = SIOD_GPIO_NUM; config.pin_sscb_scl = SIOC_GPIO_NUM;
    config.pin_pwdn = PWDN_GPIO_NUM; config.pin_reset = RESET_GPIO_NUM;
    config.xclk_freq_hz = 10000000;
    config.pixel_format = PIXFORMAT_GRAYSCALE;
    config.frame_size = FRAMESIZE_QVGA;
    config.fb_count = 1;
    esp_camera_init(&config);

    camera_fb_t * fb = esp_camera_fb_get();
    if(fb) {
        // Simple Recognition Logic
        uint8_t current[FACE_SIZE];
        for(int i=0; i<FACE_SIZE; i++) current[i] = fb->buf[(100+(i/24)*2)*320 + (130+(i%24)*2)];
        
        float best_mse = 99999; int m_idx = -1;
        for(int i=0; i<10; i++) {
            if(db[i].active) {
                long err = 0;
                for(int j=0; j<FACE_SIZE; j++) { int d = (int)current[j] - (int)db[i].template_data[j]; err += (long)d*d; }
                float mse = (float)err/FACE_SIZE;
                if(mse < best_mse) { best_mse = mse; m_idx = i; }
            }
        }
        
        if(m_idx != -1 && best_mse < 2000) {
            latest_msg = "Welcome " + String(db[m_idx].name);
            digitalWrite(RELAY_PIN, HIGH); digitalWrite(BUILTIN_LED, HIGH);
            delay(3000);
            digitalWrite(RELAY_PIN, LOW); digitalWrite(BUILTIN_LED, LOW);
        } else { latest_msg = "Unknown Person"; }
        esp_camera_fb_return(fb);
    }
    
    // Switch back to Color for UI
    initCamera(FRAMESIZE_QVGA);
    server.send(200, "text/plain", latest_msg);
}

void handleEnroll() {
    String name = server.arg("name");
    esp_camera_deinit();
    camera_config_t config;
    config.ledc_channel = LEDC_CHANNEL_0; config.ledc_timer = LEDC_TIMER_0;
    config.pin_d0 = Y2_GPIO_NUM; config.pin_d1 = Y3_GPIO_NUM; config.pin_d2 = Y4_GPIO_NUM; config.pin_d3 = Y5_GPIO_NUM;
    config.pin_d4 = Y6_GPIO_NUM; config.pin_d5 = Y7_GPIO_NUM; config.pin_d6 = Y8_GPIO_NUM; config.pin_d7 = Y9_GPIO_NUM;
    config.pin_xclk = XCLK_GPIO_NUM; config.pin_pclk = PCLK_GPIO_NUM; config.pin_vsync = VSYNC_GPIO_NUM;
    config.pin_href = HREF_GPIO_NUM; config.pin_sscb_sda = SIOD_GPIO_NUM; config.pin_sscb_scl = SIOC_GPIO_NUM;
    config.pin_pwdn = PWDN_GPIO_NUM; config.pin_reset = RESET_GPIO_NUM;
    config.xclk_freq_hz = 10000000;
    config.pixel_format = PIXFORMAT_GRAYSCALE;
    config.frame_size = FRAMESIZE_QVGA;
    config.fb_count = 1;
    esp_camera_init(&config);
    
    camera_fb_t * fb = esp_camera_fb_get();
    if(fb) {
        for(int i=0; i<10; i++) {
            if(!db[i].active) {
                strncpy(db[i].name, name.c_str(), 31);
                for(int j=0; j<FACE_SIZE; j++) db[i].template_data[j] = fb->buf[(100+(j/24)*2)*320 + (130+(j%24)*2)];
                db[i].active = true; save_db();
                break;
            }
        }
        esp_camera_fb_return(fb);
    }
    initCamera(FRAMESIZE_QVGA);
    server.send(200, "text/plain", "Enrolled " + name);
}

// ===========================
// UI (GITHUB STYLE)
// ===========================

const char* INDEX_HTML = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <title>XIAO AI CORE V20</title>
    <style>
        body { font-family: monospace; background: #000; color: #0f0; text-align: center; padding: 20px; }
        .box { border: 2px solid #0f0; padding: 20px; display: inline-block; }
        img { border: 1px solid #0f0; margin: 10px 0; max-width: 100%; }
        button { background: #000; color: #0f0; border: 1px solid #0f0; padding: 10px; cursor: pointer; margin: 5px; }
        button:hover { background: #0f0; color: #000; }
    </style>
</head>
<body>
    <div class="box">
        <h1>[ XIAO AI CORE V20 ]</h1>
        <p>IP: <span id="ip"></span></p>
        <img id="view" src="/cam.jpg">
        <br>
        <button onclick="refresh()">REFRESH VIEW</button>
        <button onclick="recognize()">AI RECOGNIZE</button>
        <button onclick="enroll()">ENROLL USER</button>
        <button onclick="clearAll()">CLEAR DB</button>
        <h2 id="msg">SYSTEM READY</h2>
    </div>
    <script>
        document.getElementById('ip').innerText = window.location.host;
        function refresh() { document.getElementById('view').src = "/cam.jpg?t=" + Date.now(); }
        function recognize() {
            document.getElementById('msg').innerText = "RECOGNIZING...";
            fetch('/ai').then(r => r.text()).then(t => { document.getElementById('msg').innerText = t; refresh(); });
        }
        function enroll() {
            let n = prompt("Name:");
            if(n) fetch('/enroll?name=' + n).then(r => r.text()).then(t => { document.getElementById('msg').innerText = t; });
        }
        function clearAll() { if(confirm("Clear All?")) fetch('/clear').then(() => location.reload()); }
    </script>
</body>
</html>
)rawliteral";

void setup() {
    Serial.begin(115200);
    pinMode(RELAY_PIN, OUTPUT); pinMode(BUILTIN_LED, OUTPUT);
    digitalWrite(RELAY_PIN, LOW);
    load_db();
    
    initCamera(FRAMESIZE_QVGA);
    
    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED) delay(500);
    Serial.println(WiFi.localIP());

    server.on("/", HTTP_GET, [](){ server.send(200, "text/html", INDEX_HTML); });
    server.on("/cam.jpg", HTTP_GET, handleJpg);
    server.on("/ai", HTTP_GET, handleAI);
    server.on("/enroll", HTTP_GET, handleEnroll);
    server.on("/clear", HTTP_GET, [](){ for(int i=0; i<10; i++) db[i].active = false; save_db(); server.send(200); });
    
    server.begin();
}

void loop() {
    server.handleClient();
}
