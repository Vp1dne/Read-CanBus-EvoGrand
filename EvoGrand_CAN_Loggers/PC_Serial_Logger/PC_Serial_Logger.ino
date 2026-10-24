#include <Arduino.h>
#include "driver/twai.h"

// ==================== CẤU HÌNH PHẦN CỨNG ====================
// Phiên bản PC/Laptop: Dump trực tiếp dữ liệu thô qua cáp USB.
// Hỗ trợ hiển thị trạng thái kết nối (các dòng bắt đầu bằng '#' sẽ được Excel/Python tự động bỏ qua).

#define CAN_TX_PIN GPIO_NUM_17
#define CAN_RX_PIN GPIO_NUM_18

uint32_t rx_count = 0;
uint32_t last_rx_time = 0;
bool is_connected = false;

void setup() {
    Serial.begin(115200);
    while (!Serial) delay(10); 
    
    Serial.println("# ==================================================");
    Serial.println("# VINFAST EVO GRAND - PC SERIAL LOGGER INITIALIZING");
    Serial.println("# ==================================================");

    twai_general_config_t g_config = TWAI_GENERAL_CONFIG_DEFAULT(CAN_TX_PIN, CAN_RX_PIN, TWAI_MODE_NORMAL);
    twai_timing_config_t t_config = TWAI_TIMING_CONFIG_250KBITS();
    twai_filter_config_t f_config = TWAI_FILTER_CONFIG_ACCEPT_ALL();

    if (twai_driver_install(&g_config, &t_config, &f_config) == ESP_OK && twai_start() == ESP_OK) {
        Serial.println("# >>> [OK] CAN Bus Started at 250kbps.");
        Serial.println("# >>> Dang lang nghe... (Luu y: Cac dong co dau '#' se bi an khi luu CSV)");
        Serial.println("Time_ms,ID,DLC,D0,D1,D2,D3,D4,D5,D6,D7");
        last_rx_time = millis();
    } else {
        Serial.println("# >>> [LOI] KHOI TAO CAN BUS THAT BAI! Kiem tra lai day TX/RX.");
    }
}

void loop() {
    twai_message_t msg;
    
    // Đọc liên tục từ CAN Bus
    if (twai_receive(&msg, pdMS_TO_TICKS(1)) == ESP_OK) {
        if (!is_connected) {
            is_connected = true;
            Serial.println("# >>> [SUCCESS] DA KET NOI VA NHAN DUOC TIN HIEU TU XE!");
        }
        
        rx_count++;
        last_rx_time = millis();
        
        // In ra Serial theo đúng định dạng CSV
        Serial.printf("%lu,0x%08X,%d,", millis(), msg.identifier, msg.data_length_code);
        
        for (int i = 0; i < 8; i++) {
            if (i < msg.data_length_code) {
                Serial.printf("%02X", msg.data[i]);
            } else {
                Serial.print("00"); // Padding byte trống
            }
            if (i < 7) Serial.print(",");
        }
        Serial.println();

        // Báo cáo nhịp tim định kỳ mỗi 5000 gói tin để người dùng biết mạch chưa treo
        if (rx_count % 5000 == 0) {
            Serial.printf("# [INFO] Da ghi nhan tong cong %lu goi tin...\n", rx_count);
        }
        
    } else {
        // Mất tín hiệu quá 3 giây (Do tắt chìa khóa hoặc tuột dây)
        if (millis() - last_rx_time > 3000) {
            is_connected = false;
            Serial.printf("# [WAITING] %lu ms: Dang cho tin hieu CAN tu xe...\n", millis());
            last_rx_time = millis(); // Reset để 3 giây sau mới báo tiếp
        }
    }
}