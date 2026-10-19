# PES2027 online (mod 1.1) – Friend Match qua server AWS

Bản 1.1 là bản thử nghiệm cho phòng Friend Match giữa hai iPhone, chưa xác nhận trận P2P/TURN chạy hoàn chỉnh trên thiết bị. myClub tiếp tục chạy offline trên từng máy.
Chỉ các lệnh online đi lên server này:
- phòng: tạo, vào, thông tin phòng, sẵn sàng, chọn bên, rời phòng;
- phiên trận: game session, TURN, tiến trình trận, kết quả.

## Cách hoạt động
- **Dylib** (`ios/ZiruOffline.mm`) đọc `PESmobile.app/ziru/online.json`. Người chơi có thể ghi đè bằng `Documents/PES2027-online.json`:
  `{"server": "https://IP:8443", "name": "Tên hiển thị", "enabled": true, "token": "PRIVATE_DEPLOYMENT_TOKEN", "cert_sha256": "CERTIFICATE_SHA256"}`.
  Các lệnh online được chuyển tới `server + /pes22/gate/gate_<CMD>.php`, giữ nguyên mã hóa của game, kèm UID, tên, platform IOS và token riêng của deployment. Chứng chỉ tự ký phải khớp SHA-256 đã cấu hình; không chấp nhận mọi chứng chỉ của cùng host.
- **Định danh:** mọi máy đều dùng tài khoản Ziru (user_id 359042717). Server làm việc với id riêng của từng máy. Trong mọi phản hồi, server đổi id của chính người gọi về 359042717 để game nhận ra mình.
- **Trận đấu:** chạy P2P (`game_sycom = P2P`) bằng địa chỉ NTL mà hai máy gửi lên khi tạo, vào phòng hoặc khi bấm sẵn sàng. Nếu P2P không được, game dùng TURN (coturn trên cùng máy EC2), với thông tin đăng nhập tạm thời do server cấp qua `CMD_GET_TURN_SERVER_LIST`.
- **gRPC:** tắt (`connect_grpc_task.disable = true` trong server env của Ziru). Phòng được cập nhật bằng cách hỏi định kỳ `CMD_GET_ROOM_INFO` mỗi 5 giây.

## Triển khai máy chủ (VPS Ubuntu / Amazon Linux)
1. Sử dụng VPS Linux (Ubuntu 22.04/24.04 hoặc Amazon Linux 2023) có 1 IP Public tĩnh.
2. Mở các cổng mạng (Firewall / Security Group):

   | Giao thức | Cổng | Mục đích |
   |---|---|---|
   | TCP | 8443 | Signaling API (HTTPS) |
   | TCP + UDP | 3478 | STUN / TURN Relay |
   | TCP | 5349 | TURN over TLS |
   | UDP | 49160-49259 | Media Relay Port Range |

3. Copy thư mục `online/` lên server và chạy script thiết lập tự động:
   ```bash
   sudo bash setup_ec2.sh <PUBLIC_IP>
   ```
4. Tải file cấu hình client `/etc/pes2027-online/client.json` về máy local (không commit file này lên Git).
   Đóng gói IPA cùng file cấu hình:
   ```bash
   python tools/build_ipa.py --online-config path/to/client.json ...
   ```
5. Kiểm tra trạng thái máy chủ:
   ```bash
   curl -k https://<PUBLIC_IP>:8443/health
   ```

## Kiểm thử kết nối phòng
Hai iPhone cài cùng bản build IPA, kết nối mạng:
1. Thiết bị A vào mục Giao hữu (Friend Match), tạo phòng và nhận mã phòng gồm 6 chữ số.
2. Thiết bị B nhập mã phòng để tham gia.
3. Cả hai thiết bị bấm "Sẵn sàng" để bắt đầu trận đấu.

## Giám sát & Quản trị hệ thống
- Quản trị viên kiểm tra trạng thái máy chủ qua Header `Authorization: Bearer <PES2027_ADMIN_TOKEN>` (token được lưu trong file `/etc/pes2027-online/server.env`).
- Dịch vụ chạy dưới user cách ly `pes2027` với giới hạn tài nguyên an toàn (Signaling API ~192MB RAM, Coturn ~160MB RAM).

Chạy kiểm tra mã hóa HTTP với hai client mô phỏng: `python -m unittest discover -s online -p test_online_server.py -v`. Các kiểm tra này bao gồm UID remap, phòng/mật khẩu, sẵn sàng hai phía, phiên trận, trao đổi TURN, kết quả và phân quyền phòng. Chúng chưa chứng minh game native bắt tay và chơi được trên hai iPhone.

Kiểm tra deployment thật từ PC: `python online/smoke_online.py --config PRIVATE_CLIENT_JSON --output smoke-result.json`. Công cụ kiểm chứng certificate pin, luồng phòng hai client HTTPS và cấp/thu hồi TURN allocation UDP bằng thông tin xác thực từ API. Không in token hoặc mật khẩu TURN. Kết quả có `native_iphone_match_tested: false` để phân biệt với trận thật.
