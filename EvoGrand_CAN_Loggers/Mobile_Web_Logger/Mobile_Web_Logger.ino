#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include "driver/twai.h"

// ==================== CẤU HÌNH PHẦN CỨNG & WIFI ====================
#define CAN_TX_PIN GPIO_NUM_17
#define CAN_RX_PIN GPIO_NUM_18

const char* AP_SSID = "VinFast_Evo_Logger";
const char* AP_PASS = "12345678";

WebServer server(80);

// ==================== CẤU HÌNH DATA LOGGER (100KB BUFFER) ====================
struct CanLogEntry {
    uint32_t time_ms;
    uint32_t id;
    uint8_t dlc;
    uint8_t data[8];
};

#define MAX_CAN_LOGS 6400  // ~100KB RAM
CanLogEntry canLogBuffer[MAX_CAN_LOGS];
uint16_t logIndex = 0;
bool isLogging = false;
bool logBufferFull = false;

// ==================== TERMINAL TRỰC TIẾP TRÊN WEB ====================
#define LOG_BUFFER_SIZE 25
String logLines[LOG_BUFFER_SIZE];
uint8_t logHead = 0;
uint32_t rx_count = 0;

void pushLog(const String& line) {
    logLines[logHead] = line;
    logHead = (logHead + 1) % LOG_BUFFER_SIZE;
    Serial.println(line);
}

// ==================== GIAO DIỆN WEB CHO MOBILE ====================
const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="vi">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>Evo Grand Mobile Logger</title>
<style>
  body { background: #121212; color: #fff; font-family: sans-serif; margin: 0; padding: 15px; }
  h2 { text-align: center; color: #00ff88; margin-top: 5px; }
  .card { background: #1e1e1e; border-radius: 8px; padding: 15px; margin-bottom: 15px; border: 1px solid #333; }
  .btn-group { display: flex; gap: 10px; margin-bottom: 15px; }
  .btn { flex: 1; padding: 15px; border: none; border-radius: 8px; font-weight: bold; cursor: pointer; font-size: 14px; }
  .btn-start { background: #2ecc71; color: #000; }
  .btn-stop { background: #e74c3c; color: #fff; }
  .btn-dl { background: #0b6efd; color: #fff; width: 100%; padding: 15px; margin-bottom: 10px; border:none; border-radius:8px; font-weight:bold; cursor:pointer;}
  .status { text-align: center; font-size: 14px; color: #aaa; margin-bottom: 10px; }
  .term { background: #000; color: #00ff88; padding: 10px; border-radius: 8px; height: 300px; overflow-y: auto; font-family: monospace; font-size: 12px; }
  .blink { color: #ff4757; font-weight: bold; animation: blinker 1s linear infinite; }
  @keyframes blinker { 50% { opacity: 0; } }
</style>
</head>
<body>
  <h2>EVO GRAND LOGGER</h2>
  <div class="card">
    <div class="btn-group">
      <button class="btn btn-start" onclick="startLog()">▶ BẮT ĐẦU GHI</button>
      <button class="btn btn-stop" onclick="stopLog()">■ DỪNG GHI</button>
    </div>
    <div class="status" id="status-txt">Trạng thái: Đang chờ...</div>
    <button class="btn-dl" onclick="downloadCSV()">📥 TẢI FILE EXCEL (100KB)</button>
    <div style="text-align: center; font-size: 12px; color: #888;">Frames nhận: <span id="rx_count">0</span></div>
  </div>
  <div class="term" id="term-box">Đang kết nối CAN Bus...</div>

  <script>
    function startLog() {
      fetch('/api/start').then(() => {
        document.getElementById('status-txt').innerHTML = '<span class="blink">🔴 ĐANG GHI LOG 100KB...</span>';
      });
    }
    function stopLog() {
      fetch('/api/stop').then(() => {
        document.getElementById('status-txt').textContent = '✔ Đã dừng ghi. Sẵn sàng tải file!';
      });
    }
    function downloadCSV() { window.location.href = '/log/download'; }

    async function fetchStream() {
      try {
        let res = await fetch('/api/stream');
        let d = await res.json();
        document.getElementById('rx_count').textContent = d.rx;
        // Fix ngắt dòng JS
        document.getElementById('term-box').textContent = d.logs.join('\n');
      } catch(e) {}
    }
    setInterval(fetchStream, 500);
  </script>
</body>
</html>
)rawliteral";

// ==================== ROUTING ====================
void setup() {
    Serial.begin(115200);
    WiFi.mode(WIFI_AP);
    WiFi.softAP(AP_SSID, AP_PASS);

    server.on("/", HTTP_GET, []() { server.send_P(200, "text/html", INDEX_HTML); });
    
    server.on("/api/start", HTTP_GET, []() {
        logIndex = 0; logBufferFull = false; isLogging = true;
        pushLog(">>> BẮT ĐẦU GHI LOG...");
        server.send(200, "text/plain", "OK");
    });

    server.on("/api/stop", HTTP_GET, []() {
        isLogging = false;
        pushLog(">>> ĐÃ DỪNG GHI.");
        server.send(200, "text/plain", "OK");
    });

    server.on("/log/download", HTTP_GET, []() {
        isLogging = false;
        // Fix nháy kép và ngắt dòng C++
        server.sendHeader("Content-Disposition", "attachment; filename=\"VINFAST_EVO_100KB.csv\"");
        server.sendHeader("Connection", "close");
        server.setContentLength(CONTENT_LENGTH_UNKNOWN);
        server.send(200, "text/csv", "");
        server.sendContent("Time_ms,ID,DLC,D0,D1,D2,D3,D4,D5,D6,D7\n");

        uint16_t total = logBufferFull ? MAX_CAN_LOGS : logIndex;
        for(uint16_t i = 0; i < total; i++) {
            char buf[128];
            snprintf(buf, sizeof(buf), "%lu,0x%08X,%d,%02X,%02X,%02X,%02X,%02X,%02X,%02X,%02X\n",
                     canLogBuffer[i].time_ms, canLogBuffer[i].id, canLogBuffer[i].dlc,
                     canLogBuffer[i].data[0], canLogBuffer[i].data[1], canLogBuffer[i].data[2], canLogBuffer[i].data[3],
                     canLogBuffer[i].data[4], canLogBuffer[i].data[5], canLogBuffer[i].data[6], canLogBuffer[i].data[7]);
            server.sendContent(buf);
        }
        server.sendContent("");
        pushLog(">>> ĐÃ TẢI FILE XONG.");
    });

    server.on("/api/stream", HTTP_GET, []() {
        String json = "{\"rx\":" + String(rx_count) + ",\"logs\":[";
        bool first = true;
        for (int i = 0; i < LOG_BUFFER_SIZE; i++) {
            int idx = (logHead + i) % LOG_BUFFER_SIZE;
            if (logLines[idx].length() > 0) {
                if (!first) json += ",";
                // Fix ngoặc kép chuỗi JSON
                json += "\"" + logLines[idx] + "\"";
                first = false;
            }
        }
        json += "]}";
        server.send(200, "application/json", json);
    });

    server.begin();

    // Init CAN
    twai_general_config_t g_config = TWAI_GENERAL_CONFIG_DEFAULT(CAN_TX_PIN, CAN_RX_PIN, TWAI_MODE_NORMAL);
    twai_timing_config_t t_config = TWAI_TIMING_CONFIG_250KBITS(); 
    twai_filter_config_t f_config = TWAI_FILTER_CONFIG_ACCEPT_ALL();

    if (twai_driver_install(&g_config, &t_config, &f_config) == ESP_OK && twai_start() == ESP_OK) {
        pushLog("CAN 250kbps Initialized!");
    }
}

void loop() {
    server.handleClient();

    twai_message_t msg;
    int frames = 0;
    while (twai_receive(&msg, pdMS_TO_TICKS(1)) == ESP_OK && frames < 20) {
        frames++; rx_count++;
        
        // Push 1 số frame lên màn hình để nhìn cho vui (giới hạn tốc độ hiển thị)
        if (rx_count % 100 == 0) {
            char t_buf[32];
            snprintf(t_buf, sizeof(t_buf), "[%lu] ID: 0x%X", millis(), msg.identifier);
            pushLog(String(t_buf));
        }

        if (isLogging) {
            canLogBuffer[logIndex].time_ms = millis();
            canLogBuffer[logIndex].id = msg.identifier;
            canLogBuffer[logIndex].dlc = msg.data_length_code;
            for(int i=0; i<8; i++) canLogBuffer[logIndex].data[i] = (i < msg.data_length_code) ? msg.data[i] : 0;
            
            logIndex++;
            if (logIndex >= MAX_CAN_LOGS) { logIndex = 0; logBufferFull = true; }
        }
    }
}