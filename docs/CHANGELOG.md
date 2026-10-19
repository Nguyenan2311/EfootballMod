# Changelog

Tất cả các thay đổi và cập nhật quan trọng của dự án `pes2027-mod` được ghi lại tại đây.

## [v1.1.2] - 2026-10-18
### Tính năng mới & Cải tiến
- **Hiển thị tên cầu thủ trên lưng áo (Jersey Uniform Name)**: Triển khai bộ 13 binary patch cho engine game (`PlayerModelConfig::SetHasUniformName`, `SetupUniformConfig`, `APlayerActorMobile::BackNumberStep`, `APlayerActorMobile::SetUniformName`, `APlayerActor::SetUniformName`), kích hoạt vẽ font tên cầu thủ phía trên số áo trong trận đấu và cutscene ăn mừng.
- **Khắc phục triệt để lỗi đá Rank AI / Division Match**: Xây dựng hàm `z27AwaySquad` tái tạo cấu trúc đội hình đối thủ (AWAY) chuẩn hóa gồm 23 cầu thủ (đầy đủ `parameters`, `play_style`, `uniform_number`), Object HLV hoàn chỉnh và kích hoạt Coach Link. Xóa bỏ hoàn toàn lỗi kẹt xoay vòng rồi báo "Xong trận".

### Sửa lỗi
- **Xung đột Skill chính & Kỹ năng bổ sung đặc biệt (Showtime)**:
  - Khôi phục 100% skill chính gốc Konami (`parameters[70..141]`) từ catalog template khi nạp cầu thủ từ SQLite, không bị mất skill chính sau khi thoát game.
  - Sửa cơ chế unshift ID kỹ năng bổ sung (`iosSkillId`), thiết lập ánh xạ 1-1 cho toàn bộ 74 kỹ năng iOS (bao gồm 2 kỹ năng đặc biệt Showtime tại index 25 và 68 sang internal 72/73), ngăn chặn hiện tượng ID bị dịch chuyển lũy tiến làm kỹ năng đặc biệt Showtime biến thành kỹ năng thường.
  - Loại bỏ lỗi ghi đè SQLite trùng lặp khi huấn luyện kỹ năng: lưu đồng bộ mảng `additional_skill_id` và các bit kỹ năng trong `parameters` trong một thao tác duy nhất.
- **Nhận vật phẩm khi hoàn thành nhiệm vụ**:
  - Triển khai handler đầy đủ cho `CMD_RECV_MYCLUB_ACHIEVEMENT`, trả về chi tiết danh sách vật phẩm nhận thưởng (`achievement_item_info_list`) gồm vé chọn kỹ năng đặc biệt, thẻ kinh nghiệm, GP và eFootball Coin.
  - Khôi phục danh sách nhiệm vụ hoàn chỉnh trong `set_myclubuser_achieveentlist.json`, `CmdGetMyclubAchievementlist.json` và menu chính.
- **Sửa lỗi liên kết HLV phụ & số áo cầu thủ khi vào trận**:
  - Khắc phục `ability_id[3]` bị dịch quá tầm cho Conte (12) và Mourinho (14) trong `core/ios_compat.hpp`.
  - Bổ sung `uniform_number` cho toàn bộ danh sách cầu thủ đội hình sân nhà trong `z27Home`, ngăn chặn lỗi UE4 engine không tải được trận đấu AI.
- **Khắc phục lỗi CMF 40 & Silhouette trắng của Çalhanoğlu & Mkhitaryan**:
  - Gán độ thành thạo vị trí (DMF, CMF, LMF, RMF, AMF, SS) lên mức tối đa `2` (Full Green), khôi phục chỉ số CMF về `84+`.
  - Chuẩn hóa ID cầu thủ card thường về 24-bit base ID (`44379`, `40352`) để engine Unreal nạp trực tiếp texture khuôn mặt thật từ file `pc7000_mobile_ios.utoc`.

## [v1.1.1] - 2026-10-17
### Tính năng mới & Cải tiến
- **Chuẩn hóa điểm huấn luyện (Progression Points)**: Áp dụng công thức tính điểm `max(0, level_limit - 1) * 2 + 60` đồng nhất giữa màn hình đăng nhập, chiêu mộ cầu thủ, phân bổ chỉ số và reset điểm.
- **Cơ chế thăng hạng Rank AI**: Tự động thăng hạng Division ngay khi đạt mốc `point_promoted`.

### Sửa lỗi
- **Giữ HLV khi vào trận**: Duy trì đúng HLV và số serial sở hữu khi gọi `CMD_START_GAME`.
- **Bảo vệ bộ nhớ & Chống crash**: Vá lỗi rẽ nhánh `0x1046FE0B0` (`AddStadiumDlTask::Bind`) tránh crash khi nạp sân vận động.

## [v1.1.0] - 2026-10-17
### Tính năng mới & Cải tiến
- **Online Friend Match (P2P & STUN/TURN Relay)**: Bổ sung máy chủ Signaling API (HTTPS) và tích hợp relay Coturn cho phép 2 thiết bị iOS thi đấu giao hữu qua mạng Internet.
- **Bảo mật Certificate Pinning**: Kiểm tra mã băm SHA-256 của chứng chỉ SSL máy chủ ngay trong `ZiruOffline.dylib`.

## [v1.0.0] - 2026-10-16
### Phiên bản ban đầu
- **C++ Offline Server Core**: Port hoàn chỉnh logic máy chủ Ziru từ Java/Android sang C++17 thuần, chạy trực tiếp trong tiến trình game (In-process server) với SQLite3 và SharedPreferences giả lập.
- **Hook mạng In-process (`ZiruOffline.dylib`)**: Đánh chặn `HttpImplAsyncRequest` / `HttpImplRequest`, chuyển hướng toàn bộ traffic sang loopback `https://127.0.0.1:8085` thông qua `NSURLProtocol`.
- **Tách dữ liệu khỏi IPA Bundle**: Vá hàm `basic::PathGet()` trong Mach-O binary để chuyển hướng nạp `pak/` và `cpk/` ra thư mục Documents, giúp file IPA nhẹ (~180MB) và người chơi có thể cập nhật data pack độc lập.
- **Hỗ trợ tiếng Việt đầy đủ**: Nạp bộ font tiếng Việt (`eFootballSans`, `eFootballStencil`) trực tiếp vào Unreal Engine Slate.
- **Mở khóa toàn bộ sân vận động**: Cho phép chọn các sân vận động bản quyền (Bernabéu, Camp Nou, San Siro, Emirates, v.v.) trong mọi chế độ đấu.
