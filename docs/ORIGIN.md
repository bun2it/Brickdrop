# Nguồn gốc source seed

* Repo gốc: RomCloud (`/Users/tai/RomCloud`), thư mục `src/localsend/`.
* Commit gốc:
  * `2ce4be2 v2.2.0: LocalSend HTTPS & cancel handling, borderless UI, File Explorer enhancements`
* Ngày seed BrickDrop: 2026-10-07.
* Phương pháp: copy verbatim 3 file, chưa sửa logic.

## Dependency của LocalSendManager.cpp cần tách khi build độc lập

| Include trong code gốc | Dùng để làm gì | Hướng stub/port |
|---|---|---|
| `../config/AppConfig.h` | alias + fingerprint persist (`getLocalSendAlias`, `getOrCreateLocalSendFingerprint`), `getRomsDir`, `getDataDir` | thay bằng config file local `~/.brickdrop/` |
| `../logging/Logger.h` | `Logger::info/warn/error` | logger nhẹ (stderr + file) |
| `../database/DatabaseManager.h` | `upsertGame` sau khi nhận ROM | bỏ qua ở P0, hoặc sqlite tối giản |
| `../database/RomIndexer.h` | `scanAllSystems` post-process | bỏ qua ở P0 |
| `../rom/RomOrganizer.h` | `organizeDirectory(Inbox)` post-process | giữ file ở Inbox ở P0 |
| `../ui/UIManager.h` | `setNeedLibraryRefresh` | bỏ qua ở P0 |
| `curl/curl.h` | sender HTTP client (`httpPostJson/Binary`) | giữ, link `-lcurl` |

Phần protocol thuần (`LocalSendProtocol.h`, discovery UDP, HTTP server raw socket)
không phụ thuộc RomCloud và là lõi để BrickDrop tái dùng.
