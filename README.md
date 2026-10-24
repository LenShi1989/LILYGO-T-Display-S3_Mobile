# LILYGO T-Display-S3 控制面板

以 Arduino IDE 開發的 LILYGO T-Display-S3（ESP32-S3）多功能控制面板。使用 LovyanGFX 驅動 170x320 橫向 ST7789 顯示器，提供繁體中文選單、WiFi 設定、天氣、NTP 時鐘、股票報價與折線圖，並在右上角顯示 WiFi 與電池狀態。

目前軟體版本：`1.0.2`

## 功能

### 主選單

| 項目 | 說明 |
| --- | --- |
| 系統狀態 | WiFi 資訊、系統資訊、顯示器設定 |
| WiFi設定 | 掃描網路、手動設定、清除設定 |
| 天氣 | Open-Meteo 即時天氣與今日高低溫 |
| 時間 | NTP 網路對時，UTC+8 數字時鐘 |
| 股票 | Yahoo Finance 報價與當日 5 分鐘折線圖 |

### 系統狀態

- WiFi 資訊：連線狀態、SSID、IP、訊號強度、閘道、MAC、主機名稱。
- 系統資訊：晶片型號、核心頻率、Flash、PSRAM、記憶體、運作時間、電池電壓、SDK 版本。
- 顯示器：驅動晶片、面板解析度、畫布解析度、匯流排、亮度與色彩測試條。
- 顯示器頁面短按 P0 可切換四段亮度。

### WiFi 設定

- 掃描附近 2.4GHz WiFi，依訊號強度排序並去除重複 SSID。
- 顯示加密鎖頭與訊號格數。
- 手動輸入 SSID 與密碼。
- 清除已儲存的 WiFi 設定。
- 連線成功後自動寫入 NVS，開機自動重連。

### 天氣

- 資料來源：Open-Meteo。
- 顯示目前溫度、體感溫度、濕度、風速、雨量、今日最高/最低溫、天氣描述與更新時間。
- 預設地點：台北（25.0330, 121.5654）。
- 短按 P14 重新整理。

### 時間

- 使用 NTP 對時，時區固定 UTC+8。
- 大字顯示時分，右側顯示秒數。
- 顯示日期與星期。
- 網路未連線時顯示提示訊息。

### 股票

- 資料來源：Yahoo Finance Chart API。
- 預設代號：`2330.TW`。
- 純數字代號會自動加上 `.TW`，例如 `2330` 轉為 `2330.TW`。
- 報價頁顯示現價、漲跌、漲跌幅、昨收、最高、最低、成交量與更新時間。
- 折線圖顯示當日 5 分鐘收盤價、昨收參考線、最高/最低價刻度與時間範圍。
- 若 `1d` 區間沒有資料（例如部分台股），會自動改用 `5d`，並只取最後一個交易日的走勢。
- 台灣習慣紅漲綠跌。
- 短按 P14 重新整理。

### 狀態列

- 右上角顯示 WiFi 圖示。
- 顯示電池百分比與電池圖示。
- WiFi 連線中時圖示閃爍。
- 電量每 15 秒更新，圖示每秒更新。

## 目錄結構

```text
LILYGO T-Display-S3/
├─ README.md
├─ TDisplayS3_Menu/
│  └─ TDisplayS3_Menu.ino
└─ docs/
   └─ 硬體腳位參考圖片
```

所有程式碼集中於 `TDisplayS3_Menu/TDisplayS3_Menu.ino`，不需要額外的字型檔或圖檔。中文使用 LovyanGFX 內建的 `efontTW_14` 與 `efontTW_16_b`。

## 硬體需求

- LILYGO T-Display-S3（ESP32-S3）。
- USB-C 傳輸線。
- 2.4GHz WiFi 網路（ESP32-S3 不支援 5GHz）。
- 選購：單節鋰電池，用於電量顯示。

### 腳位

| 功能 | GPIO |
| --- | --- |
| LCD D0 | 39 |
| LCD D1 | 40 |
| LCD D2 | 41 |
| LCD D3 | 42 |
| LCD D4 | 45 |
| LCD D5 | 46 |
| LCD D6 | 47 |
| LCD D7 | 48 |
| LCD WR | 8 |
| LCD RD | 9 |
| LCD DC | 7 |
| LCD CS | 6 |
| LCD RST | 5 |
| LCD 背光 | 38 |
| LCD 電源 | 15 |
| 按鍵：下一項/上一項 | 0 |
| 按鍵：確認/返回 | 14 |
| 電池電壓 ADC | 4 |

### 顯示器設定

| 參數 | 值 |
| --- | --- |
| 驅動 IC | ST7789 |
| 面板解析度 | 170x320 |
| 使用方向 | 橫向 320x170 |
| 匯流排 | 8-bit parallel |
| 寫入頻率 | 20MHz |
| 旋轉 | 1 |
| X 偏移 | 35 |
| 顏色反轉 | 是 |
| RGB 順序 | BGR |
| 背光 | PWM，12kHz，channel 7 |

## 開發環境

| 項目 | 版本 |
| --- | --- |
| Arduino IDE | 2.x |
| ESP32 Arduino Core | 3.3.12 |
| LovyanGFX | 1.2.34 |
| ArduinoJson | 7.4.2 |

### 安裝函式庫

在 Arduino IDE 的「程式庫管理員」安裝：

- `LovyanGFX`
- `ArduinoJson`

不需要安裝或設定 `TFT_eSPI`。

### 開發板設定

1. 開啟 `TDisplayS3_Menu/TDisplayS3_Menu.ino`。
2. 開發板選擇 `LilyGo T-Display-S3`。
3. 連接 USB-C 後選擇對應序列埠。
4. 若無法上傳或序列埠沒有出現，確認 USB CDC On Boot 已啟用，或按住 BOOT 鍵再上傳。
5. 上傳。

命令列編譯方式：

```powershell
& "C:\Users\Len\AppData\Local\Programs\Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe" compile --fqbn esp32:esp32:lilygo_t_display_s3 "C:\Users\Len\Desktop\LILYGO T-Display-S3\TDisplayS3_Menu"
```

目前編譯結果：

```text
Sketch 使用 2217879 位元組（70%）的程式儲存空間。最大為 3145728 位元組
全域變數使用 51224 位元組 (15%) 的動態記憶體，保留 276456 位元組給區域變數。最大 327680 位元組
```

## 操作方式

### 按鍵

| 按鍵 | 短按 | 長按 |
| --- | --- | --- |
| P0（GPIO 0） | 下一項 | 上一項 |
| P14（GPIO 14） | 確認/進入 | 返回 |

長按判定時間為 700ms。

### 各畫面操作

| 畫面 | P0 | P14 |
| --- | --- | --- |
| 主選單/子選單 | 下一項 | 短按進入；長按回主選單 |
| WiFi 掃描 | 下一個網路 | 短按連線；長按返回 |
| 鍵盤 | 下一個按鍵 | 短按輸入；長按取消 |
| 天氣 | 無作用 | 短按更新；長按返回 |
| 時鐘 | 無作用 | 長按返回 |
| 股票報價/走勢 | 無作用 | 短按更新；長按返回 |
| 顯示器 | 切換亮度 | 長按返回 |
| WiFi 資訊/系統資訊 | 無作用 | 長按返回 |
| 清除設定 | 切換是/否 | 短按確認；長按返回 |

### 螢幕鍵盤

- 5 列 x 10 欄按鍵。
- `SFT`：切換大小寫。
- `SPC`：輸入空白。
- `.com`：快速輸入 `.com`。
- `DEL`：刪除最後一個字元。
- `OK`：確認。
- 輸入密碼時以 `*` 遮蔽。
- 密碼輸入完成後按 `OK` 開始連線。

## 設定儲存

使用 ESP32 `Preferences` 儲存於 NVS，namespace 為 `tds3`。

| Key | 內容 | 預設值 |
| --- | --- | --- |
| `ssid` | WiFi SSID | 空 |
| `pass` | WiFi 密碼 | 空 |
| `sym` | 股票代號 | `2330.TW` |
| `lat` | 天氣緯度 | `25.0330` |
| `lon` | 天氣經度 | `121.5654` |
| `city` | 天氣城市名稱 | `台北` |

目前 UI 只提供 WiFi 與股票代號的設定介面。若要變更天氣地點，需修改程式中的預設值，或自行擴充設定畫面。

## 網路服務

| 服務 | 位址 | 用途 |
| --- | --- | --- |
| Open-Meteo | `https://api.open-meteo.com/v1/forecast` | 即時天氣與每日高低溫 |
| Yahoo Finance | `https://query1.finance.yahoo.com/v8/finance/chart/{symbol}` | 股票報價與 5 分鐘走勢 |
| NTP | `pool.ntp.org`、`time.google.com`、`time.cloudflare.com` | 網路對時 |

- HTTPS 使用 `WiFiClientSecure`，目前呼叫 `setInsecure()` 略過憑證驗證。
- HTTP 請求會跟隨重新導向。
- 天氣請求逾時 12 秒，股票請求逾時 15 秒。
- WiFi 初次連線逾時 15 秒，其他網路操作確保連線逾時 12 秒。

## 程式架構

### 顯示層

`LGFX` 類別繼承 `lgfx::LGFX_Device`，集中設定：

- `Bus_Parallel8`：8 位元並列匯流排與 GPIO。
- `Panel_ST7789`：面板解析度、偏移、反轉與 RGB 順序。
- `Light_PWM`：背光腳位、頻率與 PWM channel。

全域物件 `lcd` 負責所有繪圖。

### 狀態與資料

- `Screen`：目前畫面列舉。
- `MenuData`：選單項目、數量、選取項、捲動位置與標題。
- `WeatherData`：天氣資料與有效性。
- `StockData`：股票報價、時間戳與走勢點。
- `Preferences`：讀寫 NVS 設定。

### 按鍵

`Button` 類別提供：

- 20ms 去抖動。
- 短按事件。
- 700ms 長按事件。
- `reset()` 可在畫面切換後清除殘留狀態。

`btnNext` 對應 GPIO 0，`btnOk` 對應 GPIO 14。

### 畫面流程

```text
主選單
├─ 系統狀態
│  ├─ WiFi資訊
│  ├─ 系統資訊
│  └─ 顯示器
├─ WiFi設定
│  ├─ 掃描網路 → 鍵盤（密碼）
│  ├─ 手動設定 → 鍵盤（SSID）→ 鍵盤（密碼）
│  └─ 清除設定
├─ 天氣
├─ 時間
└─ 股票
   ├─ 股票查詢
   ├─ 折線圖
   └─ 更改代號 → 鍵盤
```

畫面切換由 `handleInput()` 依 `screen` 分派，選單移動由 `menuMove()` 處理，選單項目由 `activateMenuItem()` 執行。

### 網路與資料流程

```text
ensureWiFi()
  └─ httpGet()
       ├─ fetchWeather() → Open-Meteo JSON → WeatherData
       └─ fetchStock()   → Yahoo JSON     → StockData
```

- `ensureWiFi()` 確認連線，必要時使用已儲存帳密重連。
- `httpGet()` 建立 HTTPClient，依 URL 使用 WiFiClient 或 WiFiClientSecure。
- `fetchWeather()` 使用 ArduinoJson 解析 `current` 與 `daily`。
- `fetchStock()` 先請求 `1d`，若走勢點不足則改用 `5d`；`parseStockPayload()` 解析 `chart.result[0].meta` 與 `indicators.quote[0].close`，最多保留 120 個走勢點，並只保留最後一個交易日的資料。
- `ensureTimeService()` 在首次連線後啟動 NTP。

### 繪圖

- `drawHeader()`：標題列與狀態圖示。
- `drawFooter()`：底部操作提示。
- `drawStatusIcons()`：WiFi 與電池圖示。
- `drawMenuScreen()`：通用選單與捲動軸。
- `drawWeatherScreen()`、`drawClockScreen()`、`drawStockQuoteScreen()`、`drawStockChartScreen()`：各功能頁。
- `drawKeyboard()`：螢幕鍵盤。
- `drawBusy()`、`showToast()`：載入與提示訊息。

### 主迴圈

`loop()` 每次執行：

1. 輪詢兩個按鍵並處理輸入。
2. 更新 WiFi 連線狀態。
3. 連線成功時啟動 NTP。
4. 每 15 秒更新電池。
5. 每秒更新狀態圖示。
6. 時鐘頁面每秒更新時間。
7. 短暫延遲 5ms。

## 疑難排解

### 螢幕白屏或黑屏

- 確認開發板型號與腳位是否為 T-Display-S3。
- 確認 GPIO 15 已設為 HIGH，LCD 電源才會開啟。
- 確認 `offset_x = 35`、`rotation = 1`、`invert = true`。
- 若畫面方向或偏移不正確，調整 `LGFX` 建構子中的面板參數。

### 顏色相反或紅藍顛倒

調整 `_panel_instance.config()` 中的：

- `cfg.invert`
- `cfg.rgb_order`

### 無法上傳

- 確認使用可傳輸資料的 USB-C 線。
- 確認序列埠驅動正常。
- 嘗試按住 BOOT 鍵後再上傳。
- 確認 Arduino IDE 的 USB CDC On Boot 設定。

### WiFi 無法連線

- ESP32-S3 僅支援 2.4GHz WiFi。
- 確認 SSID 與密碼正確。
- 使用「清除設定」後重新輸入。
- 確認路由器沒有開啟不相容的加密或 MAC 過濾。

### 天氣或股票無法更新

- 確認 WiFi 已連線且可存取網際網路。
- Yahoo Finance 為非官方端點，可能限制請求或變更格式。
- 若走勢頁空白但報價正常，程式會自動嘗試 `5d` 區間；仍失敗時請確認該代號在 Yahoo Finance 是否有盤中資料。
- 確認股票代號格式，例如台股 `2330.TW`、美股 `AAPL`。
- HTTPS 使用 `setInsecure()`，若網路環境需要憑證驗證，需自行加入根憑證。

### 中文顯示亂碼

- 確認 `.ino` 以 UTF-8 儲存。
- 本專案使用 LovyanGFX 內建中文點陣字型，不需要額外字型檔。

### 時間不正確

- 確認已連上 WiFi。
- 確認網路可存取 NTP。
- 時區固定為 `CST-8`（UTC+8）。

### 電量不準

- 確認電池分壓比 `BAT_RATIO = 2.0` 與實際電路相符。
- 可調整 `BAT_MIN_V`、`BAT_MAX_V` 校正百分比。
- 無電池時顯示 `--`。

### 按鍵方向相反

確認 `PIN_BTN_NEXT = 0`、`PIN_BTN_OK = 14`。若硬體版本不同，修改這兩個常數。

## 注意事項

- WiFi 帳密以明文儲存在 NVS，請勿在共用設備上使用重要帳密。
- `setInsecure()` 不驗證 TLS 憑證，僅適合個人裝置與受信任網路。
- Yahoo Finance API 非官方公開 API，長期可用性不受保證。
- 股票資料可能延遲，僅供參考，不構成投資建議。
- 本專案未包含 OTA 更新功能。
- 若要擴充天氣地點設定，可新增緯度、經度與城市輸入畫面，並沿用現有鍵盤元件。
