# Khóa Cửa Nhận Diện Khuôn Mặt ESP32-S3

Đây là dự án dùng ESP32-S3 và Camera để làm khóa cửa thông minh. Hệ thống tự động nhận diện khuôn mặt và điều khiển Relay để mở cửa.

## 1. Cách kết nối dây (Relay)
- Chân VCC của Relay -> Nối vào 5V trên ESP32
- Chân GND của Relay -> Nối vào GND trên ESP32
- Chân IN (Tín hiệu) -> Nối vào chân D2 (GPIO 3) trên ESP32

## 2. Cách chạy dự án
1. Mở file `S3_Face_Recognition_V2.ino` bằng phần mềm Arduino IDE.
2. Sửa tên WiFi và Mật khẩu ở dòng 36-37 trong code cho đúng với WiFi nhà bạn.
3. Chọn mạch là "XIAO_ESP32S3" hoặc "ESP32S3 Dev Module".
4. Nhấn nút Mũi tên (Upload) để nạp code vào chip.
5. Sau khi nạp xong, ESP32 sẽ tự tạo một tên miền nội bộ.
6. Dùng điện thoại hoặc máy tính truy cập vào địa chỉ: **http://gatekeeper.local**
   *(Nếu không vào được bằng tên miền, bạn mới cần mở Serial Monitor để lấy địa chỉ IP số dạng 192.168.1.x)*

## 3. Cách sử dụng trên Web
- **Xem Camera**: Hình ảnh sẽ hiện trực tiếp trên trang web.
- **Thêm người mới (Enroll)**: Nhấn nút "ENROLL", nhập tên bạn vào, đứng trước camera rồi nhấn OK. Máy sẽ lưu mặt bạn ngay lập tức.
- **Mở cửa (Scan Face)**: Đứng trước camera và nhấn "SCAN FACE". 
  - Nếu đúng chủ nhà: Relay sẽ bật để mở cửa trong 5 giây rồi tự tắt.
  - Có hiện chỉ số J (Độ rung) và V (Độ biến thiên) để chống dùng ảnh chụp để lừa máy.
- **Xóa dữ liệu**: Nhấn "CLEAR DATA" nếu muốn xóa hết các mặt đã lưu.

## 4. Lưu ý về an toàn
Máy có tính năng chống ảnh tĩnh. Nếu bạn đưa một tấm ảnh lên, máy sẽ báo "SPOOF DETECTED" và không mở cửa. Bạn phải là người thật đứng trước máy thì cửa mới mở.

---
Chúc bạn sử dụng vui vẻ!
