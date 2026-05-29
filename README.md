# Khóa Cửa Nhận Diện Khuôn Mặt ESP32-S3 (V27 - TFT Edition)

Dự án sử dụng ESP32-S3 và Camera OV2640 để nhận diện khuôn mặt, tích hợp chống giả mạo ảnh tĩnh và hiển thị trạng thái lên màn hình ST7735.

## 1. Sơ đồ kết nối phần cứng

### 1.1. Module Relay (Điều khiển khóa)
- **VCC** -> 5V (ESP32)
- **GND** -> GND
- **IN** -> **GPIO 3 (D2)**

### 1.2. Màn hình TFT ST7735
- **VCC** -> 3.3V hoặc 5V
- **GND** -> GND
- **SCK** -> **GPIO 42**
- **SDA (MOSI)** -> **GPIO 41**
- **RES (RESET)** -> **GPIO 40**
- **RS (DC)** -> **GPIO 39**
- **CS** -> **GPIO 38**

## 2. Cách cài đặt và nạp code
1. Cài đặt thư viện: `Adafruit GFX`, `Adafruit ST7735`.
2. Mở file `S3_Face_Recognition_V2.ino`.
3. Cấu hình WiFi (dòng 49-50).
4. Chọn mạch: **XIAO_ESP32S3** hoặc **ESP32S3 Dev Module**.
5. Nhấn **Upload**.

## 3. Cách sử dụng
- Truy cập **http://gatekeeper.local** để quản lý.
- **SCAN FACE**: Bắt đầu quét khuôn mặt (Hệ thống sẽ kiểm tra thực thể sống trước khi so khớp).
- **ENROLL**: Nạp khuôn mặt mới vào bộ nhớ.
- **CLEAR DATA**: Xóa toàn bộ khuôn mặt đã nạp (Khuyên dùng khi nâng cấp thuật toán nhận diện).

## 4. Lưu ý về Độ chính xác
- Khoảng cách tối ưu: **40cm - 60cm**.
- Nếu nhận diện kém sau khi cập nhật code, hãy dùng chức năng **CLEAR DATA** và đăng ký lại khuôn mặt để đồng bộ hóa vùng cắt ảnh (Crop) mới.
- Chỉ số **J (Jitter)** và **V (Variance)** hiển thị trên màn hình giúp theo dõi khả năng chống giả mạo.

---

