# Read-CanBus-EvoGrand
 READ CAN BUS VINFAST EVO GRAND (PIN LFP 2.4KWH - HỆ 19S)
================================================================================
⚡ READ CAN BUS VINFAST EVO GRAND (PIN LFP 2.4KWH - HỆ 19S)
Tác giả / Nhà phát triển: DevByNVP (2026)
Dự án: Nghiên cứu, dịch ngược và phát triển hệ sinh thái CAN Bus mã nguồn mở
Dự án cũng có thể được tham khảo và phát triển cho các dòng xe điện sử dụng 
pin 1.5 hoặc 2.4 của Vinfast (EvoGrandLite, Feliz...)
LƯU Ý: TOÀN BỘ HỆ THỐNG CỦA EVOGRAND/GRANDLITE ĐỀU SỬ DỤNG TỐC ĐÔ 250KBPS
================================================================================

I. GIỚI THIỆU DỰ ÁN
--------------------------------------------------------------------------------
Dự án nghiên cứu kỹ thuật đảo ngược (Reverse Engineering) và giải mã toàn bộ dữ liệu 
giao tiếp mạng CAN Bus tốc độ 250kbps trên dòng xe máy điện VinFast Evo Grand 
(khối pin LFP 2.4kWh - hệ 19S).

Bộ công cụ cung cấp giải pháp trọn gói:
1. Tài liệu giải mã chi tiết từng Byte và mã định danh CAN (CAN ID Protocol Map).
2. Công cụ ghi nhận dữ liệu (CAN Loggers) cho PC và Điện thoại.
3. Firmware mô phỏng phần cứng (Hardware Simulator) phát dữ liệu kiểm thử độc lập.
4. Giao diện Web Dashboard thời gian thực trên nền ESP32-S3 (Sport Cluster 240°,
   giám sát 19 cell pin, đồng bộ giờ HMI qua frame 0x182, 3 biểu đồ nạp xả/tốc độ).

--------------------------------------------------------------------------------
II. YÊU CẦU PHẦN CỨNG & SƠ ĐỒ ĐẤU DÂY (SN65HVD230)
--------------------------------------------------------------------------------
1. Thiết bị phần cứng:
   - Vi điều khiển: ESP32-S3 (Khuyên dùng bản có PSRAM như N16R8, N8R2).
   - Module thu phát CAN: SN65HVD230 (Hoạt động ở mức logic 3.3V, cắm trực tiếp 
     vào chân GPIO của ESP32-S3 mà không cần mạch chuyển mức tín hiệu).
   - Module nguồn hạ áp DC-DC cách ly tốt (hạ áp từ 12V/72V trên xe xuống 5V/3.3V).

2. Bảng chân đấu nối:
   +---------------+--------------------+-----------------+-----------------------+
   | Chân ESP32-S3 | Chân SN65HVD230    | Hướng tín hiệu  | Chức năng             |
   +---------------+--------------------+-----------------+-----------------------+
   | 3V3           | 3V3 (VCC)          | ESP32 -> Module | Nguồn nuôi 3.3V       |
   | GND           | GND                | Chung           | Mass hệ thống / sườn  |
   | GPIO 17       | CTX / TXD          | ESP32 -> Module | CAN Transmit (TWAI)   |
   | GPIO 18       | CRX / RXD          | Module -> ESP32 | CAN Receive (TWAI)    |
   | ---           | CAN_H              | Module -> Xe    | CAN High              |
   | ---           | CAN_L              | Module -> Xe    | CAN Low               |
   +---------------+--------------------+-----------------+-----------------------+

3. Sơ đồ khối đấu nối:

  +--------------------+             +-------------------+             +-----------------------+
  |     ESP32-S3       |             |    SN65HVD230     |             |    GIẮC CAN TRÊN XE   |
  |                    |             |   (Logic 3.3V)    |             |  (VinFast Evo Grand)  |
  |       3.3V [Pin] --+------------>+ 3V3               |             |                       |
  |        GND [Pin] --+------------>+ GND               |             |                       |
  |    GPIO 17 [Pin] --+------------>+ CTX / TXD         |             |                       |
  |    GPIO 18 [Pin] <-------------+ CRX / RXD         	 |             |                       |
  |                    |             |            CAN_H +------------> [Dây CAN High trên xe]  |
  |                    |             |            CAN_L +------------> [Dây CAN Low trên xe]   |
  |        GND [Pin] --+---------------------------------------------> [CAN_GND / Sườn xe]     |
  +--------------------+             +-------------------+             +-----------------------+

* Lưu ý kỹ thuật:
- Trở đầu cuối: Xe đã tích hợp sẵn trở phân mạng CAN. Nếu đấu song song mạch vào xe,
  hãy gỡ hoặc ngắt jumper trở dán 120 ohm trên module SN65HVD230 để tránh làm giảm 
  trở kháng toàn mạng xuống dưới 60 ohm.
- Chân Rs trên module SN65HVD230 cần được nối xuống GND để chạy chế độ tốc độ cao.
- Cặp dây CAN_H và CAN_L cần được xoắn đôi để chống nhiễu xung điện từ cuộn dây động cơ.

--------------------------------------------------------------------------------
III. BẢNG MAP GIAO THỨC CAN BUS (250KBPS, 11-BIT STANDARD ID)
--------------------------------------------------------------------------------

1. NHÓM ĐIỀU KHIỂN NGUỒN & HỆ THỐNG (VCU / SMARTKEY)
[ID: 0x101] - DLC 3
  * Byte 0: Trạng thái nguồn chính (0x11 = Key ON | 0x00 = Key OFF)
  * Byte 1: Trạng thái phụ ACC (0x10)

[ID: 0x102] - DLC 8
  * Byte 0: Trạng thái P & Chế độ lái:
    + Dải 0x20: Đang khóa P (P ON)
    + Dải 0x30: Đã nhả P (Ready)
    + Số chẵn (0x20, 0x30): Chế độ ECO
    + Số lẻ (0x21, 0x31): Chế độ SPORT
    -> Code: isPark = ((D0 & 0xF0) == 0x20); isSport = ((D0 & 0x01) == 0x01);
  * Byte 1: Cảm biến chân chống nghiêng (0x00 = Đá lên | 0x10 = Gạt xuống ngắt ga)
  * Byte 7: Rolling Counter tăng tuần hoàn theo thời gian

2. NHÓM ĐỘNG CƠ & VẬN HÀNH (MCU / ODO)
[ID: 0x201] - DLC 8
  * Byte 0..1: Tốc độ vòng tua động cơ (RPM) -> (D0 << 8) | D1
    -> Tốc độ xe quy đổi: Km/h = RPM / 10.0f
  * Byte 3..5: ODO nội bộ MCU -> ((D3 << 16) | (D4 << 8) | D5) / 10.0f (Km)
  * Byte 7: Cảm biến phanh điện (0x00 = Nhả phanh | 0x10 = Đang bóp phanh Regen)

[ID: 0x208] - DLC 8
  * Byte 1: Dòng pha / Lực kéo tức thời (Phase Current) - Kiểu Signed 8-bit bù 2:
    + Số dương (> 0): Kéo ga có tải sinh công
    + Số âm (< 0): Hãm phanh điện nạp ngược (Regen)
  * Byte 3: Nhiệt độ MCU / Cuộn dây stator (°C)
  * Byte 4..7: Cờ mã lỗi phần cứng MCU (Bình thường: 00 00 00 00)

[ID: 0x400] - DLC 8
  * Byte 5..7: ODO tổng hiển thị trên đồng hồ HMI -> ((D5 << 16) | (D6 << 8) | D7) / 10.0f (Km)
  * Quãng đường chuyến đi (Trip): Tính bằng ODO hiện tại trừ ODO lúc bật khóa

3. NHÓM QUẢN LÝ PIN LFP 2.4KWH (BMS)
[ID: 0x300] - DLC 8
  * Byte 0..7: Mã định danh phần cứng (Serial) khối pin VinFast LFP

[ID: 0x303] - DLC 8
  * Byte 0: BMS Heartbeat Rolling Counter

[ID: 0x309] - DLC 8
  * Byte 2..3: Điện áp tổng Pack Pin (Pack Voltage) -> ((D2 << 8) | D3) * 0.002f (V)
  * Byte 6..7: Dòng điện tổng Pack Pin (Signed 16-bit) -> rawI * 0.05f (A)
    + Số âm: Dòng xả tải
    + Số dương: Dòng nạp / dòng phanh Regen

[ID: 0x30A] - DLC 8
  * Byte 0 & Byte 3: Sức khỏe pin SOH (100 = 100%)
  * Byte 1 & Byte 2: Phần trăm pin thực tế SOC (%)
  * Byte 4..5: Số chu kỳ sạc (Cycle Count) -> (D4 << 8) | D5

[ID: 0x320] - DLC 5
  * Byte 0: Nhiệt độ trung bình các cell (°C)
  * Byte 3: Nhiệt độ Pin chuẩn hiển thị lên HMI (°C)

[ID: 0x328] - DLC 2: Cảm biến nhiệt độ T1 (°C), T2 (°C)
[ID: 0x329] - DLC 2: Cảm biến nhiệt độ T3 (°C), T4 (°C)

4. NHÓM ĐIỆN ÁP TỪNG CELL PIN LFP (HỆ 19S)
Công thức giải mã: Cell_V = ((Dx << 8) | Dy) / 10000.0f (Volt)

[ID: 0x310] - DLC 8: Tóm tắt thông số Cell:
  * Byte 0..1: Điện áp trung bình pack (avgCellV)
  * Byte 2..3: Điện áp cell thấp nhất (minCellV)
  * Byte 4:    Vị trí thứ tự cell thấp nhất (1 - 19)
  * Byte 5..6: Điện áp cell cao nhất (maxCellV)
  * Byte 7:    Vị trí thứ tự cell cao nhất (1 - 19)

[ID: 0x311] - DLC 8: Cell 01, Cell 02, Cell 03, Cell 04
[ID: 0x312] - DLC 8: Cell 05, Cell 06, Cell 07, Cell 08
[ID: 0x313] - DLC 8: Cell 09, Cell 10, Cell 11, Cell 12
[ID: 0x314] - DLC 8: Cell 13, Cell 14, Cell 15, Cell 16
[ID: 0x31A] - DLC 8: Cell 17, Cell 18, Cell 19 (Byte 0..5), Byte 6..7 cố định 00 00
[ID: 0x31B] - DLC 8: Blank frame (00 00 00 00 00 00 00 00)

--------------------------------------------------------------------------------
IV. CẤU TRÚC THƯ MỤC REPOSITORY (PROJECT STRUCTURE)
--------------------------------------------------------------------------------
Read-CanBus-EvoGrand/
├── EvoGrand_CAN_Loggers/
│   ├── Mobile_Web_Logger/               # Firmware ghi log qua Web AP điện thoại (CSV 100KB)
│   └── PC_Serial_Logger/                # Firmware dump dữ liệu CAN tốc độ cao qua cổng Serial PC
├── EvoGrand_CAN_Hardware_Simulator/     # Firmware mô phỏng toàn bộ tín hiệu xe ra CAN vật lý
├── LogCan/                              # Các tệp log CAN thực tế thu thập từ xe
├── images/                              # Biểu đồ phân tích và giao diện Dashboard
├── Định Nghĩa CanBus EvoGrand.txt 	 # Bản đồ tra cứu CAN Bus chi tiết
└── README.txt                           # Tài liệu hướng dẫn dự án

--------------------------------------------------------------------------------
V. HƯỚNG DẪN KIỂM THỬ & SỬ DỤNG
--------------------------------------------------------------------------------
1. Kiểm thử trên bàn làm việc bằng Hardware Simulator:
   - Nạp EvoGrand_CAN_Hardware_Simulator.ino vào ESP32 thứ nhất (Node A - ECU giả lập).
   - Nạp firmware Web Dashboard vào ESP32 thứ hai (Node B - Receiver).
   - Đấu chéo CAN_H, CAN_L giữa 2 module SN65HVD230 (đấu chung GND). Node A sẽ liên tục
     phát dữ liệu động cơ, ODO lăn bánh, pin và dòng tải mô phỏng để Node B hiển thị.

--------------------------------------------------------------------------------
VI. BẢN QUYỀN & TUYÊN BỐ MIỄN TRỪ TRÁCH NHIỆM
--------------------------------------------------------------------------------
- Tác giả: DevByNVP (2026).
- Mục đích: Dự án phục vụ nghiên cứu khoa học, giáo dục kỹ thuật điện tử và thúc đẩy
  cộng đồng xe điện mã nguồn mở.
- Tuyên bố miễn trừ: Tác giả không chịu trách nhiệm đối với bất kỳ hư hỏng phần cứng,
  chập cháy thiết bị hoặc sự cố an toàn nào phát sinh từ việc can thiệp vào hệ thống
  điện của phương tiện mà không tuân thủ các quy tắc an toàn kỹ thuật.
================================================================================
