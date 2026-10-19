# pes2027-mod — eFootball iOS 11.1.1 (Offline & Online Mod)

Dự án mod và port logic máy chủ offline cho eFootball iOS phiên bản **11.1.1 (build 11.1.101)** dựa trên kiến trúc bản Android *(Efootball 2027 Offline Full Epic – Bigtime by Ziru)*, bổ sung tính năng thi đấu Online Friend Match (1v1) qua máy chủ Relay độc lập.

Kho lưu trữ này **chỉ chứa mã nguồn** (C++ core, Objective-C++ dylib, công cụ nhị phân Mach-O, server signaling Python). **Không chứa tài nguyên có bản quyền** (IPA gốc của Konami, APK Android, mô hình 3D, khuôn mặt hay livedata). Toàn bộ dữ liệu được tổng hợp và đóng gói cục bộ thông qua script đi kèm.

---

## Tính năng nổi bật

- **Offline Server In-Process (C++17)**: Tái tạo toàn bộ logic của `com.offline.server.OfflineServer` (Ziru) bằng mã C++ hiệu năng cao, chạy trực tiếp trong tiến trình game với SQLite3 (`ziru_save.db`) và SharedPreferences giả lập (`almanac_state`).
- **Tách dữ liệu game (Data Decoupling)**: Vá hàm `basic::PathGet()` trong binary Mach-O để chuyển hướng đọc `pak/` và `cpk/` ra thư mục `Documents`. File cài đặt `PES2027.ipa` chỉ nặng ~180 MB, dữ liệu lớn (~4.6 GB) được nhập độc lập qua app Tệp (Files), không làm phình dung lượng app hay mất save khi nâng cấp.
- **Hiển thị tên cầu thủ trên lưng áo (Jersey Names)**: Vá trực tiếp chuỗi hàm render (`PlayerModelConfig::SetHasUniformName`, `SetupUniformConfig`, `APlayerActorMobile::SetUniformName`, `APlayerActor::SetUniformName`), ép Unreal Engine hiển thị đầy đủ tên cầu thủ phía trên số áo trong cả trận đấu và các màn ăn mừng.
- **Mở khóa toàn bộ sân vận động**: Cho phép tự do lựa chọn các sân vận động bản quyền (Santiago Bernabéu, Spotify Camp Nou, San Siro, Emirates, Stamford Bridge, v.v.) trong mọi chế độ chơi (Giao hữu, Tour Event, Authentic Match).
- **Việt hóa giao diện & Bình luận**: Nạp trực tiếp bộ font OpenType (`eFootballSans`, `eFootballStencil`) vào hệ thống Slate UI của Unreal Engine, hiển thị tiếng Việt có dấu chuẩn xác; hỗ trợ tích hợp gói bình luận tiếng Việt (dt510 CPK).
- **Hệ thống thi đấu AI & myClub đầy đủ**:
  - Thi đấu Rank Division từ 10 lên 1 với cơ chế thăng hạng ngay khi đủ mốc điểm.
  - Tự động đồng bộ điểm kỹ năng (Progression Points), bảo toàn trọn vẹn kỹ năng đặc biệt (Showtime, Fortress, Momentum Dribbling...) và kỹ năng gốc qua các lần khởi động lại.
  - Tinh chỉnh chuẩn xác chỉ số, độ thành thạo vị trí và khuôn mặt thật của các cầu thủ cập nhật (Çalhanoğlu, Mkhitaryan, Cristiano Ronaldo, v.v.).
- **Chế độ Online Friend Match (Mod v1.1)**: Tích hợp máy chủ Signaling API (HTTPS) và STUN/TURN Coturn relay, cho phép 2 thiết bị iOS tạo phòng bằng mã 6 số và thi đấu giao hữu qua mạng Internet với độ trễ thấp (P2P NAT Punching / UDP Relay).

---

## Cấu trúc mã nguồn

| Thư mục / Tệp | Vai trò |
| --- | --- |
| `core/` | Nhân server C++: 180+ endpoint xử lý myClub, Division, MyLeague, chiêu mộ, huấn luyện, giao dịch SQLite và bộ mã hóa giải mã `AES-256-CBC + zlib/gzip + msgpack`. |
| `ios/` | Mã nguồn `ZiruOffline.dylib`: Đánh chặn mạng qua `NSURLProtocol` (chuyển hướng request nội bộ sang loopback `127.0.0.1:8085`), nạp font chữ tiếng Việt và giao diện nhập gói data trước khi Unreal Engine 4 khởi động. |
| `tools/build_ipa.py` | Công cụ tự động hóa: vá binary Mach-O (ARM64), tiêm `LC_LOAD_DYLIB`, tích hợp livedata, font chữ và đóng gói file `PES2027.ipa` hoàn chỉnh. |
| `online/` | Mã nguồn máy chủ giao hữu: Signaling API (`pes_online_server.py`), kịch bản triển khai tự động lên VPS (`setup_ec2.sh`) và bộ kiểm thử giao thức mã hóa hai client. |
| `ziru-extra/` | Các phản hồi JSON bổ sung cho client iOS 11.1.1 (xử lý EULA, sự kiện, gói sân vận động). |
| `test/` | Bộ kiểm thử hồi quy: đối chiếu dữ liệu C++ với JVM bytecode gốc của Ziru, kiểm tra bảng dữ liệu native iOS và tính toàn vẹn gói data. |

---

## Chi tiết kỹ thuật & Binary Patches

### Cơ chế giao thức mã hóa
Client eFootball iOS 11.1.1 giải khóa AES trong hàm `onlinesystem::ServerDef::GetAESKey()` (tại địa chỉ ảo `0x106A62340`):
$$\text{key}[i] = d[i] \oplus A[i \pmod 9] \oplus B[i \pmod{12}] \oplus \text{0x75}$$
Khóa giải mã hoàn toàn trùng khớp với `KEY_HEX` của máy chủ offline Ziru:
- **Request từ game:** `IV (16 bytes) ‖ AES-256-CBC(zlib(msgpack))`
- **Response trả về:** `IV (16 bytes) ‖ AES-256-CBC(gzip(msgpack))`

### Bảng các điểm vá nhị phân (Mach-O ARM64)
Toàn bộ các bản vá được thực thi tự động bởi [`tools/build_ipa.py`](tools/build_ipa.py) trên file thực thi `PESmobile`:

| Địa chỉ ảo (VA) | Lệnh gốc (Disassembly) | Lệnh sau vá (Patched) | Mục đích |
| --- | --- | --- | --- |
| `0x1000155BC` | `str x20, [sp, #0x18]` | `str x21, [sp, #0x18]` | Chuyển hướng đường dẫn CPK sang Documents |
| `0x10001561C` | `str x20, [sp, #0x18]` | `str x21, [sp, #0x18]` | Chuyển hướng đường dẫn PAK sang Documents |
| `0x1046FE0B0` | `cbz x0, 0x1046fe0b4` | `b 0x1046fe0b4` | Tránh crash `AddStadiumDlTask::Bind` khi kích thước rỗng |
| `0x1008F2FA8` | `cmp w1, #0` | `mov w8, #2` | Ép `PlayerModelConfig::SetHasUniformName` luôn bật |
| `0x10162331C` | `cset w1, ne` | `mov w1, #1` | Kích hoạt `SetupUniformConfig` mang tên lưng áo |
| `0x10089F4A4` | `ldr w8, [x19, #0xe38]` | `mov w8, #2` | Kích hoạt render font tên áo trong `BackNumberStep` |
| `0x100895F44` | `ldrb w8, [x19, #0xf10]` | `mov w8, #1` | Bỏ qua cờ ẩn tên áo trong `APlayerActorMobile` |
| `0x100896064` | `b.eq 0x100895f54` | `b 0x100895f54` | Ép vẽ tên cầu thủ ngay từ frame khởi tạo đầu |
| `0x100911418` | `ldrb w10, [x19, #0xf10]`| `mov w10, #1` | Kích hoạt cờ tên áo trong `APlayerActor` |
| Mach-O Header | — | `LC_LOAD_DYLIB` | Nạp `Frameworks/ZiruOffline.dylib` khi game mở |

---

## Hướng dẫn đóng gói IPA (Build Instructions)

### 1. Chuẩn bị nguyên liệu
1. **File ứng dụng iOS gốc đã giải mã (Decrypted IPA / `PESmobile.app`)**: Phiên bản eFootball 11.1.1 (build 11.1.101), trích xuất bằng `frida-ios-dump`, `IAPDump` hoặc tải từ các nguồn decrypt uy tín.
2. **File APK Android của Ziru**: Dùng để trích xuất kho dữ liệu `assets/` (database cầu thủ, sự kiện, mô hình đội bóng).
3. **Thư viện `ZiruOffline.dylib`**: Biên dịch trực tiếp qua macOS hoặc tải file nhị phân mới nhất từ tab **Actions** / **Releases** của repository.
4. **Python 3.9+** trên máy tính (Windows / macOS / Linux).

### 2. Thực hiện đóng gói
Chạy lệnh đóng gói thông qua công cụ `tools/build_ipa.py`:

```bash
# Ví dụ trên Windows PowerShell:
python tools/build_ipa.py `
  --app "path/to/Payload/PESmobile.app" `
  --dylib "path/to/ZiruOffline.dylib" `
  --apk "path/to/Ziru_Offline.apk" `
  --extra-pak "path/to/pak_extra" `
  --native-livedata "path/to/native-livedata" `
  --out "D:/PES2027" `
  --skip-data
```

*Các tùy chọn hữu ích:*
- `--skip-data`: Chỉ đóng gói file `PES2027.ipa` (dành cho các bản cập nhật mã nguồn/dylib mà không cần nén lại 4.6 GB data).
- `--commentary "path/to/dt510.cpk"`: Tự động đưa gói bình luận tiếng Việt vào game.
- `--online-config "path/to/client.json"`: Nhúng cấu hình máy chủ thi đấu Online Friend Match vào app.

---

## Hướng dẫn cài đặt lên thiết bị iOS

1. **Cài đặt file IPA**:
   - **TrollStore (Khuyên dùng)**: Mở trực tiếp file `PES2027.ipa` và chọn Install. App chạy với đầy đủ quyền hạn, không bị thu hồi chứng chỉ và không giới hạn 7 ngày.
   - **ESign / Feather / Sideloadly / AltStore**: Ký file IPA bằng chứng chỉ cá nhân hoặc chứng chỉ doanh nghiệp và cài đặt lên máy.
2. **Nhập gói dữ liệu (Data Pack)**:
   - Chép file dữ liệu `PES2027-data-<data_id>.zip` vào ứng dụng **Tệp (Files)** trên iPhone (thư mục *Trên iPhone › PES 2027* hoặc vị trí bất kỳ).
   - Mở game lần đầu → Màn hình **Nhập dữ liệu game** của mod xuất hiện → Nhấn chọn gói ZIP vừa chép.
   - Ứng dụng sẽ tự động giải nén, kiểm tra tính toàn vẹn (SHA-256) của từng file và khởi tạo môi trường offline.
3. **Xem file nhật ký khi gặp sự cố**:
   - Toàn bộ log vận hành được ghi tại: *Tệp › Trên iPhone › PES 2027 › `PES2027-logs/ziru.log`* và `offline_server.log`.

---

## Kiểm thử & Phát triển (Testing)

Dự án tích hợp sẵn bộ test đầy đủ để xác minh tính tương thích trước khi phát hành:

```bash
# 1. Biên dịch bộ công cụ test trên Linux / macOS:
make -j2 build/runcpp build/packtest build/gameplaytest

# 2. Chạy hồi quy kiểm thử logic gameplay (division, career, progression, skills):
./build/gameplaytest

# 3. Kiểm thử chuyển đổi bảng dữ liệu nhị phân iOS:
python3 -m unittest discover -s test -p 'ios_tables_test.py' -v

# 4. Kiểm thử giao thức mã hóa máy chủ Online (Signaling + TURN):
python3 -m unittest discover -s online -p 'test_online_server.py' -v
```

---

## Giấy phép & Lời cảm ơn (Credits & Disclaimer)

- **Ziru**: Tác giả nguyên bản của bản mod Android *Efootball 2027 Offline Full Epic – Bigtime*, người đã đảo ngược và xây dựng bộ API offline server gốc trên nền tảng Java.
- **Dự án mã nguồn mở**: Sử dụng các thư viện `sqlite3`, `zlib`, `msgpack-c`, và `coturn`.
- **Miễn trừ trách nhiệm**: Dự án này được phát triển hoàn toàn vì mục đích học tập, nghiên cứu kỹ thuật đảo ngược (reverse engineering) và bảo tồn trò chơi. Tất cả nhãn hiệu, bản quyền hình ảnh, âm thanh và thương hiệu eFootball thuộc về **Konami Digital Entertainment**. Vui lòng không sử dụng mã nguồn này vào mục đích thương mại.
