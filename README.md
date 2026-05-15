# 📸 ESP32-S3 Face Recognition System (N16R8 Optimized)

Hệ thống nhận diện khuôn mặt thời gian thực sử dụng ESP32-S3 và Camera OV2640/OV5640, tích hợp giao diện Web hiện đại và bộ công cụ huấn luyện AI bằng Python.

---

## 🛠 Yêu cầu Kỹ thuật

### 1. Cấu hình Mạch (Arduino IDE)
Để hệ thống chạy ổn định nhất trên ESP32-S3, bạn **bắt buộc** phải cài đặt như sau:
- **Board**: `ESP32S3 Dev Module`
- **USB CDC On Boot**: `Enabled` (để xem Serial qua cổng USB)
- **Flash Size**: `16MB` (hoặc tùy board của bạn)
- **PSRAM**: `OPI PSRAM` (Cực kỳ quan trọng để xử lý ảnh)
- **Partition Scheme**: `16M Flash (3MB APP/9.9MB FATFS)`

### 2. Môi trường Python
Cài đặt các thư viện cần thiết:
```bash
pip install opencv-python numpy
```

---

## 🚀 Quy trình Vận hành 4 Bước

### Bước 1: Nạp Firmware
- Mở `esp32s3_face_recognition.ino`.
- Điền thông tin WiFi của bạn.
- Nhấn **Upload**. Sau khi nạp xong, mở Serial Monitor để lấy địa chỉ IP (Ví dụ: `192.168.1.190`).

### Bước 2: Chụp ảnh Khuôn mặt (`Face_Capture.py`)
Script này giúp bạn tạo bộ dữ liệu cho AI.
- **Cách dùng**: Chạy lệnh `python Face_Capture.py`.
- **Nhập ID & Tên**: 
    - Nếu bạn nhập ID mới: Script tạo thư mục mới.
    - Nếu bạn nhập ID cũ (ví dụ: `1`): Script sẽ tự động chụp bổ sung ảnh vào thư mục hiện có.
- **Mẹo**: Hãy xoay nhẹ đầu (lên, xuống, trái, phải) trong khi chụp để AI học được nhiều góc độ.

### Bước 3: Huấn luyện AI (`Train_Data.py`)
- Chạy lệnh: `python Train_Data.py`.
- Script sẽ tự động xử lý toàn bộ ảnh trong thư mục `dataset/`.
- Nó sẽ tạo ra file `faces_data.h` chứa "bộ não" đã được số hóa dưới dạng mảng byte.

### Bước 4: Nạp lại để Kích hoạt
- Quay lại Arduino IDE.
- Nhấn **Upload** một lần nữa. Lúc này ESP32 sẽ mang theo bộ dữ liệu khuôn mặt bạn vừa chụp để thực hiện nhận diện.

---

## 🖥 Giao diện Web (Web UI)
Truy cập IP của mạch qua trình duyệt. Các tính năng bao gồm:
- **Live Stream**: Xem hình ảnh trực tiếp từ camera.
- **Auto-Greeting**: Hiển thị tên người được nhận diện ngay trên màn hình (Ví dụ: `WELCOME: tranquangdat`).
- **Real-time Sync**: Kết quả nhận diện được cập nhật mỗi 1 giây thông qua API `/result`.

---

## 📂 Cấu trúc Thư mục
- `/dataset`: Nơi lưu trữ ảnh gốc của từng người (phân loại theo `ID_Ten`).
- `esp32s3_face_recognition.ino`: Mã nguồn chính cho ESP32.
- `faces_data.h`: File dữ liệu AI (được sinh ra tự động từ Bước 3).
- `Face_Capture.py`: Công cụ chụp ảnh.
- `Train_Data.py`: Công cụ huấn luyện.

## 💡 Lưu ý nâng cao
- **Độ chính xác**: Nếu AI nhận diện nhầm, hãy xóa bớt các ảnh mờ trong thư mục `dataset/` và chạy lại Bước 3 & 4.
- **Xoay ảnh**: Nếu ảnh trên Web bị ngược, hãy chỉnh lại `s->set_vflip(1)` hoặc `s->set_hmirror(1)` trong phần `setup()` của Arduino.
- **Đèn LED**: Đèn LED trên mạch sẽ chớp nhanh mỗi khi nhận diện đúng người để thông báo trạng thái.

---
*Hệ thống được tối ưu hóa cho tốc độ và độ ổn định trên dòng chip ESP32-S3.*
