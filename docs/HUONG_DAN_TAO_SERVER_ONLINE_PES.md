# HƯỚNG DẪN KỸ THUẬT TRIỂN KHAI MÁY CHỦ ONLINE DÀNH CHO PES2027 (eFootball Mobile)

> **Tài liệu lưu trữ nội bộ — Dành cho Quản trị viên & Kỹ thuật viên hệ thống**  
> *Phiên bản: 1.1.1*  
> *Mục đích: Thiết lập và tự vận hành cụm máy chủ Ghép trận (Signaling API) & Relay mạng (STUN/TURN Coturn) độc lập cho PES2027 Mobile.*

---

## 1. Tổng Quan Kiến Trúc Mạng (Architecture Overview)

Game eFootball Mobile thi đấu đối kháng thời gian thực giữa 2 thiết bị người chơi (P2P hoặc Relay). Để đảm bảo kết nối xuyên qua các lớp tường lửa mạng di động (4G/5G, CGNAT, Symmetric NAT), hệ thống Online bao gồm 2 thành phần máy chủ cốt lõi:

```
┌────────────────────────────────────────────────────────────────────────┐
│                        CLIENT A (iPhone/iOS)                           │
│  ZiruOffline.dylib • Pin SHA-256 Cert • P2P ICE & Matchmaking Engine   │
└───────────────────▲────────────────────────────────▲───────────────────┘
                    │                                │
    (1) HTTPS/TLS   │                (3) P2P Punching│ hoặc
    Signaling API   │                    UDP Relay   │ (khi bị Symmetric NAT)
    Cổng 8443       │                                │
┌───────────────────▼────────────────────────────────▼───────────────────┐
│                     MÁY CHỦ PES ONLINE (VPS / CLOUD)                   │
│                                                                        │
│  [1] PES Signaling API (Port 8443 HTTPS)                               │
│      • Tạo phòng 6 số ngẫu nhiên (/room/create)                        │
│      • Ghép cặp & Kiểm tra trạng thái sẵn sàng (/room/join, /ready)    │
│      • Cấp phát Dynamic HMAC Token cho Coturn (/turn/credentials)      │
│      • Khóa bảo mật: Certificate Pinning SHA-256 + Access Token        │
│                                                                        │
│  [2] Coturn STUN/TURN Relay (Ports 3478, 5349, UDP 49160 - 49259)      │
│      • STUN: Hỗ trợ xác định IP Public & Port Mapping                  │
│      • TURN: Chuyển tiếp gói tin UDP tốc độ cao khi P2P thất bại       │
│      • Giới hạn băng thông (QoS) & Quota chống Spam / DoS              │
└───────────────────▲────────────────────────────────▲───────────────────┘
                    │                                │
    (1) HTTPS/TLS   │                (3) P2P Punching│ hoặc
    Signaling API   │                    UDP Relay   │ (khi bị Symmetric NAT)
    Cổng 8443       │                                │
┌───────────────────▼────────────────────────────────▼───────────────────┐
│                        CLIENT B (iPhone/iOS)                           │
│  ZiruOffline.dylib • Pin SHA-256 Cert • P2P ICE & Matchmaking Engine   │
└────────────────────────────────────────────────────────────────────────┘
```

---

## 2. Yêu Cầu Phần Cứng & Môi Trường Máy Chủ

* **Hạ tầng khuyến nghị:** VPS Linux độc lập (AWS Lightsail, EC2, DigitalOcean, Linode, Vultr hoặc Hetzner).
* **Vị trí (Region):** Singapore hoặc Hong Kong (độ trễ Ping về Việt Nam dưới 35ms).
* **Cấu hình tối thiểu:**
  * CPU: 1 vCPU
  * RAM: 1 GB (hệ thống tiêu thụ chỉ ~25 MB RAM)
  * Ổ cứng: 10 GB SSD
  * IP: **1 IP Tĩnh (Static Public IPv4)** — *Bắt buộc*
* **Hệ điều hành tương thích:**
  * **Ubuntu 22.04 LTS / 24.04 LTS** (Khuyên dùng — cài đặt Coturn trực tiếp qua apt)
  * **Amazon Linux 2023** (Hỗ trợ qua script tự động biên dịch Coturn 4.18.0)

---

## 3. Cấu Hình Tường Lửa Mạng (Firewall Rules)

Mở chính xác các cổng sau trên bảng điều khiển Cloud (AWS Security Group / Firewall) và trên OS:

| Cổng (Port) | Giao thức | Dịch vụ | Mục đích sử dụng |
| :---: | :---: | :---: | :--- |
| **8443** | **TCP** | PES Online API | Giao tiếp phòng, mã hóa HTTPS, cấp TURN ticket |
| **3478** | **UDP & TCP** | Coturn STUN/TURN | Cổng nghe STUN/TURN mặc định |
| **5349** | **TCP** | Coturn TLS | Cổng nghe TURN qua TLS bảo mật |
| **49160 - 49259** | **UDP** | Dynamic Media Relay | Dải cổng UDP trung chuyển dữ liệu trận đấu giữa 2 máy |
| **22** | **TCP** | SSH | Quản trị máy chủ từ xa |

---

## 4. Hướng Dẫn Cài Đặt Tự Động (One-Command Setup)

### Bước 1: Đăng nhập SSH vào máy chủ VPS
```bash
ssh -i /path/to/key.pem ubuntu@<IP_PUBLIC_VPS>
```

### Bước 2: Tải mã nguồn server từ repo
```bash
sudo apt-get update && sudo apt-get install -y git
git clone https://github.com/iOSVNNews/pes2027-mod.git /tmp/pes2027-mod
cd /tmp/pes2027-mod/online
```

### Bước 3: Chạy script tự động thiết lập
Thay `<IP_PUBLIC_VPS>` bằng địa chỉ IPv4 công khai của máy chủ:
```bash
sudo bash setup_ec2.sh <IP_PUBLIC_VPS>
```

> **Script sẽ tự động thực hiện toàn bộ:**
> 1. Tạo người dùng hệ thống cô lập `pes2027` (không có quyền login shell).
> 2. Cài đặt Python Virtualenv và cài thư viện `fastapi`, `uvicorn`, `cryptography`.
> 3. Tự sinh ngẫu nhiên chuỗi bảo mật `PES2027_TURN_SECRET`, `PES2027_ACCESS_TOKEN`, `PES2027_ADMIN_TOKEN` vào `/etc/pes2027-online/server.env` (quyền 600).
> 4. Tự tạo chứng chỉ SSL 2048-bit (`cert.pem`, `key.pem`) ghim theo IP Public.
> 5. Cấu hình Coturn `/etc/pes2027-online/turnserver.conf` giới hạn băng thông, chống loopback nội bộ.
> 6. Cài đặt 2 service Systemd: `pes2027-online.service` và `pes2027-turn.service`.
> 7. Tạo sẵn file cấu hình client `/etc/pes2027-online/client.json` kèm mã băm SHA-256 fingerprint.

---

## 5. Cấu Trúc Các Tệp Tin Trên Máy Chủ

Sau khi chạy xong, hệ thống được đặt tại các thư mục chuẩn hóa:

* `/opt/pes2027-online/`: Thư mục chạy mã nguồn chính.
  * `pes_online_server.py`: Engine điều phối phòng chơi và WebSocket/REST API.
  * `venv/`: Môi trường Python biệt lập.
  * `logs/`: Ghi log các sự kiện trận đấu.
* `/etc/pes2027-online/`: Thư mục lưu trữ khóa bảo mật (chỉ `root` và nhóm `pes2027` được đọc).
  * `server.env`: Lưu các token bí mật (`PES2027_TURN_SECRET`, `PES2027_ACCESS_TOKEN`).
  * `cert.pem` & `key.pem`: Chứng chỉ TLS phục vụ mã hóa HTTPS cổng 8443.
  * `turnserver.conf`: File cấu hình Coturn Relay.
  * `client.json`: **Tệp chứa thông tin kết nối dành cho Client iOS**.

---

## 6. Lấy Thông Tin Cấu Hình Ghép Vào Client iOS

Sau khi cài đặt xong, xem nội dung tệp `client.json` bằng lệnh:
```bash
sudo cat /etc/pes2027-online/client.json
```

Kết quả sẽ có định dạng như sau:
```json
{
  "server": "https://<IP_PUBLIC_VPS>:8443",
  "enabled": true,
  "token": "a1b2c3d4e5f6...",
  "cert_sha256": "3e9b142f..."
}
```

### Cách nạp vào IPA dành cho tester:
1. Đặt tệp `client.json` vào:
   * Đường dẫn đóng gói: `tools/build_ipa.py --online-config /path/to/client.json`
   * Hoặc thả trực tiếp file `client.json` vào thư mục `Documents/PES2027-online/client.json` trên iPhone qua ứng dụng **Tệp (Files)** hoặc 3uTools.
2. `ZiruOffline.dylib` khi khởi động sẽ tự động đọc `client.json`, xác thực mã băm SHA-256 của chứng chỉ SSL máy chủ (Certificate Pinning) và bật mục **Chơi Online (Friend Match)**.

---

## 7. Lệnh Quản Trị & Kiểm Tra Sức Khỏe Hệ Thống

### Kiểm tra trạng thái hoạt động:
```bash
sudo systemctl status pes2027-online
sudo systemctl status pes2027-turn
```

### Xem log thời gian thực:
```bash
# Log của API Ghép trận & Tạo phòng:
sudo journalctl -u pes2027-online -f

# Log của Relay Coturn:
sudo journalctl -u pes2027-turn -f
```

### Khởi động lại dịch vụ:
```bash
sudo systemctl restart pes2027-online pes2027-turn
```

### Chạy kịch bản kiểm thử toàn diện (Smoke Test):
Script kiểm tra toàn bộ luồng tạo phòng, vào phòng, chứng thực token TURN và cấp phát port relay:
```bash
python3 /tmp/pes2027-mod/online/smoke_online.py /etc/pes2027-online/client.json
```
Nếu màn hình trả về `[PASS] ALL SMOKE TESTS PASSED`, hệ thống sẵn sàng 100%!

---

## 8. Cơ Chế Dự Phòng: Đá Mạng Nội Bộ (LAN) & Bluetooth

Trong trường hợp máy chủ Online bị tắt hoặc không có Internet:
1. `ZiruOffline.dylib` được tích hợp sẵn cơ chế **Local Peer Discovery**.
2. Khi 2 iPhone bắt chung 1 mạng Wi-Fi (hoặc 1 máy phát Wi-Fi Hotspot cho máy kia kết nối):
   * Client tự động phát gói tin UDP Broadcast để tìm phòng chơi trên dải IP `192.168.x.x`.
   * Giao tiếp trực tiếp giữa 2 thiết bị mà không cần đi qua bất kỳ máy chủ trung gian nào trên Internet.
   * Đảm bảo tính an toàn pháp lý tuyệt đối về mặt bản quyền và cho phép thi đấu ngoại tuyến mọi lúc mọi nơi.
