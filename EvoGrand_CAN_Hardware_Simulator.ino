#include <Arduino.h>
#include "driver/twai.h"
#include <math.h>

// =================================================================================
// VINFAST EVO GRAND (2.4KWH) - CAN BUS HARDWARE SIMULATOR (NODE TRANSMITTER)
// Tác giả: DevByNVP (2026)
// Mục đích: Phát giả lập toàn bộ mạng CAN Bus VinFast Evo Grand chuẩn 250kbps ra
//          đường truyền vật lý (TX/RX) để test Dashboard, HMI, Logger độc lập.
// =================================================================================

#define CAN_TX_PIN GPIO_NUM_17
#define CAN_RX_PIN GPIO_NUM_18

// Hàm truyền gói tin CAN vật lý với timeout 5ms
bool sendCanMessage(uint32_t id, const uint8_t* data, uint8_t len) {
    twai_message_t tx_msg;
    tx_msg.identifier = id;
    tx_msg.extd = 0;              // Standard 11-bit ID
    tx_msg.rtr = 0;               // Data frame
    tx_msg.data_length_code = len;
    for (uint8_t i = 0; i < len; i++) {
        tx_msg.data[i] = data[i];
    }
    return (twai_transmit(&tx_msg, pdMS_TO_TICKS(5)) == ESP_OK);
}

void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println();
    Serial.println(F("================================================================"));
    Serial.println(F("VINFAST EVO GRAND (2.4KWH LFP) - CAN BUS HARDWARE SIMULATOR"));
    Serial.println(F("Firmware phát giả lập chuẩn mạng CAN 250kbps | DevByNVP"));
    Serial.println(F("================================================================"));

    // Khởi tạo TWAI (CAN Controller) 250kbps
    // Lưu ý: Nếu chỉ test 1 board phát một mình không có node nhận (không có ACK), 
    // có thể đổi TWAI_MODE_NORMAL thành TWAI_MODE_NO_ACK.
    twai_general_config_t g_config = TWAI_GENERAL_CONFIG_DEFAULT(CAN_TX_PIN, CAN_RX_PIN, TWAI_MODE_NORMAL);
    twai_timing_config_t t_config = TWAI_TIMING_CONFIG_250KBITS();
    twai_filter_config_t f_config = TWAI_FILTER_CONFIG_ACCEPT_ALL();

    if (twai_driver_install(&g_config, &t_config, &f_config) == ESP_OK && twai_start() == ESP_OK) {
        Serial.println(F("[CAN] TWAI Driver 250kbps khoi chay THANH CONG."));
        Serial.println(F("[CAN] Bat dau ban cac Frame: 0x101, 0x102, 0x201, 0x208, 0x300, 0x309, 0x30A, 0x310..0x31A, 0x320..0x329, 0x400"));
    } else {
        Serial.println(F("[CAN] LOI: Khong the khoi tao CAN Driver 250kbps! Kiem tra chan TX/RX."));
    }
}

void loop() {
    static unsigned long lastFastCycle = 0;    // 20ms: 0x201 (RPM, ODO), 0x208 (Phase Current, Temp MCU)
    static unsigned long lastMediumCycle = 0;  // 100ms: 0x101 (Power), 0x102 (VCU/Mode), 0x309 (V/I Pack)
    static unsigned long lastSlowCycle = 0;    // 500ms: 0x300 (Serial), 0x30A (SOC), 0x310..0x31A (Cells), 0x320..0x329 (Temps), 0x400 (HMI ODO)

    unsigned long now = millis();

    // -----------------------------------------------------------------------------
    // THUẬT TOÁN ĐỘNG HỌC MÔ PHỎNG HÀNH TRÌNH XE ĐANG CHẠY THẬT
    // -----------------------------------------------------------------------------
    static float simTime = 0.0f;
    simTime += 0.02f;

    // Vận tốc dao động mượt mà hình Sin: 0 -> 63 km/h (RPM từ 0 -> 630)
    float speedSim = 35.0f + 28.0f * sin(simTime * 0.35f);
    if (speedSim < 0.0f) speedSim = 0.0f;
    uint16_t simRPM = (uint16_t)(speedSim * 10.0f);

    // ODO lăn bánh thực tế (Tăng mỗi khi bánh xe quay)
    static uint32_t simRawOdo = 12505; // Mốc ban đầu: 1250.5 Km
    static unsigned long lastOdoInc = 0;
    if (now - lastOdoInc >= 1000) {
        lastOdoInc = now;
        if (simRPM > 100) simRawOdo++; // Tăng thêm 100m
    }

    // Mô phỏng dòng pha (Phase Current 0x208 D1) & Cờ phanh (0x201 D7)
    int8_t simPhaseI = 0;
    bool simBraking = false;
    float dSpd = cos(simTime * 0.35f);
    if (dSpd > 0.15f) {
        simPhaseI = (int8_t)(dSpd * 90.0f);  // Kéo ga tăng tốc: Dòng pha dương 10..90
        simBraking = false;
    } else if (dSpd < -0.55f) {
        simPhaseI = (int8_t)(dSpd * 95.0f);  // Phanh điện Regen: Dòng pha âm bù 2 (-45..-90)
        simBraking = true;
    } else {
        simPhaseI = 0;                       // Trôi xe tự do (Coast)
        simBraking = false;
    }

    // -----------------------------------------------------------------------------
    // CHU KỲ NHANH: 20ms (Tương đương 50Hz)
    // -----------------------------------------------------------------------------
    if (now - lastFastCycle >= 20) {
        lastFastCycle = now;

        // [0x201] - MCU: RPM, ODO đồng bộ mâm, Phanh/Regen
        uint8_t d201[8];
        d201[0] = (uint8_t)(simRPM >> 8);
        d201[1] = (uint8_t)(simRPM & 0xFF);
        d201[2] = 0x00;
        d201[3] = (uint8_t)((simRawOdo >> 16) & 0xFF);
        d201[4] = (uint8_t)((simRawOdo >> 8) & 0xFF);
        d201[5] = (uint8_t)(simRawOdo & 0xFF);
        d201[6] = 0x3E;
        d201[7] = simBraking ? 0x10 : 0x00; // 0x10: Đang bóp phanh
        sendCanMessage(0x201, d201, 8);

        // [0x208] - MCU: Dòng pha tức thời (D1), Nhiệt độ mâm/IC (D3), Mã lỗi (D4..D7)
        uint8_t d208[8];
        d208[0] = (simPhaseI < 0) ? 0xFF : 0x00; // Byte dấu bù 2
        d208[1] = (uint8_t)simPhaseI;            // D1: Dòng pha
        d208[2] = 0x01;
        d208[3] = (uint8_t)(48 + (simRPM / 45)); // D3: Nhiệt độ MCU tăng theo tải (48..62°C)
        d208[4] = 0x00;
        d208[5] = 0x00;
        d208[6] = 0x00;
        d208[7] = 0x00;
        sendCanMessage(0x208, d208, 8);
    }

    // -----------------------------------------------------------------------------
    // CHU KỲ TRUNG BÌNH: 100ms (Tương đương 10Hz)
    // -----------------------------------------------------------------------------
    if (now - lastMediumCycle >= 100) {
        lastMediumCycle = now;

        // [0x101] - Smartkey / Nguồn: Key ON (0x11), ACC (0x10)
        uint8_t d101[3] = {0x11, 0x10, 0x01};
        sendCanMessage(0x101, d101, 3);

        // [0x102] - VCU: Trạng thái P/Mode (D0), Chân chống (D1), Rolling Counter (D7)
        static uint8_t rollCount = 0;
        uint8_t d102[8] = {
            0x31,             // Ready + Sport Mode (0x31)
            0x00,             // Chân chống đá lên (0x10 là gạt xuống)
            0x01, 0x86, 0x02, 0x80, 0x00,
            ++rollCount       // Rolling counter tăng liên tục
        };
        sendCanMessage(0x102, d102, 8);

        // [0x309] - BMS: Điện áp Pack (D2-D3) & Dòng điện Pack bù 2 (D6-D7)
        float simVolt = 62.8f - (simPhaseI * 0.02f); // Sụt áp khi ép ga, dội áp khi phanh
        uint16_t rawV = (uint16_t)(simVolt / 0.002f);
        int16_t rawI = 0;
        if (simPhaseI > 0) rawI = (int16_t)(-simPhaseI * 14.5f);     // Xả kéo tải (Âm)
        else if (simPhaseI < 0) rawI = (int16_t)(-simPhaseI * 2.6f); // Sạc Regen (Dương)

        uint8_t d309[8] = {
            0x00, 0x00,
            (uint8_t)(rawV >> 8), (uint8_t)(rawV & 0xFF),
            0x00, 0x00,
            (uint8_t)(((uint16_t)rawI >> 8) & 0xFF), (uint8_t)((uint16_t)rawI & 0xFF)
        };
        sendCanMessage(0x309, d309, 8);
    }

    // -----------------------------------------------------------------------------
    // CHU KỲ CHẬM: 500ms (Tương đương 2Hz)
    // -----------------------------------------------------------------------------
    if (now - lastSlowCycle >= 500) {
        lastSlowCycle = now;

        // [0x300] - Serial khối Pin
        uint8_t d300[8] = {0x04, 0xE2, 0xFA, 0x24, 0x06, 0x18, 0xF8, 0xB0};
        sendCanMessage(0x300, d300, 8);

        // [0x303] - BMS Heartbeat
        static uint8_t bmsHeartbeat = 0;
        uint8_t d303[8] = {++bmsHeartbeat, 0x30, 0x00, 0x24, 0x00, 0x00, 0x00, 0x00};
        sendCanMessage(0x303, d303, 8);

        // [0x30A] - Dung lượng Pin: SOH 100% (D0/D3), SOC 85% (D1/D2), Chu kỳ 5 (D4-D5)
        uint8_t d30A[8] = {100, 85, 85, 100, 0x00, 0x05, 0x00, 0x00};
        sendCanMessage(0x30A, d30A, 8);

        // [0x310] - BMS: Áp TB (D0-D1), Min (D2-D3), Cell Min (D4), Max (D5-D6), Cell Max (D7)
        uint8_t d310[8] = {
            0x80, 0x91,  // 3.291V Áp TB
            0x80, 0x48,  // 3.272V Áp Min
            11,          // Thứ tự Cell Min: Cell 11
            0x80, 0xB2,  // 3.312V Áp Max
            7            // Thứ tự Cell Max: Cell 7
        };
        sendCanMessage(0x310, d310, 8);

        // [0x311 .. 0x314] - Chi tiết Cell 1 đến Cell 16 (4 cell mỗi gói, raw / 10000 V)
        for (uint8_t c = 0; c < 4; c++) {
            uint8_t dCell[8];
            for (uint8_t b = 0; b < 4; b++) {
                uint16_t v = 32880 + (b * 6) + (c * 4);
                if (c == 1 && b == 2) v = 33120; // Giả lập Cell 7 Max (3.312V)
                if (c == 2 && b == 2) v = 32720; // Giả lập Cell 11 Min (3.272V)
                dCell[b * 2] = (uint8_t)(v >> 8);
                dCell[b * 2 + 1] = (uint8_t)(v & 0xFF);
            }
            sendCanMessage(0x311 + c, dCell, 8);
        }

        // [0x31A] - Cell 17, 18, 19 (Cell 20 = 00 00 khuyết vì hệ 19S)
        uint8_t d31A[8] = {
            0x80, 0x55,  // Cell 17: 3.285V
            0x80, 0x58,  // Cell 18: 3.288V
            0x80, 0x52,  // Cell 19: 3.282V
            0x00, 0x00   // Cell 20 khuyết
        };
        sendCanMessage(0x31A, d31A, 8);

        // [0x320] - Nhiệt độ Pin (D0 = TB 27°C, D3 = HMI 28°C)
        uint8_t d320[5] = {27, 26, 3, 28, 4};
        sendCanMessage(0x320, d320, 5);

        // [0x328] - Cảm biến nhiệt T1, T2 (27°C, 28°C)
        uint8_t d328[2] = {27, 28};
        sendCanMessage(0x328, d328, 2);

        // [0x329] - Cảm biến nhiệt T3, T4 (26°C, 28°C)
        uint8_t d329[2] = {26, 28};
        sendCanMessage(0x329, d329, 2);

        // [0x400] - HMI: ODO Tổng lưu trên đồng hồ (D5-D7, đơn vị 0.1km)
        uint8_t d400[8] = {
            0x10, 0x1A, 0x10, 0x00, 0x00,
            (uint8_t)((simRawOdo >> 16) & 0xFF),
            (uint8_t)((simRawOdo >> 8) & 0xFF),
            (uint8_t)(simRawOdo & 0xFF)
        };
        sendCanMessage(0x400, d400, 8);
    }
}