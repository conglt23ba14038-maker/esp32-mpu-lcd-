# Liquid Dots — ESP32 + MPU6050 + LCD1602

Dự án VS Code + PlatformIO cho phần cứng trong ảnh: ESP32, MPU6050/GY-521, LCD16x2 qua backpack I2C PCF8574.

MPU cân bằng → hàng chấm ngang ở hàng dưới LCD. Nghiêng MPU → hàng chấm đổi độ dốc để mô phỏng đường mặt chất lỏng. Góc được lọc kết hợp gyro và gia tốc. Sóng mở rộng có thể bật sau, mặc định tắt để test cơ chế cơ bản trước.




## Cấu trúc code

```text
platformio.ini            board, framework, môi trường test
include/Config.h          chân, trục nghiêng, hướng, độ nhạy, bật/tắt sóng
include/LiquidMath.h      giải mã signed, ánh xạ góc thành vị trí chấm
src/main.cpp              driver tự viết, hiệu chỉnh, lọc, vòng lặp

```

## Cách chương trình hoạt động

1. Quét I2C để hỗ trợ tìm lỗi. Dò MPU ở 0x68/0x69, in WHO_AM_I. ID chuẩn là 0x68; module đang cắm trả 0x72 nên cấu hình riêng `MPU_COMPAT_WHO_AM_I=0x72` trong Config.h. Driver chỉ chấp nhận ID chuẩn hoặc ID tương thích đã khai báo, rồi đọc lại các thanh ghi cấu hình để kiểm tra.
2. Khởi tạo MPU qua các thanh ghi: PWR_MGMT_1, CONFIG, SMPLRT_DIV, GYRO_CONFIG, ACCEL_CONFIG. Chọn 100Hz, accel ±4g, gyro ±500°/s.
3. Đọc burst 14 byte từ 0x3B; đổi signed 16-bit sang g và rad/s. Hiệu chỉnh gyro và mốc góc khi đứng yên.
4. Bộ lọc bổ sung kết hợp góc từ gia tốc và tích phân gyro; giảm ảnh hưởng gia tốc khi có rung mạnh.
5. LCD: truyền nibble 4 bit qua PCF8574, tạo xung Enable, nạp 8 glyph vào CGRAM, cập nhật ô thay đổi.
6. LiquidMath ánh xạ góc nghiêng thành 16 vị trí chấm, xóa frame cũ và giới hạn trong màn hình.

Hai hàng LCD tạo 16 mức theo chiều dọc nhưng có khe hở vật lý giữa các ô/hàng. Đồ họa sẽ là chấm nhỏ, không thể tròn/mượt như hình minh họa hoặc OLED. Đây là mô hình hiển thị góc nghiêng, không đo lượng nước thực tế hay mô phỏng thủy động lực học.

## Chỉnh tham số

Chỉnh include/Config.h rồi Build/Upload lại:

| Tham số | Mặc định | Ý nghĩa |
|---|---|---|
| TILT_AXIS | 0 | 0: roll quanh X; 1: pitch quanh Y |
| DIRECTION | 1 | Đổi -1 nếu đường nghiêng ngược mong muốn |
| REST_Y | 11 | Tọa độ chấm khi cân bằng; 0 là trên cùng, 15 dưới cùng |
| SLOPE_GAIN | 0.55 | Độ nhạy độ dốc |
| ENABLE_WAVE | false | true để thêm sóng tắt dần |
| SDA_PIN/SCL_PIN | 21/22 | GPIO I2C |
| MPU_COMPAT_WHO_AM_I | 0x72 | ID khác chuẩn của module đã thử thực tế; đặt 0 để chỉ nhận MPU6050 có ID 0x68 |
