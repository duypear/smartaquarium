/*
 * ========================================
 * SMART AQUARIUM SYSTEM - ESP32-C3
 * Version: WebServer (không dùng Async)
 * Tương thích mọi ESP32 core
 * ========================================
 */

#include <WiFi.h>
#include <WebServer.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <ESP32Servo.h>
#include <Adafruit_NeoPixel.h>
#include <LiquidCrystal_I2C.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include <time.h>

#define TEMP_SENSOR_PIN    1    // DS18B20
#define WATER_LEVEL_PIN    2    // Cảm biến mực nước (ADC)
#define RELAY_PIN          3    // Relay bơm lọc
#define SERVO_PIN          6    // Servo cho ăn
#define LED_PIN            8    // LED RGB WS2812B
#define AIR_PUMP_PIN       7    // Relay máy sục khí
#define LCD_SDA            4    // LCD I2C SDA
#define LCD_SCL            5    // LCD I2C SCL

#define NUM_LEDS           27   // Số LED RGB
#define LCD_ADDRESS        0x27 // Địa chỉ I2C LCD
#define SERVO_FEED_ANGLE   180   // Góc xoay servo khi cho ăn
#define SERVO_REST_ANGLE   0    // Góc nghỉ servo


#define TEMP_HIGH          29.0 // Nhiệt độ cao 
#define TEMP_LOW           17.0 // Nhiệt độ thấp
#define WATER_LOW          0  // Mực nước thấp 
#define WATER_NORMAL       300 // Mực nước bình thường
#define WATER_HIGH         1900 // Mực nước cao

// Lịch mặc định
#define FEED_TIMES         5    // Số lần cho ăn/ngày
#define FILTER_INTERVAL    3600000 // Lọc mỗi 1 tiếng (ms)
#define FILTER_DURATION    1800000 // Lọc trong 30 phút (ms)

// khai báo các đối tượng 
OneWire oneWire(TEMP_SENSOR_PIN);
DallasTemperature sensors(&oneWire);
Servo servoFeeder;
Adafruit_NeoPixel strip(NUM_LEDS, LED_PIN, NEO_GRB + NEO_KHZ800);
LiquidCrystal_I2C lcd(LCD_ADDRESS, 16, 2);
WebServer server(80);
Preferences prefs;

// WiFi
const char* ssid = "Van Minh";     
const char* password = "11118888";    

// Trạng thái hệ thống
float temperature = 0.0;
int waterLevel = 0;
String waterStatus = "NORMAL";
bool pumpRunning = false;
bool systemOK = true;



// LED RGB
int ledBrightness = 128;  // 0-255
uint32_t ledColor = strip.Color(0, 100, 255); // Mặc định xanh dương

int ledOnHour = 18;
int ledOffHour = 22;
bool ledCurrentState = false;

// lịch cho ăn 
int feedSchedule[10] = {7, 12, 15, 18, 21, -1, -1, -1, -1, -1}; // -1 = không dùng
String lastFeedTime = "Chua co";
// ===== TRACKING CHO ĂN =====
unsigned long lastAutoFeedMillis = 0;  
unsigned long lastManualFeedMillis = 0; 

// Bơm lọc
unsigned long lastFilterStart = 0;
unsigned long filterRunTime = 0;
bool filterAutoMode = true;
bool isFilterAutoRunning = false;  
unsigned long lastAutoFilterEnd = 0;  // Thời điểm kết thúc chu kỳ auto gần nhất
// lịch bơm lọc
int filterIntervalHours = 1;    // Lọc mỗi bao nhiêu tiếng
int filterDurationMinutes = 30; // Chạy trong bao nhiêu phút
// Máy sục khí
bool airPumpRunning = false;

int airPumpOnHour = 0;   // Luôn bật (0h-24h)
int airPumpOffHour = 24; // Không tắt
// Thời gian
struct tm timeinfo;
bool timeValid = false;
unsigned long lastTimeSync = 0;

// Camera ESP32-CAM
String cameraIP = "10.30.248.83"; //  IP của ESP32-CAM

int currentLCDPage = 0;                      // Trang hiện tại (0-5)
unsigned long lastLCDPageChange = 0;         // Thời điểm đổi trang cuối
const unsigned long LCD_PAGE_INTERVAL = 8000; // Đổi trang mỗi 8 giây
const int TOTAL_LCD_PAGES = 6;               
struct HistoryLog {
  String time;
  String event;
  String value;
};
// Lịch sử hoạt động (lưu 10 sự kiện gần nhất)
HistoryLog history[10];
int historyIndex = 0;

// Thống kê
unsigned long totalFeedCount = 0;
unsigned long totalFilterTime = 0;
float tempMin = 999.0;
float tempMax = -999.0;

// Cài đặt nâng cao
bool enableNotifications = true;
bool enableAutoFeed = true;
bool enableAutoFilter = true;
bool enableAutoLED = true;
bool enableAutoAirPump = true;  
// Kiểm tra điều khiển thủ công
unsigned long lastManualControl = 0;
const unsigned long MANUAL_TIMEOUT = 0;
bool isManualOverrideLED = false;     
bool isManualOverrideAirPump = false;  

unsigned long lastSaveTime = 0;
const unsigned long SAVE_INTERVAL = 5000;

void saveSettingsSafe() {
  if (millis() - lastSaveTime > SAVE_INTERVAL) {
    saveSettings();
    lastSaveTime = millis();
  }
}


void setup() {
  Serial.begin(115200);
  delay(1000);
  
  Serial.println("\n\n========================================");
  Serial.println("SMART AQUARIUM SYSTEM - ESP32-C3");
  Serial.println("========================================\n");

  // B1:Tải cài đặt cũ
  prefs.begin("aquarium", false);
  loadSettings();
  
  // B2: khởi tạo phần cứng
  pinMode(RELAY_PIN, OUTPUT);
  pinMode(WATER_LEVEL_PIN, INPUT);
  pinMode(AIR_PUMP_PIN, OUTPUT);
  
  // Cảm biến nhiệt độ
  sensors.begin();
  Serial.println("✓ DS18B20 khoi tao");

  // Servo
  servoFeeder.attach(SERVO_PIN);
  servoFeeder.write(SERVO_REST_ANGLE);
  Serial.println("✓ Servo khoi tao");

  // LED
  strip.begin();
  strip.setBrightness(ledBrightness);
  Serial.println("✓ LED RGB khoi tao");

  // LCD
  Wire.begin(LCD_SDA, LCD_SCL);
  lcd.init();
  lcd.backlight();
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("SMART AQUARIUM");
  lcd.setCursor(0, 1);
  lcd.print("Starting...");
  Serial.println("✓ LCD khoi tao");
  
  //B3 : KHôi phục trạng thái cũ
  digitalWrite(RELAY_PIN, pumpRunning ? HIGH : LOW);
  digitalWrite(AIR_PUMP_PIN, airPumpRunning ? HIGH : LOW);
  
  if (ledCurrentState) {
    for (int i = 0; i < NUM_LEDS; i++) {
      strip.setPixelColor(i, ledColor);
    }
    strip.show();
  } else {
    strip.clear();
    strip.show();
  }
  
  Serial.println("========== TRANG THAI KHOI PHUC ==========");
  Serial.printf("Bom loc: %s\n", pumpRunning ? "BAT" : "TAT");
  Serial.printf("LED: %s\n", ledCurrentState ? "BAT" : "TAT");
  Serial.printf("May suc: %s\n", airPumpRunning ? "BAT" : "TAT");
  Serial.println("==========================================");

  //B4: Kết nối wifi, khởi động webserver
  connectWiFi();
  syncTime();

  setupWebServer();
  server.begin();
  Serial.println("✓ Web Server khoi dong");

  // BƯỚC 5: LCD
  Serial.println("\n========================================");
  Serial.println("HE THONG SAN SANG!");
  Serial.println("========================================\n");

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("2GenAqua !");
  lcd.setCursor(0, 1);
  lcd.print("Starting...");
  delay(1000);

  for (int i = 0; i < 2; i++) {
    lcd.noBacklight();
    delay(150);
    lcd.backlight();
    delay(150);
  }

  lcd.clear();
  lcd.setCursor(0, 0);

  if (WiFi.status() == WL_CONNECTED) {
    lcd.print("WiFi:CONNECTED");
    lcd.setCursor(0, 1);
    lcd.print("IP:");
    
    IPAddress ip = WiFi.localIP();
    String ipStr = ip.toString();
    
    if (ipStr.length() <= 13) {
      lcd.print(ipStr);
    } else {
      lcd.printf("%d.%d.*.%d", ip[0], ip[1], ip[3]);
    }
    
    Serial.printf("LCD: IP = %s\n", ipStr.c_str());
    
    for (int i = 0; i < 2; i++) {
      lcd.noBacklight();
      delay(100);
      lcd.backlight();
      delay(100);
    }
  } else {
    lcd.print("WiFi:OFFLINE   ");
    lcd.setCursor(0, 1);
    lcd.print("Check Router!  ");
    
    for (int i = 0; i < 3; i++) {
      lcd.noBacklight();
      delay(100);
      lcd.backlight();
      delay(100);
    }
  }

  delay(3000);
  lcd.clear();
  Serial.println("Bat dau che do luan chuyen...");
}

// ==================== LOOP ====================
void loop() {
  server.handleClient();

  if (millis() - lastTimeSync > 3600000) {
    syncTime();
  }
  static unsigned long lastWiFiCheck = 0;
  if (millis() - lastWiFiCheck > 30000) {  
    lastWiFiCheck = millis();
    
    if (WiFi.status() != WL_CONNECTED) {
      Serial.println(" Mat ket noi WiFi - Dang thu ket noi lai...");
      
      // Hiển thị lên LCD
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("WiFi: Reconnect");
      lcd.setCursor(0, 1);
      lcd.print("Please wait...");
      
      // Thử kết nối lại
      WiFi.reconnect();
      delay(3000);
      
      if (WiFi.status() == WL_CONNECTED) {
        Serial.println(" Ket noi lai WiFi thanh cong!");
        Serial.print("IP: ");
        Serial.println(WiFi.localIP());
        
        // Hiển thị IP mới
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print("WiFi:CONNECTED");
        lcd.setCursor(0, 1);
        lcd.print("IP:");
        lcd.print(WiFi.localIP().toString());
        delay(3000);
      } else {
        Serial.println(" Khong the ket noi lai WiFi");
      }
    }
  }
  if (timeValid) {
    getLocalTime(&timeinfo);
  }

  // Đọc cảm biến
  readSensors();

  // Kiểm tra cảnh báo
  checkAlerts();

// Điều khiển tự động
autoFilterControl();
autoFeedControl();
autoLEDControl();
autoAirPumpControl();
  // Cập nhật LCD
  updateLCD();

  delay(100); 
}

// Đọc cảm biến
void readSensors() {
  static unsigned long lastRead = 0;
  if (millis() - lastRead < 1000) return; // Đọc mỗi giây
  lastRead = millis();
  
  // Đọc nhiệt độ
  sensors.requestTemperatures();
  temperature = sensors.getTempCByIndex(0);
  // Cập nhật min/max
  if (temperature > -50 && temperature < 100) {
    if (temperature < tempMin) tempMin = temperature;
    if (temperature > tempMax) tempMax = temperature;
  }
  if (temperature == DEVICE_DISCONNECTED_C || temperature < -50) {
    temperature = 0.0;
  }

  // Đọc mực nước
  waterLevel = analogRead(WATER_LEVEL_PIN);
  
  if (waterLevel < WATER_LOW) {
    waterStatus = "LOW";
  } else if (waterLevel < WATER_NORMAL) {
    waterStatus = "NORMAL";
  } else if (waterLevel < WATER_HIGH) {
    waterStatus = "NORMAL";
  } else {
    waterStatus = "HIGH";
  }
}

// ==================== KIỂM TRA CẢNH BÁO ====================
void checkAlerts() {
  systemOK = true;

  // Cảnh báo nhiệt độ
  if (temperature > TEMP_HIGH || temperature < TEMP_LOW) {
    systemOK = false;
  }

  // Cảnh báo mực nước thấp
  if (waterStatus == "LOW") {
    systemOK = false;
    
//Tắt bơm nước
if (pumpRunning && enableAutoFilter) {  
  digitalWrite(RELAY_PIN, LOW);
  pumpRunning = false;
  addHistory("Bơm lọc", "Tắt (nước thấp)");
}
  }
}
//  Điều khiển tự động bơm lọc

void autoFilterControl() {
  if (!enableAutoFilter) {
    isFilterAutoRunning = false;
    return;
  }

  unsigned long currentMillis = millis();
  unsigned long intervalMs = filterIntervalHours * 3600000UL;
  unsigned long durationMs = filterDurationMinutes * 60000UL;

  // ===== KIỂM TRA XEM CÓ ĐANG TRONG CHU KỲ AUTO KHÔNG =====
  if (isFilterAutoRunning) {
    //  Đang trong chu kỳ AUTO
    if (currentMillis - lastFilterStart >= durationMs) {
      // Hết thời gian → Tắt AUTO
      digitalWrite(RELAY_PIN, LOW);
      pumpRunning = false;
      isFilterAutoRunning = false;
      lastAutoFilterEnd = currentMillis;  
      
      totalFilterTime += durationMs;
      addHistory("Bơm lọc", "Dừng tự động");
      saveSettings();
      Serial.println(" Dung bom loc AUTO (het thoi gian)");
    }
    
  } else {
    // ===== KHÔNG TRONG CHU KỲ AUTO - Kiểm tra xem đã đến giờ chạy chưa =====
    
    unsigned long timeSinceLastAutoEnd;
    
    if (lastAutoFilterEnd == 0) {
      // Chưa bao giờ chạy auto → Chạy ngay
      timeSinceLastAutoEnd = intervalMs;
    } else {
      // Đã có chu kỳ auto trước → Tính từ lúc KẾT THÚC chu kỳ auto
      timeSinceLastAutoEnd = currentMillis - lastAutoFilterEnd;
    }
    
    if (timeSinceLastAutoEnd >= intervalMs) {
      if (waterStatus != "LOW") {
        digitalWrite(RELAY_PIN, HIGH);
        pumpRunning = true;
        isFilterAutoRunning = true;  // ← Đánh dấu bắt đầu chu kỳ auto
        
        lastFilterStart = currentMillis;
        addHistory("Bơm lọc", "Bật tự động");
        saveSettings();
        Serial.println("Bat bom loc AUTO (theo lich)");
      } else {
        Serial.println("Bo qua chu ky loc - Muc nuoc thap!");
      }
    }
  }
}
// CHO ĂN TỰ ĐỘNG 
void autoFeedControl() {
  // ===== CHECK 1: Kiểm tra điều kiện cơ bản =====
  if (!enableAutoFeed) {
    static int lastCheckedHour = -1;
    lastCheckedHour = -1;
    return;
  }
  
  if (!timeValid) return;

  // Lấy thời gian hiện tại 
  int currentHour = timeinfo.tm_hour;
  int currentMin = timeinfo.tm_min;
  static int lastFedHour = -1;  // Lưu giờ đã cho ăn gần nhất
  
  // Reset flag khi sang giờ mới
  if (currentHour != lastFedHour && currentMin > 5) {
    // Chỉ reset khi đã qua 5 phút (tránh reset sớm)
    lastFedHour = -1;
  }
  
  // Kiểm tra xem có trùng giờ cho ăn không
  for (int i = 0; i < 10; i++) {
    if (feedSchedule[i] == -1) break;
    
    if (currentHour == feedSchedule[i]) {
      
      if (currentMin >= 0 && currentMin <= 5) {
        
        if (lastFedHour != currentHour) {
          

          if (millis() - lastAutoFeedMillis > 120000) {
              feedFish();
              lastAutoFeedMillis = millis();  
              lastFedHour = currentHour;          
              Serial.printf("✓ Cho an tu dong luc %02d:%02d\n", currentHour, currentMin);
              return;
            }
        }
      }
    }
  }
}

// ==================== THỰC HIỆN CHO ĂN ====================

void feedFish() {
  feedFish(false);  // Mặc định là auto
}

// Hàm cho ăn với tham số phân biệt manual/auto
void feedFish(bool isManual) {
  Serial.println(" Cho ca an...");
  
  servoFeeder.write(SERVO_FEED_ANGLE);
  delay(1000);
  servoFeeder.write(SERVO_REST_ANGLE);
  
  // ===== CẬP NHẬT TRACKING =====
  if (isManual) {
    lastManualFeedMillis = millis();
    Serial.println("    Loai: THU CONG");
  } else {
    // Auto feed đã cập nhật lastAutoFeedMillis trong autoFeedControl()
    Serial.println("   Loai: TU DONG");
  }
  
  // ===== LƯU THỜI GIAN CHO ĂN =====
  if (timeValid) {
    char timeStr[20];
    sprintf(timeStr, "%02d:%02d %02d/%02d", 
            timeinfo.tm_hour, timeinfo.tm_min,
            timeinfo.tm_mday, timeinfo.tm_mon + 1);
    lastFeedTime = String(timeStr);
    
    prefs.putString("lastFeed", lastFeedTime);
    prefs.putULong("lastFeedMs", millis());
    
    // Ghi lịch sử
    addHistory("Cho ăn", isManual ? "Thủ công" : "Tự động");
  } else {
    lastFeedTime = isManual ? "Manual feed" : "Auto feed (no time)";
    
    prefs.putString("lastFeed", lastFeedTime);
    prefs.putULong("lastFeedMs", millis());
    
    addHistory("Cho ăn", isManual ? "Thủ công" : "Tự động");
  }
  
  totalFeedCount++;
  prefs.putULong("feedCount", totalFeedCount);
  
  Serial.println("✓ Hoan thanh cho an");
}
// ==================== ĐIỀU KHIỂN LED TỰ ĐỘNG ====================
// ==================== ĐIỀU KHIỂN LED TỰ ĐỘNG (VỚI OVERRIDE) ====================
// ==================== ĐIỀU KHIỂN LED TỰ ĐỘNG (OPTION 1) ====================
void autoLEDControl() {
  if (!enableAutoLED || !timeValid) return;

  int currentHour = timeinfo.tm_hour;
  bool shouldBeOn = (currentHour >= ledOnHour && currentHour < ledOffHour);

  // ===== KIỂM TRA MANUAL OVERRIDE =====
  if (isManualOverrideLED) {
    //  Reset khi VÀO giờ auto
    if (shouldBeOn) {
     isManualOverrideLED = false;
      Serial.println("⏰ Reset LED manual override (bat dau gio auto)");
      //KHÔNG return → Tiếp tục logic auto
    } else {
      // Ngoài giờ auto → Giữ manual
      return;
    }
  }

  // ===== LOGIC AUTO BÌNH THƯỜNG =====
  if (shouldBeOn && !ledCurrentState) {
    // Bật LED
    for (int i = 0; i < NUM_LEDS; i++) {
      strip.setPixelColor(i, ledColor);
    }
    strip.show();
    ledCurrentState = true;
    addHistory("LED", "Bật tự động");
    Serial.println(" Bat LED (auto)");
    
  } else if (!shouldBeOn && ledCurrentState) {
    // Tắt LED
    strip.clear();
    strip.show();
    ledCurrentState = false;
    addHistory("LED", "Tắt tự động");
    Serial.println(" Tat LED (auto)");
  }
}
// ĐIỀU KHIỂN TỰ ĐỘNG MÁY SỤC KHÍ
void autoAirPumpControl() {  // ← THÊM DÒNG NÀY
  if (!enableAutoAirPump || !timeValid) return;

  int currentHour = timeinfo.tm_hour;
  bool shouldBeOn = (currentHour >= airPumpOnHour && currentHour < airPumpOffHour);

  //  KIỂM TRA MANUAL OVERRIDE
  if (isManualOverrideAirPump) {
    //  Reset khi VÀO giờ auto
    if (shouldBeOn) { 
      isManualOverrideAirPump = false; 
      Serial.println("⏰ Reset air pump manual override (bat dau gio auto)");
      // KHÔNG return → Tiếp tục logic auto
    } else {
      // Ngoài giờ auto → Giữ manual
      return;
    }
  }
  if (shouldBeOn && !airPumpRunning) {
    digitalWrite(AIR_PUMP_PIN, HIGH);
    airPumpRunning = true;
    addHistory("Máy sục khí", "Bật tự động");
    Serial.println("🫧 Bat may suc khi (auto)");
    
  } else if (!shouldBeOn && airPumpRunning) {
    digitalWrite(AIR_PUMP_PIN, LOW);
    airPumpRunning = false;
    addHistory("Máy sục khí", "Tắt tự động");
    Serial.println("⏹ Tat may suc khi (auto)");
  }
}

// ==================== CẬP NHẬT LCD ====================
void updateLCD() {
  // Kiểm tra xem đã đến lúc đổi trang chưa
  if (millis() - lastLCDPageChange >= LCD_PAGE_INTERVAL) {
    lastLCDPageChange = millis();
    // Nếu có cảnh báo, hiển thị trang cảnh báo
    if (!systemOK) {
      currentLCDPage = 4;  // Trang cảnh báo
    } else {
      currentLCDPage = (currentLCDPage + 1) % 6;    
      if (currentLCDPage == 4) {
        currentLCDPage = 5;
      }
    }
    
    lcdTransitionEffect();
    lcd.clear();
    displayLCDPage(currentLCDPage);
  }
}

// chuyển trang
void lcdTransitionEffect() {
  lcd.noBacklight();
  delay(100);
  lcd.backlight();
}
//  HIỂN THỊ TỪNG TRANG LCD 
void displayLCDPage(int page) {
  switch(page) {
    case 0:
      // TRANG 1: TỔNG QUAN
      displayPage0_Overview();
      break;
      
    case 1:
      // TRANG 2: LED & MÁY SỤC KHÍ 
      displayPage1_Lights();
      break;
      
    case 2:
      // TRANG 3: LỊCH CHO ĂN 
      displayPage2_FeedSchedule();
      break;
      
    case 3:
      // TRANG 4: THỐNG KÊ
      displayPage3_Stats();
      break;
      
    case 4:
      //  TRANG 5: CẢNH BÁO 
      displayPage4_Warning();
      break;
      
    case 5: 
      //TRANG 6: THÔNG TIN MẠNG
      displayPage5_Network();
      break;
  }
}

// TRANG 0: TỔNG QUAN 
void displayPage0_Overview() {
  // Dòng 1: Nhiệt độ + Mực nước
  lcd.setCursor(0, 0);
  lcd.print("T:");
  lcd.print(temperature, 1);
  lcd.print("C W:");
  
  if (waterStatus == "LOW") {
    lcd.print("LOW");
  } else if (waterStatus == "HIGH") {
    lcd.print("HGH");
  } else {
    lcd.print("NOR");
  }
  
  // Dòng 2: Bơm lọc + Máy sục + Thời gian
  lcd.setCursor(0, 1);
  lcd.print("F:");
  lcd.print(pumpRunning ? "ON " : "OFF");
  lcd.print(" A:");
  lcd.print(airPumpRunning ? "ON " : "OF");
  
  if (timeValid) {
    lcd.print(" ");
    lcd.printf("%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min);
  }
}

// TRANG 1: LED & MÁY SỤC KHÍ 
void displayPage1_Lights() {
  lcd.setCursor(0, 0);
  
  if (!enableAutoLED) {
    lcd.print("LED: AUTO OFF  ");
  } else {
    lcd.print("LED:");
    lcd.print(ledCurrentState ? "ON " : "OFF");
    lcd.printf(" %02d-%02dh", ledOnHour, ledOffHour);
  } 

  lcd.setCursor(0, 1);
  
  if (!enableAutoAirPump) {  
    lcd.print("Air: AUTO OFF  ");
  } else {
    lcd.print("Air:");
    lcd.print(airPumpRunning ? "ON " : "OFF");
    
    if (airPumpOnHour == 0 && airPumpOffHour == 24) {
      lcd.print(" 24/7");
    } else {
      lcd.printf(" %02d-%02dh", airPumpOnHour, airPumpOffHour);
    }
  }
}
//  TRANG 2: LỊCH CHO ĂN 
void displayPage2_FeedSchedule() {
  if (!enableAutoFeed) {
    // Tắt auto → Hiển thị AUTO OFF
    lcd.setCursor(0, 0);
    lcd.print("Feed Schedule  ");
    lcd.setCursor(0, 1);
    lcd.print("   AUTO OFF    ");
    return;
  }
  lcd.setCursor(0, 0);
  lcd.print("Feed:");
  int count = 0;
  for (int i = 0; i < 10 && feedSchedule[i] != -1 && count < 4; i++) {
    if (i > 0) lcd.print(",");
    lcd.printf("%02d", feedSchedule[i]);
    count++;
  }
  if (count == 0) {
    lcd.print("None");
  }
  // Dòng 2: Giờ cho ăn tiếp theo
  lcd.setCursor(0, 1);
  lcd.print("Next:");
  if (timeValid) {
    int currentHour = timeinfo.tm_hour;
    int nextFeedHour = -1;
    // Tìm giờ cho ăn tiếp theo
    for (int i = 0; i < 10; i++) {
      if (feedSchedule[i] == -1) break;
      if (feedSchedule[i] > currentHour) {
        nextFeedHour = feedSchedule[i];
        break;
      }
    }
    if (nextFeedHour == -1 && feedSchedule[0] != -1) {
      nextFeedHour = feedSchedule[0];
      lcd.print("Tmr ");  
    } else {
      lcd.print("    ");
    }
    if (nextFeedHour != -1) {
      lcd.printf("%02d:00", nextFeedHour);
    } else {
      lcd.print("None");
    }
  } else {
    lcd.print("No Time");
  }
}

// TRANG 3: THỐNG KÊ 
void displayPage3_Stats() {
  lcd.setCursor(0, 0);
  if (!enableAutoFilter) {
    lcd.print("Filter:AUTO OFF");
  } else {
    lcd.print("Filter:");
    lcd.printf("%dh/%dm", filterIntervalHours, filterDurationMinutes);
  }
  lcd.setCursor(0, 1);
  lcd.print("Feeds:");
  lcd.print(totalFeedCount);
  lcd.print(" times");
}

// TRANG 4: CẢNH BÁO 
void displayPage4_Warning() {
  lcd.setCursor(0, 0);
  lcd.print("  !! WARNING !! ");
  
  lcd.setCursor(0, 1);
  
  if (waterStatus == "LOW") {
    lcd.print("  Water LOW !!  ");
  } else if (temperature > TEMP_HIGH) {
    lcd.print(" Temp HIGH ");
    lcd.print(temperature, 1);
    lcd.print("C");
  } else if (temperature < TEMP_LOW) {
    lcd.print(" Temp LOW ");
    lcd.print(temperature, 1);
    lcd.print("C");
  } else {
    lcd.print("  Check System  ");
  }
}
// TRANG 5: THÔNG TIN MẠNG 
void displayPage5_Network() {
  lcd.setCursor(0, 0);
  
  if (WiFi.status() == WL_CONNECTED) {
    lcd.print("WiFi:CONNECTED");
    
    lcd.setCursor(0, 1);
    lcd.print("IP:");
    
    IPAddress ip = WiFi.localIP();
    
    String ipStr = ip.toString();
    
    if (ipStr.length() <= 13) {
      lcd.print(ipStr);
    } else {
      lcd.printf("%d.%d.*.%d", ip[0], ip[1], ip[3]);
    }
    
  } else {
    lcd.print("WiFi:OFFLINE   ");
    lcd.setCursor(0, 1);
    lcd.print("Check Router!  ");
  }
}
// KẾT NỐI WIFI 
void connectWiFi() {
  Serial.print("Ket noi WiFi");
  WiFi.begin(ssid, password);
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n✓ Ket noi WiFi thanh cong!");
    Serial.print("IP: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("\n⚠ Khong ket noi duoc WiFi - Che do offline");
  }
}

//  ĐỒNG BỘ THỜI GIAN 
void syncTime() {
  configTime(7 * 3600, 0, "pool.ntp.org", "time.nist.gov");
  delay(2000);
  timeValid = getLocalTime(&timeinfo);
  lastTimeSync = millis();
  
  if (timeValid) {
    Serial.printf("✓ Thoi gian: %02d:%02d:%02d %02d/%02d/%04d\n",
                  timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec,
                  timeinfo.tm_mday, timeinfo.tm_mon + 1, timeinfo.tm_year + 1900);
  } else {
    Serial.println(" Khong dong bo duoc thoi gian NTP");
  }
}


// GHI LỊCH SỬ
void addHistory(String event, String value) {
  if (!timeValid) return;
  
  char timeStr[20];
  sprintf(timeStr, "%02d:%02d %02d/%02d", 
          timeinfo.tm_hour, timeinfo.tm_min,
          timeinfo.tm_mday, timeinfo.tm_mon + 1);
  
  history[historyIndex].time = String(timeStr);
  history[historyIndex].event = event;
  history[historyIndex].value = value;
  
  historyIndex = (historyIndex + 1) % 10; // Vòng lặp 0-9
}
void saveSettings() {
  // CÀI ĐẶT CƠ BẢN
  prefs.putInt("ledBright", ledBrightness);
  prefs.putUInt("ledColor", ledColor);
  prefs.putInt("ledOnHour", ledOnHour);
  prefs.putInt("ledOffHour", ledOffHour);
  // Lưu lịch cho ăn
  for (int i = 0; i < 10; i++) {
    String key = "feed" + String(i);
    prefs.putInt(key.c_str(), feedSchedule[i]);
  }
  // Lịch bơm lọc và máy sục
  prefs.putInt("filterInt", filterIntervalHours);
  prefs.putInt("filterDur", filterDurationMinutes);
  prefs.putInt("airOn", airPumpOnHour);
  prefs.putInt("airOff", airPumpOffHour);
  
  //  CHẾ ĐỘ TỰ ĐỘNG
  prefs.putBool("filterAuto", filterAutoMode);

 
  prefs.putBool("enableFeed", enableAutoFeed);
  prefs.putBool("enableFilter", enableAutoFilter);
  prefs.putBool("enableLED", enableAutoLED);
  prefs.putBool("enableAir", enableAutoAirPump); 
  // TRẠNG THÁI THIẾT BỊ 
  prefs.putBool("pumpState", pumpRunning);
  prefs.putBool("ledState", ledCurrentState);
  prefs.putBool("airState", airPumpRunning);
  Serial.println(" Da luu cai dat + trang thai");
}
void loadSettings() {
  // CÀI ĐẶT CƠ BẢN
  ledBrightness = prefs.getInt("ledBright", 128);
  ledColor = prefs.getUInt("ledColor", strip.Color(0, 100, 255));
  ledOnHour = prefs.getInt("ledOnHour", 18);
  ledOffHour = prefs.getInt("ledOffHour", 22);
  // Tải lịch cho ăn
  for (int i = 0; i < 10; i++) {
    String key = "feed" + String(i);
    feedSchedule[i] = prefs.getInt(key.c_str(), (i < 5) ? (7 + i * 3) : -1);
  }
  // Lịch bơm lọc và máy sục
  filterIntervalHours = prefs.getInt("filterInt", 1);
  filterDurationMinutes = prefs.getInt("filterDur", 30);
  airPumpOnHour = prefs.getInt("airOn", 0);
  airPumpOffHour = prefs.getInt("airOff", 24);
  // TẢI CHẾ ĐỘ TỰ ĐỘNG
  filterAutoMode = prefs.getBool("filterAuto", true);
  enableAutoFeed = prefs.getBool("enableFeed", true);
  enableAutoFilter = prefs.getBool("enableFilter", true);
  enableAutoLED = prefs.getBool("enableLED", true);
  enableAutoAirPump = prefs.getBool("enableAir", true);
  
  // TẢI TRẠNG THÁI THIẾT BỊ 
  pumpRunning = prefs.getBool("pumpState", false);
  ledCurrentState = prefs.getBool("ledState", false);
  airPumpRunning = prefs.getBool("airState", false);
 // TẢI LỊCH SỬ CHO ĂN 
  lastFeedTime = prefs.getString("lastFeed", "Chua co");
  lastAutoFeedMillis = prefs.getULong("lastFeedMs", 0);  // ← SỬA ĐÂY
  totalFeedCount = prefs.getULong("feedCount", 0);
  Serial.println(" Da tai cai dat + trang thai");
}

// ==================== WEB SERVER ====================
void setupWebServer() {
  // Trang chủ
  server.on("/", HTTP_GET, []() {
    server.send(200, "text/html", getHTMLPage());
  });

  // API lấy dữ liệu cảm biến
  server.on("/api/status", HTTP_GET, []() {
    StaticJsonDocument<512> doc;
    doc["temperature"] = temperature;
    doc["waterLevel"] = waterLevel;
    doc["waterStatus"] = waterStatus;
    doc["pumpRunning"] = pumpRunning;
    doc["systemOK"] = systemOK;
    doc["lastFeed"] = lastFeedTime;
    doc["ledBrightness"] = ledBrightness;
    doc["ledAuto"] = enableAutoLED;
    // Chuyển ledColor về dạng hex #RRGGBB
uint8_t r = (ledColor >> 16) & 0xFF;
uint8_t g = (ledColor >> 8) & 0xFF;
uint8_t b = ledColor & 0xFF;
char colorHex[8];
sprintf(colorHex, "#%02X%02X%02X", r, g, b);
doc["ledColorHex"] = colorHex;
doc["ledCurrentState"] = ledCurrentState;

// Máy sục khí
doc["airPumpRunning"] = airPumpRunning;


String output;
serializeJson(doc, output);
    server.send(200, "application/json", output);
  });

  // API điều khiển bơm
server.on("/api/pump", HTTP_GET, []() {
  if (!server.hasArg("action")) {
    server.send(400, "text/plain", "No action");
    return;
  }
  String action = server.arg("action");
  //  BẬT BƠM THỦ CÔNG 
  if (action == "on") {
    digitalWrite(RELAY_PIN, HIGH);
    pumpRunning = true;
    // Không thay đổi bất kỳ biến AUTO nào
    
    if (waterStatus == "LOW") {
      addHistory("Bơm lọc", "Bật thủ công (cảnh báo nước thấp)");
      saveSettings();
      server.send(200, "text/plain", "WARNING_LOW_WATER");
      Serial.println("⚠ Bat bom thu cong - Muc nuoc THAP!");
    } else {
      addHistory("Bơm lọc", "Bật thủ công");
      saveSettings();
      server.send(200, "text/plain", "Pump ON");
      Serial.println("✓ Bat bom thu cong (khong anh huong auto)");
    }
    return;
  }

  //  TẮT BƠM THỦ CÔNG 
  if (action == "off") {
    // Kiểm tra bảo vệ AUTO
    if (isFilterAutoRunning) {
      addHistory("Bơm lọc", "Từ chối tắt (auto đang chạy)");
      server.send(403, "text/plain", "AUTO_CYCLE_RUNNING");
      Serial.println("⚠ Tu choi tat bom - Dang trong chu ky AUTO!");
      return;
    }
    
    // Cho phép tắt
    digitalWrite(RELAY_PIN, LOW);
    pumpRunning = false;
    addHistory("Bơm lọc", "Tắt thủ công");
    saveSettings();
    server.send(200, "text/plain", "Pump OFF");
    Serial.println("✓ Tat bom thu cong (khong anh huong auto)");
    return;
  }
  server.send(400, "text/plain", "Invalid action");
});
  // API cho ăn thủ công
server.on("/api/feed", HTTP_GET, []() {
  feedFish(true);  // ← TRUE = manual feed
  server.send(200, "text/plain", "Fed");
});
// API điều khiển máy sục khí
server.on("/api/airpump", HTTP_GET, []() {
  if (!server.hasArg("action")) {
    server.send(400, "text/plain", "No action");
    return;
  }
  
  String action = server.arg("action");
  
  // KIỂM TRA XUNG ĐỘT VỚI AUTO 
  if (enableAutoAirPump && timeValid) {
    int currentHour = timeinfo.tm_hour;
    bool inAutoSchedule = (currentHour >= airPumpOnHour && currentHour < airPumpOffHour);
    
    if (inAutoSchedule) {
      addHistory("Máy sục khí", "Từ chối (auto đang chạy)");
      server.send(403, "text/plain", "AUTO_OVERRIDE");
      Serial.println(" Tu choi dieu khien thu cong - Dang trong gio AUTO");
      return;
    }
  }
  
  // THỰC HIỆN ĐIỀU KHIỂN THỦ CÔNG
  if (action == "on") {
    digitalWrite(AIR_PUMP_PIN, HIGH);
    airPumpRunning = true;
    lastManualControl = millis();
     isManualOverrideAirPump = true;
    addHistory("Máy sục khí", "Bật thủ công");
    saveSettings();
    server.send(200, "text/plain", "Air pump ON");
    Serial.println("✓ May suc khi BAT (thu cong)");
    
  } else if (action == "off") {
    digitalWrite(AIR_PUMP_PIN, LOW);
    airPumpRunning = false;
    lastManualControl = millis();
     isManualOverrideAirPump = true;
    addHistory("Máy sục khí", "Tắt thủ công");
    saveSettings();
    server.send(200, "text/plain", "Air pump OFF");
    Serial.println("✓ May suc khi TAT (thu cong)");
    
  } else {
    server.send(400, "text/plain", "Invalid action");
  }
});
  // API điều khiển LED
server.on("/api/led", HTTP_GET, []() {
  // XỬ LÝ ĐỘ SÁNG 
  if (server.hasArg("brightness")) {
    ledBrightness = server.arg("brightness").toInt();
    strip.setBrightness(ledBrightness);
    if (ledCurrentState) {
      for (int i = 0; i < NUM_LEDS; i++) {
        strip.setPixelColor(i, ledColor);
      }
      strip.show();
    }
    saveSettings();
  }
  //  XỬ LÝ MÀU SẮC 
  if (server.hasArg("color")) {
    String color = server.arg("color");
    long number = strtol(&color[1], NULL, 16);
    int r = number >> 16;
    int g = (number >> 8) & 0xFF;
    int b = number & 0xFF;
    ledColor = strip.Color(r, g, b); 
    if (ledCurrentState) {
      for (int i = 0; i < NUM_LEDS; i++) {
        strip.setPixelColor(i, ledColor);
      }
      strip.show();
    }
    saveSettings();
  }
  //  XỬ LÝ BẬT/TẮT THỦ CÔNG 
  if (server.hasArg("manual")) {
    String state = server.arg("manual");
    if (enableAutoLED && timeValid) {
      int currentHour = timeinfo.tm_hour;
      bool inAutoSchedule = (currentHour >= ledOnHour && currentHour < ledOffHour);
      
      if (inAutoSchedule) {
        //  ĐANG TRONG GIỜ AUTO → CHỈ CHẶN LỆNH TẮT
        if (state == "off") {
          server.send(403, "text/plain", "AUTO_OVERRIDE_OFF");
          Serial.println("⚠ Tu choi TAT LED - Dang trong gio AUTO");
          return;
        }
      }
    }
    
    // THỰC HIỆN LỆNH BẬT/TẮT 
    if (state == "on") {
      for (int i = 0; i < NUM_LEDS; i++) {
        strip.setPixelColor(i, ledColor);
      }
      strip.show();
      ledCurrentState = true;
     isManualOverrideLED = true;  
      lastManualControl = millis();
      addHistory("LED", "Bật thủ công");
      saveSettings();
      
    } else if (state == "off") {
      strip.clear();
      strip.show();
      ledCurrentState = false;
    isManualOverrideLED = true; 
      lastManualControl = millis();
      addHistory("LED", "Tắt thủ công");
      saveSettings();
    }
  }
  
  server.send(200, "text/plain", "OK");
});
// API lấy lịch sử
  server.on("/api/history", HTTP_GET, []() {
    StaticJsonDocument<1024> doc;
    JsonArray arr = doc.createNestedArray("history");
    
    for (int i = 0; i < 10; i++) {
      if (history[i].time == "") continue;
      JsonObject obj = arr.add<JsonObject>();
      obj["time"] = history[i].time;
      obj["event"] = history[i].event;
      obj["value"] = history[i].value;
    }
    
    String output;
    serializeJson(doc, output);
    server.send(200, "application/json", output);
  });

  // API lấy thống kê
  server.on("/api/stats", HTTP_GET, []() {
    StaticJsonDocument<512> doc;
    doc["totalFeedCount"] = totalFeedCount;
    doc["totalFilterTime"] = totalFilterTime / 1000; // giây
    doc["tempMin"] = tempMin;
    doc["tempMax"] = tempMax;
    doc["enableAutoFeed"] = enableAutoFeed;
    doc["enableAutoFilter"] = enableAutoFilter;
    doc["enableAutoLED"] = enableAutoLED;
    doc["enableAutoAirPump"] = enableAutoAirPump; 
    doc["filterAutoMode"] = filterAutoMode;
   doc["ledAutoMode"] = enableAutoLED; 
    doc["ledOnHour"] = ledOnHour;
    doc["ledOffHour"] = ledOffHour;
    // Lịch bơm lọc
doc["filterIntervalHours"] = filterIntervalHours;
doc["filterDurationMinutes"] = filterDurationMinutes;
// Máy sục khí
doc["airPumpOnHour"] = airPumpOnHour;
doc["airPumpOffHour"] = airPumpOffHour;
    // Lịch cho ăn
    JsonArray feedArr = doc.createNestedArray("feedSchedule");
    for (int i = 0; i < 10; i++) {
      if (feedSchedule[i] != -1) {
        feedArr.add(feedSchedule[i]);
      }
    }
    
    String output;
    serializeJson(doc, output);
    server.send(200, "application/json", output);
  });

// API cài đặt tự động
server.on("/api/settings", HTTP_GET, []() {
  if (server.hasArg("autoFeed")) {
    enableAutoFeed = (server.arg("autoFeed") == "true");
    saveSettings();
  }
  
  if (server.hasArg("autoFilter")) {
    enableAutoFilter = (server.arg("autoFilter") == "true");
    saveSettings();
    // KHÔNG cần reset manual nữa
  }
  
  if (server.hasArg("autoLED")) {
    enableAutoLED = (server.arg("autoLED") == "true");
    saveSettings();
  }
  
  if (server.hasArg("autoAirPump")) {
    enableAutoAirPump = (server.arg("autoAirPump") == "true");
    saveSettings();
    // KHÔNG cần reset manual nữa
  }
  
  server.send(200, "text/plain", "OK");
});
  // API cập nhật lịch
  server.on("/api/schedule", HTTP_GET, []() {
    if (server.hasArg("feed")) {
      String feedStr = server.arg("feed");
      int idx = 0;
      int lastPos = 0;
      
      for (int i = 0; i < 10; i++) feedSchedule[i] = -1;
      
      for (int i = 0; i <= feedStr.length(); i++) {
        if (i == feedStr.length() || feedStr[i] == ',') {
          if (i > lastPos) {
            feedSchedule[idx++] = feedStr.substring(lastPos, i).toInt();
          }
          lastPos = i + 1;
        }
      }
      saveSettings();
    }
    
    if (server.hasArg("ledOn")) {
      ledOnHour = server.arg("ledOn").toInt();
      saveSettings();
    }
    
    if (server.hasArg("ledOff")) {
      ledOffHour = server.arg("ledOff").toInt();
      saveSettings();
    }
    if (server.hasArg("filterInterval")) {
  filterIntervalHours = server.arg("filterInterval").toInt();
  if (filterIntervalHours < 1) filterIntervalHours = 1;
  if (filterIntervalHours > 24) filterIntervalHours = 24;
}

if (server.hasArg("filterDuration")) {
  filterDurationMinutes = server.arg("filterDuration").toInt();
  if (filterDurationMinutes < 5) filterDurationMinutes = 5;
  if (filterDurationMinutes > 120) filterDurationMinutes = 120;
}
// Lịch máy sục khí
if (server.hasArg("airPumpOn")) {
  airPumpOnHour = server.arg("airPumpOn").toInt();
  if (airPumpOnHour < 0) airPumpOnHour = 0;
  if (airPumpOnHour > 23) airPumpOnHour = 23;
  saveSettings();
}

if (server.hasArg("airPumpOff")) {
  airPumpOffHour = server.arg("airPumpOff").toInt();
  if (airPumpOffHour < 1) airPumpOffHour = 1;
  if (airPumpOffHour > 24) airPumpOffHour = 24;
  saveSettings();
}
    server.send(200, "text/plain", "OK");
  });
}

// ==================== HTML PAGE ====================
String getHTMLPage() {
  String html = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>Smart Aquarium Pro</title>
<style>
* { margin: 0; padding: 0; box-sizing: border-box; }

:root {
  --primary: #667eea;
  --primary-dark: #5568d3;
  --secondary: #764ba2;
  --success: #38ef7d;
  --danger: #f45c43;
  --warning: #ffa726;
  --dark: #1a1a2e;
  --light: #f8f9fa;
  --border-radius: 15px;
}

body {
  font-family: 'Segoe UI', system-ui, sans-serif;
  background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
  min-height: 100vh;
  padding: 10px;
  color: #333;
}

/* ========== HEADER ========== */
.header {
  text-align: center;
  padding: 30px 20px;
  margin-bottom: 20px;
  background: rgba(255,255,255,0.15);
  backdrop-filter: blur(20px);
  border-radius: var(--border-radius);
  border: 1px solid rgba(255,255,255,0.3);
  box-shadow: 0 8px 32px rgba(0,0,0,0.1);
}

.header h1 {
  color: white;
  font-size: 2.5em;
  font-weight: 700;
  text-shadow: 0 2px 10px rgba(0,0,0,0.3);
  margin-bottom: 10px;
}

.header .subtitle {
  color: rgba(255,255,255,0.95);
  font-size: 1.1em;
  font-weight: 300;
}

.header .system-time {
  margin-top: 15px;
  padding: 10px 20px;
  background: rgba(0,0,0,0.2);
  border-radius: 25px;
  display: inline-block;
  color: white;
  font-weight: 600;
}

/* ========== TABS ========== */
.tabs {
  display: flex;
  gap: 10px;
  margin-bottom: 20px;
  overflow-x: auto;
  padding: 5px;
  background: rgba(255,255,255,0.1);
  backdrop-filter: blur(10px);
  border-radius: var(--border-radius);
}

.tab {
  padding: 15px 30px;
  background: rgba(255,255,255,0.2);
  border: none;
  border-radius: 10px;
  color: white;
  font-weight: 600;
  cursor: pointer;
  transition: all 0.3s;
  white-space: nowrap;
}

.tab:hover {
  background: rgba(255,255,255,0.3);
}

.tab.active {
  background: white;
  color: var(--primary);
  box-shadow: 0 4px 15px rgba(0,0,0,0.2);
}

/* ========== CONTAINER ========== */
.container {
  max-width: 1400px;
  margin: 0 auto;
}

.tab-content {
  display: none;
}

.tab-content.active {
  display: block;
  animation: fadeIn 0.5s;
}

@keyframes fadeIn {
  from { opacity: 0; transform: translateY(20px); }
  to { opacity: 1; transform: translateY(0); }
}

/* ========== GRID ========== */
.grid {
  display: grid;
  grid-template-columns: repeat(auto-fit, minmax(320px, 1fr));
  gap: 20px;
  margin-bottom: 20px;
}

.grid-2 {
  grid-template-columns: repeat(auto-fit, minmax(400px, 1fr));
}

/* ========== CARD ========== */
.card {
  background: white;
  border-radius: var(--border-radius);
  padding: 25px;
  box-shadow: 0 8px 32px rgba(0,0,0,0.1);
  transition: all 0.3s;
}

.card:hover {
  transform: translateY(-5px);
  box-shadow: 0 12px 40px rgba(0,0,0,0.15);
}

.card-header {
  display: flex;
  align-items: center;
  margin-bottom: 20px;
  padding-bottom: 15px;
  border-bottom: 2px solid #e0e0e0;
}

.card-icon {
  font-size: 2em;
  margin-right: 15px;
}

.card-title {
  font-size: 1.4em;
  font-weight: 600;
  color: #2c3e50;
  flex: 1;
}

.card-badge {
  padding: 5px 15px;
  border-radius: 20px;
  font-size: 0.85em;
  font-weight: 600;
}

.badge-auto {
  background: linear-gradient(135deg, var(--success), #11998e);
  color: white;
}

.badge-manual {
  background: linear-gradient(135deg, var(--warning), #f57c00);
  color: white;
}

/* ========== SENSOR ========== */
.sensor-grid {
  display: grid;
  grid-template-columns: repeat(auto-fit, minmax(200px, 1fr));
  gap: 15px;
  margin-bottom: 20px;
}

.sensor-item {
  background: linear-gradient(135deg, var(--primary), var(--secondary));
  padding: 20px;
  border-radius: 12px;
  text-align: center;
  color: white;
  position: relative;
  overflow: hidden;
}

.sensor-item::before {
  content: '';
  position: absolute;
  top: -50%;
  right: -50%;
  width: 200%;
  height: 200%;
  background: radial-gradient(circle, rgba(255,255,255,0.1) 0%, transparent 70%);
  animation: pulse 3s infinite;
}

@keyframes pulse {
  0%, 100% { transform: scale(1); opacity: 0.5; }
  50% { transform: scale(1.1); opacity: 0.8; }
}

.sensor-label {
  font-size: 0.9em;
  opacity: 0.95;
  margin-bottom: 10px;
  font-weight: 500;
  text-transform: uppercase;
  letter-spacing: 1px;
}

.sensor-value {
  font-size: 2.5em;
  font-weight: 700;
  text-shadow: 0 2px 8px rgba(0,0,0,0.2);
  position: relative;
  z-index: 1;
}

.sensor-range {
  font-size: 0.8em;
  margin-top: 8px;
  opacity: 0.9;
}

/* ========== STATUS ========== */
.status-grid {
  display: flex;
  flex-wrap: wrap;
  gap: 10px;
  margin: 15px 0;
}

.status {
  display: inline-flex;
  align-items: center;
  padding: 10px 16px;
  border-radius: 25px;
  font-weight: 600;
  font-size: 0.9em;
  box-shadow: 0 2px 8px rgba(0,0,0,0.1);
  position: relative;
  overflow: hidden;
}

.status::before {
  content: '';
  width: 8px;
  height: 8px;
  border-radius: 50%;
  margin-right: 8px;
  animation: blink 2s infinite;
}

@keyframes blink {
  0%, 100% { opacity: 1; }
  50% { opacity: 0.3; }
}

.status.ok {
  background: linear-gradient(135deg, #11998e, var(--success));
  color: white;
}

.status.ok::before { background: white; }

.status.warning {
  background: linear-gradient(135deg, var(--warning), #ff6f00);
  color: white;
}

.status.warning::before { background: white; }

.status.error {
  background: linear-gradient(135deg, var(--danger), #d32f2f);
  color: white;
  animation: shake 0.5s infinite;
}

.status.error::before { background: white; }

@keyframes shake {
  0%, 100% { transform: translateX(0); }
  25% { transform: translateX(-3px); }
  75% { transform: translateX(3px); }
}

/* ========== BUTTONS ========== */
.btn-group {
  display: grid;
  gap: 12px;
  margin-top: 15px;
}

button {
  padding: 15px 20px;
  border: none;
  border-radius: 12px;
  font-size: 1em;
  font-weight: 600;
  cursor: pointer;
  transition: all 0.3s;
  box-shadow: 0 4px 15px rgba(0,0,0,0.1);
  display: flex;
  align-items: center;
  justify-content: center;
  gap: 10px;
}

button:active {
  transform: scale(0.98);
}

.btn-primary {
  background: linear-gradient(135deg, var(--primary), var(--secondary));
  color: white;
}

.btn-primary:hover {
  box-shadow: 0 6px 20px rgba(102, 126, 234, 0.4);
  transform: translateY(-2px);
}

.btn-success {
  background: linear-gradient(135deg, #11998e, var(--success));
  color: white;
}

.btn-danger {
  background: linear-gradient(135deg, var(--danger), #d32f2f);
  color: white;
}

.btn-warning {
  background: linear-gradient(135deg, var(--warning), #ff6f00);
  color: white;
}

/* ========== FORM ========== */
.form-group {
  margin: 15px 0;
}

.form-label {
  display: flex;
  justify-content: space-between;
  margin-bottom: 8px;
  font-weight: 600;
  color: #2c3e50;
}

input[type="range"] {
  width: 100%;
  height: 8px;
  border-radius: 5px;
  background: #e0e0e0;
  outline: none;
  -webkit-appearance: none;
}

input[type="range"]::-webkit-slider-thumb {
  -webkit-appearance: none;
  width: 20px;
  height: 20px;
  border-radius: 50%;
  background: linear-gradient(135deg, var(--primary), var(--secondary));
  cursor: pointer;
  box-shadow: 0 2px 8px rgba(0,0,0,0.2);
}

input[type="color"] {
  width: 100%;
  height: 80px;  /* Tăng chiều cao */
  padding: 5px;
  border: 3px solid #e0e0e0;
  border-radius: 12px;
  cursor: pointer;
  transition: all 0.3s;
}

input[type="color"]:hover {
  border-color: var(--primary);
  transform: scale(1.02);
}

input[type="color"]::-webkit-color-swatch-wrapper {
  padding: 0;
}

input[type="color"]::-webkit-color-swatch {
  border: none;
  border-radius: 8px;
}

/* Các input còn lại */
input[type="number"],
input[type="text"],
select {
  width: 100%;
  padding: 12px;
  border: 2px solid #e0e0e0;
  border-radius: 10px;
  font-size: 1em;
  transition: all 0.3s;
}

input:focus, select:focus {
  outline: none;
  border-color: var(--primary);
  box-shadow: 0 0 0 3px rgba(102, 126, 234, 0.1);
}

.toggle-switch {
  position: relative;
  display: inline-block;
  width: 60px;
  height: 30px;
}

.toggle-switch input {
  opacity: 0;
  width: 0;
  height: 0;
}

.slider {
  position: absolute;
  cursor: pointer;
  top: 0;
  left: 0;
  right: 0;
  bottom: 0;
  background-color: #ccc;
  transition: .4s;
  border-radius: 30px;
}

.slider:before {
  position: absolute;
  content: "";
  height: 22px;
  width: 22px;
  left: 4px;
  bottom: 4px;
  background-color: white;
  transition: .4s;
  border-radius: 50%;
}

input:checked + .slider {
  background: linear-gradient(135deg, var(--primary), var(--secondary));
}

input:checked + .slider:before {
  transform: translateX(30px);
}

/* ========== INFO ITEM ========== */
.info-item {
  background: #f8f9fa;
  padding: 15px;
  border-radius: 10px;
  margin: 10px 0;
  display: flex;
  justify-content: space-between;
  align-items: center;
}

.info-label {
  font-weight: 600;
  color: #495057;
}

.info-value {
  font-weight: 700;
  color: var(--primary);
  font-size: 1.1em;
}

/* ========== CAMERA ========== */
.camera-card {
  grid-column: 1 / -1;
  background: rgba(0,0,0,0.95);
  padding: 25px;
}

.camera-card .card-header {
  border-bottom-color: rgba(255,255,255,0.2);
}

.camera-card .card-title {
  color: white;
}

.camera-container {
  position: relative;
  width: 100%;
  padding-bottom: 75%;
  background: #000;
  border-radius: 15px;
  overflow: hidden;
  box-shadow: 0 8px 32px rgba(0,0,0,0.5);
}

.camera-container img {
  position: absolute;
  top: 0;
  left: 0;
  width: 100%;
  height: 100%;
  object-fit: contain;
  background: #000;
}

.camera-controls {
  display: flex;
  gap: 10px;
  margin-top: 15px;
  flex-wrap: wrap;
}

/* ========== HISTORY ========== */
.history-list {
  max-height: 400px;
  overflow-y: auto;
}

.history-item {
  display: flex;
  align-items: center;
  padding: 15px;
  background: #f8f9fa;
  border-radius: 10px;
  margin-bottom: 10px;
  border-left: 4px solid var(--primary);
}

.history-time {
  font-weight: 600;
  color: var(--primary);
  min-width: 100px;
}

.history-event {
  flex: 1;
  margin: 0 15px;
  font-weight: 600;
}

.history-value {
  color: #6c757d;
  font-size: 0.9em;
}

/* ========== STATS ========== */
.stats-grid {
  display: grid;
  grid-template-columns: repeat(auto-fit, minmax(200px, 1fr));
  gap: 15px;
}

.stat-card {
  background: linear-gradient(135deg, #f093fb 0%, #f5576c 100%);
  padding: 20px;
  border-radius: 12px;
  color: white;
  text-align: center;
}

.stat-card.blue {
  background: linear-gradient(135deg, #4facfe 0%, #00f2fe 100%);
}

.stat-card.green {
  background: linear-gradient(135deg, #43e97b 0%, #38f9d7 100%);
}

.stat-card.orange {
  background: linear-gradient(135deg, #fa709a 0%, #fee140 100%);
}

.stat-label {
  font-size: 0.9em;
  opacity: 0.9;
  margin-bottom: 10px;
  text-transform: uppercase;
  letter-spacing: 1px;
}

.stat-value {
  font-size: 2.5em;
  font-weight: 700;
  text-shadow: 0 2px 8px rgba(0,0,0,0.2);
}

/* ========== SCHEDULE ========== */
.schedule-grid {
  display: grid;
  grid-template-columns: repeat(auto-fill, minmax(80px, 1fr));
  gap: 10px;
}

.schedule-item {
  background: #f8f9fa;
  padding: 15px 10px;
  border-radius: 10px;
  text-align: center;
  font-weight: 600;
  font-size: 1.2em;
  color: var(--primary);
  border: 2px solid #e0e0e0;
}

.schedule-item.active {
  background: linear-gradient(135deg, var(--primary), var(--secondary));
  color: white;
  border-color: var(--primary);
}

/* ========== RESPONSIVE ========== */
@media (max-width: 768px) {
  .header h1 { font-size: 1.8em; }
  .grid { grid-template-columns: 1fr; }
  .sensor-grid { grid-template-columns: 1fr; }
  .tabs { justify-content: flex-start; }
  .tab { padding: 12px 20px; font-size: 0.9em; }
}

/* ========== NOTIFICATION ========== */
.notification {
  position: fixed;
  top: 20px;
  right: 20px;
  padding: 15px 25px;
  background: white;
  border-radius: 10px;
  box-shadow: 0 8px 32px rgba(0,0,0,0.2);
  display: none;
  animation: slideIn 0.3s;
  z-index: 1000;
  max-width: 400px;
}

@keyframes slideIn {
  from { transform: translateX(400px); opacity: 0; }
  to { transform: translateX(0); opacity: 1; }
}

.notification.show { display: block; }
.notification.success { border-left: 4px solid var(--success); }
.notification.error { border-left: 4px solid var(--danger); }
.notification.warning { border-left: 4px solid var(--warning); }
</style>
</head>
<body>

<div class="container">
  <!-- Header -->
  <div class="header">
    <h1>🐠 2GEN AQUARIUM</h1>
    <div class="subtitle">Hệ thống giám sát & điều khiển tự động</div>
    <div class="system-time" id="systemTime">--:--:--</div>
  </div>

  <!-- Tabs -->
  <div class="tabs">
    <button class="tab active" onclick="showTab('dashboard')">📊 Tổng Quan</button>
    <button class="tab" onclick="showTab('control')">🎮 Điều Khiển</button>
    <button class="tab" onclick="showTab('schedule')">📅 Lịch Trình</button>
    <button class="tab" onclick="showTab('camera')">📷 Camera</button>
    <button class="tab" onclick="showTab('history')">📜 Lịch Sử</button>
    <button class="tab" onclick="showTab('settings')">⚙️ Cài Đặt</button>
  </div>

  <!-- TAB 1: DASHBOARD -->
  <div id="tab-dashboard" class="tab-content active">
    <div class="grid">
      <!-- Cảm biến -->
      <div class="card">
        <div class="card-header">
          <div class="card-icon">🌡️</div>
          <div class="card-title">Môi Trường</div>
        </div>
        <div class="sensor-grid">
          <div class="sensor-item">
            <div class="sensor-label">Nhiệt Độ</div>
            <div class="sensor-value" id="temp">--°C</div>
            <div class="sensor-range">Min: <span id="tempMin">--</span> | Max: <span id="tempMax">--</span></div>
          </div>
          <div class="sensor-item">
            <div class="sensor-label">Mực Nước</div>
            <div class="sensor-value" id="water">--</div>
            <div class="sensor-range">ADC: <span id="waterADC">--</span></div>
          </div>
        </div>
        <div class="status-grid" id="status"></div>
      </div>

      <!-- Thống kê -->
      <div class="card">
        <div class="card-header">
          <div class="card-icon">📈</div>
          <div class="card-title">Thống Kê</div>
        </div>
        <div class="stats-grid">
          <div class="stat-card blue">
            <div class="stat-label">Bơm Lọc</div>
            <div class="stat-value" id="statPump">0h</div>
          </div>
          <div class="stat-card green">
            <div class="stat-label">Cho Ăn</div>
            <div class="stat-value" id="statFeed">0</div>
          </div>
        </div>
      </div>
    </div>

    <!-- Trạng thái thiết bị -->
    <div class="grid grid-2">
      <div class="card">
        <div class="card-header">
          <div class="card-icon">💧</div>
          <div class="card-title">Bơm Lọc</div>
          <span class="card-badge badge-auto" id="pumpMode">TỰ ĐỘNG</span>
        </div>
        <div class="info-item">
          <span class="info-label">Trạng thái:</span>
          <span class="info-value" id="pumpStatus">--</span>
        </div>
        <div class="info-item">
          <span class="info-label">Lần chạy cuối:</span>
          <span class="info-value" id="lastPumpRun">--</span>
        </div>
      </div>

      <div class="card">
        <div class="card-header">
          <div class="card-icon">🍽️</div>
          <div class="card-title">Cho Ăn</div>
          <span class="card-badge badge-auto" id="feedMode">TỰ ĐỘNG</span>
        </div>
        <div class="info-item">
          <span class="info-label">Lần cho ăn cuối:</span>
          <span class="info-value" id="lastFeed">Chưa có</span>
        </div>
        <div class="info-item">
          <span class="info-label">Lịch hôm nay:</span>
          <span class="info-value" id="todaySchedule">--</span>
        </div>
      </div>

      <div class="card">
        <div class="card-header">
          <div class="card-icon">💡</div>
          <div class="card-title">LED RGB</div>
          <span class="card-badge badge-auto" id="ledMode">TỰ ĐỘNG</span>
        </div>
        <div class="info-item">
          <span class="info-label">Trạng thái:</span>
          <span class="info-value" id="ledStatus">--</span>
        </div>
        <div class="info-item">
          <span class="info-label">Lịch bật:</span>
          <span class="info-value" id="ledSchedule">--</span>
        </div>
      </div>
      <!-- Máy Sục Khí -->
<div class="card">
  <div class="card-header">
    <div class="card-icon">🫧</div>
    <div class="card-title">Máy Sục Khí</div>
    <span class="card-badge badge-auto" id="airPumpMode">TỰ ĐỘNG</span>
  </div>
  <div class="info-item">
    <span class="info-label">Trạng thái:</span>
    <span class="info-value" id="airPumpStatus">--</span>
  </div>
  <div class="info-item">
    <span class="info-label">Lịch hoạt động:</span>
    <span class="info-value" id="airPumpSchedule">--</span>
  </div>
</div>
    </div>
  </div>

  <!-- TAB 2: CONTROL -->
<!-- TAB 2: CONTROL -->
<div id="tab-control" class="tab-content">
  <div class="grid">
    <div class="card">
      <div class="card-header">
        <div class="card-icon">💧</div>
        <div class="card-title">Điều Khiển Bơm</div>
      </div>
      <p style="color: #6c757d; margin-bottom: 15px; font-size: 0.9em;">
        ⚠️ Điều khiển thủ công. Để bật chế độ tự động, vào tab <strong>Lịch Trình</strong>.
      </p>
      <div class="btn-group">
        <button class="btn-success" onclick="controlPump('on')">▶️ BẬT BƠM</button>
        <button class="btn-danger" onclick="controlPump('off')">⏸️ TẮT BƠM</button>
      </div>
    </div>

      <div class="card">
        <div class="card-header">
          <div class="card-icon">🍽️</div>
          <div class="card-title">Cho Cá Ăn</div>
        </div>
        <div class="btn-group">
          <button class="btn-primary" onclick="feedNow()">🐟 CHO ĂN NGAY</button>
        </div>
      </div>
<!-- Máy Sục Khí -->
<div class="card">
  <div class="card-header">
    <div class="card-icon">🫧</div>
    <div class="card-title">Máy Sục Khí</div>
  </div>
  <p style="color: #6c757d; margin-bottom: 15px; font-size: 0.9em;">
    ⚠️ Điều khiển thủ công. Để bật chế độ tự động, vào tab <strong>Lịch Trình</strong>.
  </p>
  <div class="btn-group">
    <button class="btn-success" onclick="controlAirPump('on')">▶️ BẬT MÁY SỤC</button>
    <button class="btn-danger" onclick="controlAirPump('off')">⏸️ TẮT MÁY SỤC</button>
  </div>
</div>
<div class="card">
  <div class="card-header">
    <div class="card-icon">💡</div>
    <div class="card-title">Điều Khiển LED</div>
  </div>

  

 
  <!-- Độ sáng -->
  <div class="form-group">
    <div class="form-label">
      <span>Độ sáng</span>
      <span id="brightVal">128</span>
    </div>
    <input type="range" min="0" max="255" value="128" id="brightness" oninput="updateBrightnessDisplay()">
  </div>
  
  <!-- Màu sắc -->
  <div class="form-group">
    <div class="form-label">
      <span>Màu sắc</span>
      <span id="colorPreview" style="display:inline-block;width:30px;height:30px;border-radius:50%;background:#0064ff;border:2px solid #fff;box-shadow:0 2px 5px rgba(0,0,0,0.2);vertical-align:middle;"></span>
    </div>
    <input type="color" id="color" value="#0064ff" oninput="updateColorPreview()">
  </div>
  
  <!-- Nút áp dụng màu và độ sáng -->
  <button class="btn-primary" onclick="applyLEDSettings()">
    ✨ ÁP DỤNG MÀU & ĐỘ SÁNG
  </button>
  
 <!-- Điều khiển bật/tắt -->
<p style="color: #6c757d; margin: 15px 0; font-size: 0.9em;">
  ⚠️ Điều khiển thủ công. Để bật chế độ tự động, vào tab <strong>Lịch Trình</strong>.
</p>
<div class="btn-group">
  <button class="btn-success" onclick="ledManual('on')">💡 BẬT ĐÈN</button>
  <button class="btn-danger" onclick="ledManual('off')">🌙 TẮT ĐÈN</button>
</div>
</div>
    </div>
  </div>

  <!-- TAB 3: SCHEDULE -->
  <div id="tab-schedule" class="tab-content">
    <div class="grid">
      <div class="card">
        <div class="card-header">
          <div class="card-icon">🍽️</div>
          <div class="card-title">Lịch Cho Ăn</div>
        </div>
        <!-- ===== THÊM TOGGLE AUTO ===== -->
  <div class="info-item" style="background: #e8f5e9; border-left: 4px solid var(--success);">
    <span class="info-label">🔄 Chế độ tự động</span>
    <label class="toggle-switch">
      <input type="checkbox" id="toggleFeedAuto" onchange="toggleAutoModeFromSchedule('feed', this.checked)">
      <span class="slider"></span>
    </label>
  </div>
        <div class="schedule-grid" id="feedScheduleDisplay"></div>
        <div class="form-group" style="margin-top: 20px;">
          <label class="form-label">Thêm giờ mới (0-23)</label>
          <input type="number" id="newFeedHour" min="0" max="23" placeholder="VD: 14">
        </div>
        <button class="btn-primary" onclick="updateFeedSchedule()">💾 Cập Nhật Lịch</button>
      </div>
     <!-- Lịch Bơm Lọc -->
<div class="card">
  <div class="card-header">
    <div class="card-icon">💧</div>
    <div class="card-title">Lịch Bơm Lọc</div>
  </div>
  
  <!-- ===== THÊM TOGGLE AUTO ===== -->
  <div class="info-item" style="background: #e8f5e9; border-left: 4px solid var(--success);">
    <span class="info-label">🔄 Chế độ tự động</span>
    <label class="toggle-switch">
      <input type="checkbox" id="toggleFilterAuto" onchange="toggleAutoModeFromSchedule('filter', this.checked)">
      <span class="slider"></span>
    </label>
  </div>
  <div class="form-group">
    <label class="form-label">Lọc mỗi bao nhiêu giờ? (1-24)</label>
   <input type="number" id="filterInterval" min="1" max="24" value="1" oninput="markFilterEditing()">
    <small style="color: #6c757d; display: block; margin-top: 5px;">
      VD: Nhập "2" = Lọc mỗi 2 tiếng 1 lần
    </small>
  </div>
  
  <div class="form-group">
    <label class="form-label">Chạy trong bao nhiêu phút? (5-120)</label>
   <input type="number" id="filterDuration" min="5" max="120" value="30" oninput="markFilterEditing()">
    <small style="color: #6c757d; display: block; margin-top: 5px;">
      VD: Nhập "30" = Mỗi lần chạy 30 phút
    </small>
  </div>
  
  <div class="info-item">
    <span class="info-label">📋 Lịch hiện tại:</span>
    <span class="info-value" id="currentFilterSchedule">Mỗi 1h, chạy 30 phút</span>
  </div>
  
  <button class="btn-primary" onclick="updateFilterSchedule()">
    💾 Cập Nhật Lịch Bơm
  </button>
</div>
      <div class="card">
        <div class="card-header">
          <div class="card-icon">💡</div>
          <div class="card-title">Lịch LED</div>
        </div>
          <!-- ===== THÊM TOGGLE AUTO ===== -->
  <div class="info-item" style="background: #e8f5e9; border-left: 4px solid var(--success);">
    <span class="info-label">🔄 Chế độ tự động</span>
    <label class="toggle-switch">
      <input type="checkbox" id="toggleLEDAutoSchedule" onchange="toggleAutoModeFromSchedule('led', this.checked)">
      <span class="slider"></span>
    </label>
  </div>
        <div class="form-group">
          <label class="form-label">Giờ bật (0-23)</label>
         <input type="number" id="ledOnHour" min="0" max="23" value="18" oninput="markLEDScheduleEditing()">
        </div>
        <div class="form-group">
          <label class="form-label">Giờ tắt (0-23)</label>
        <input type="number" id="ledOffHour" min="0" max="23" value="22" oninput="markLEDScheduleEditing()">
        </div>
        <button class="btn-primary" onclick="updateLEDSchedule()">💾 Cập Nhật Lịch LED</button>
      </div>
      <!-- Lịch Máy Sục Khí -->
<!-- Lịch Máy Sục Khí -->
<div class="card">
  <div class="card-header">
    <div class="card-icon">🫧</div>
    <div class="card-title">Lịch Máy Sục Khí</div>
  </div>
  
  <!-- ===== THÊM TOGGLE AUTO ===== -->
  <div class="info-item" style="background: #e8f5e9; border-left: 4px solid var(--success);">
    <span class="info-label">🔄 Chế độ tự động</span>
    <label class="toggle-switch">
      <input type="checkbox" id="toggleAirPumpAuto" onchange="toggleAutoModeFromSchedule('airpump', this.checked)">
      <span class="slider"></span>
    </label>
  </div>
  
  <div class="form-group">
    <label class="form-label">Giờ bật (0-23)</label>
    <input type="number" id="airPumpOnHour" min="0" max="23" value="0" oninput="markAirPumpScheduleEditing()">
    <small style="color: #6c757d; display: block; margin-top: 5px;">
      Để 0 = Luôn bật cả ngày
    </small>
  </div>
  <div class="form-group">
    <label class="form-label">Giờ tắt (1-24)</label>
    <input type="number" id="airPumpOffHour" min="1" max="24" value="24" oninput="markAirPumpScheduleEditing()">
    <small style="color: #6c757d; display: block; margin-top: 5px;">
      Để 24 = Không bao giờ tắt
    </small>
  </div>
  <button class="btn-primary" onclick="updateAirPumpSchedule()">💾 Cập Nhật Lịch Máy Sục</button>
</div>
    </div>
  </div>

  <!-- TAB 4: CAMERA -->
  <div id="tab-camera" class="tab-content">
    <div class="card camera-card">
      <div class="card-header">
        <div class="card-icon">📷</div>
        <div class="card-title">Camera Trực Tiếp</div>
      </div>
      <div class="camera-container">
        <img id="camera" src="" alt="Đang kết nối camera...">
      </div>
      <div class="camera-controls">
        <button class="btn-primary" onclick="refreshCamera()">🔄 Làm mới</button>
        <button class="btn-success" onclick="capturePhoto()">📸 Chụp ảnh</button>
      </div>
    </div>
  </div>

  <!-- TAB 5: HISTORY -->
  <div id="tab-history" class="tab-content">
    <div class="card">
      <div class="card-header">
        <div class="card-icon">📜</div>
        <div class="card-title">Lịch Sử Hoạt Động</div>
      </div>
      <div class="history-list" id="historyList">
        <div class="info-item">Đang tải...</div>
      </div>
    </div>
  </div>

  <!-- TAB 6: SETTINGS -->
  <div id="tab-settings" class="tab-content">
    <div class="grid">
      <div class="card">
        <div class="card-header">
          <div class="card-icon">⚙️</div>
          <div class="card-title">Chế Độ Tự Động</div>
        </div>
        
        <div class="info-item">
          <span class="info-label">🍽️ Tự động cho ăn</span>
          <label class="toggle-switch">
            <input type="checkbox" id="toggleAutoFeed" onchange="toggleAutoMode('feed', this.checked)">
            <span class="slider"></span>
          </label>
        </div>

        <div class="info-item">
          <span class="info-label">💧 Tự động bơm lọc</span>
          <label class="toggle-switch">
            <input type="checkbox" id="toggleAutoFilter" onchange="toggleAutoMode('filter', this.checked)">
            <span class="slider"></span>
          </label>
        </div>

        <div class="info-item">
          <span class="info-label">💡 Tự động LED</span>
          <label class="toggle-switch">
            <input type="checkbox" id="toggleAutoLED" onchange="toggleAutoMode('led', this.checked)">
            <span class="slider"></span>
          </label>
        </div>
        <div class="info-item">
          <span class="info-label">🫧 Tự động máy sục khí</span>
          <label class="toggle-switch">
          <input type="checkbox" id="toggleAutoAirPump" onchange="toggleAutoMode('airpump', this.checked)">
          <span class="slider"></span>
          </label>
        </div>
      </div>

      <div class="card">
        <div class="card-header">
          <div class="card-icon">🔔</div>
          <div class="card-title">Ngưỡng Cảnh Báo</div>
        </div>
        <div class="info-item">
          <span class="info-label">Nhiệt độ cao</span>
          <span class="info-value">> 29°C</span>
        </div>
        <div class="info-item">
          <span class="info-label">Nhiệt độ thấp</span>
          <span class="info-value">< 17°C</span>
        </div>
        <div class="info-item">
          <span class="info-label">Mực nước thấp</span>
          <span class="info-value">< 500 ADC</span>
        </div>
      </div>

      <div class="card">
        <div class="card-header">
          <div class="card-icon">ℹ️</div>
          <div class="card-title">Thông Tin Hệ Thống</div>
        </div>
        <div class="info-item">
          <span class="info-label">IP ESP32-C3</span>
          <span class="info-value" id="systemIP">--</span>
        </div>
        <div class="info-item">
          <span class="info-label">Thời gian chạy</span>
          <span class="info-value" id="uptime">--</span>
        </div>
        <div class="info-item">
          <span class="info-label">Phiên bản</span>
          <span class="info-value">v2.0 Pro</span>
        </div>
      </div>
    </div>
  </div>
</div>

<!-- Notification -->
<div class="notification" id="notification"></div>

<script>
// ========== GLOBAL VARIABLES ==========
let currentTab = 'dashboard';
let statsData = {};
let isEditingSchedule = false;  // Tracking khi đang edit lịch
let isEditingLED = false;  // Tracking khi đang edit LED
let isEditingFilter = false;  // Tracking khi đang edit lịch bơm lọc
let isEditingLEDSchedule = false;  // Tracking khi đang edit lịch LED (giờ bật/tắt)
let isEditingAirPumpSchedule = false;  // Tracking lịch máy sục khí
// ========== TAB MANAGEMENT ==========
function showTab(tabName) {
  document.querySelectorAll('.tab-content').forEach(el => el.classList.remove('active'));
  document.querySelectorAll('.tab').forEach(el => el.classList.remove('active'));
  
  document.getElementById('tab-' + tabName).classList.add('active');
  event.target.classList.add('active');
  
  currentTab = tabName;
  
  if (tabName === 'camera') updateCamera();
  if (tabName === 'history') loadHistory();
}

// ========== UPDATE DATA ==========
function updateData() {
  fetch('/api/status')
    .then(r => r.json())
    .then(data => {
      // Nhiệt độ & Mực nước
      document.getElementById('temp').innerHTML = data.temperature.toFixed(1) + '°C';
      document.getElementById('water').innerHTML = data.waterStatus;
      document.getElementById('waterADC').innerHTML = data.waterLevel;
      
      // Status
      let statusHtml = '';
      if (data.systemOK) {
        statusHtml += '<span class="status ok">✅ Hệ thống hoạt động tốt</span>';
      } else {
        statusHtml += '<span class="status error">⚠️ CÓ CẢNH BÁO</span>';
      }
      
      if (data.waterStatus === 'LOW') {
        statusHtml += '<span class="status warning">💧 Mực nước thấp</span>';
      }
      if (data.temperature > 29) {
        statusHtml += '<span class="status warning">🌡️ Nhiệt độ cao</span>';
      }
      if (data.temperature < 17) {
        statusHtml += '<span class="status warning">❄️ Nhiệt độ thấp</span>';
      }
      
      document.getElementById('status').innerHTML = statusHtml;
      
      // Pump
      document.getElementById('pumpStatus').innerHTML = data.pumpRunning ? '🟢 ĐANG CHẠY' : '🔴 TẮT';
      document.getElementById('pumpStatus').style.color = data.pumpRunning ? 'var(--success)' : 'var(--danger)';
      
      // Feed
      document.getElementById('lastFeed').innerHTML = data.lastFeed;
      
      // LED brightness
// LED brightness & color - CHỈ cập nhật khi KHÔNG đang edit
if (!isEditingLED) {
  document.getElementById('brightVal').innerText = data.ledBrightness;
  document.getElementById('brightness').value = data.ledBrightness;

  // Cập nhật màu từ ESP32
  if (data.ledColorHex) {
    document.getElementById('color').value = data.ledColorHex;
    document.getElementById('colorPreview').style.background = data.ledColorHex;
  }
}

// Cập nhật trạng thái LED
if (data.ledCurrentState) {
  document.getElementById('ledStatus').innerHTML = '🟢 ĐANG BẬT';
  document.getElementById('ledStatus').style.color = 'var(--success)';
} else {
  document.getElementById('ledStatus').innerHTML = '🔴 TẮT';
  document.getElementById('ledStatus').style.color = 'var(--danger)';
}
// Máy sục khí
if (data.airPumpRunning !== undefined) {
  document.getElementById('airPumpStatus').innerHTML = data.airPumpRunning ? '🟢 ĐANG CHẠY' : '🔴 TẮT';
  document.getElementById('airPumpStatus').style.color = data.airPumpRunning ? 'var(--success)' : 'var(--danger)';
}
    })
    .catch(err => showNotif('❌ Lỗi kết nối', 'error'));
  
  // Stats
  fetch('/api/stats')
    .then(r => r.json())
    .then(data => {
      statsData = data;
      
      // Min/Max temp
      document.getElementById('tempMin').innerHTML = data.tempMin.toFixed(1) + '°C';
      document.getElementById('tempMax').innerHTML = data.tempMax.toFixed(1) + '°C';
      
      // Stats
      let filterHours = Math.floor(data.totalFilterTime / 3600);
      document.getElementById('statPump').innerHTML = filterHours + 'h';
      document.getElementById('statFeed').innerHTML = data.totalFeedCount;
      
      // Modes
      document.getElementById('pumpMode').innerHTML = data.enableAutoFilter ? 'TỰ ĐỘNG' : 'THỦ CÔNG';
      document.getElementById('pumpMode').className = data.enableAutoFilter ? 'card-badge badge-auto' : 'card-badge badge-manual';
      
      document.getElementById('feedMode').innerHTML = data.enableAutoFeed ? 'TỰ ĐỘNG' : 'THỦ CÔNG';
      document.getElementById('feedMode').className = data.enableAutoFeed ? 'card-badge badge-auto' : 'card-badge badge-manual';
      
      document.getElementById('ledMode').innerHTML = data.ledAutoMode ? 'TỰ ĐỘNG' : 'THỦ CÔNG';
      document.getElementById('ledMode').className = data.ledAutoMode ? 'card-badge badge-auto' : 'card-badge badge-manual';
      
      // Feed schedule
      let scheduleStr = data.feedSchedule.map(h => h + 'h').join(', ');
      document.getElementById('todaySchedule').innerHTML = scheduleStr || 'Không có';
      
      // LED schedule
    // LED schedule
document.getElementById('ledSchedule').innerHTML = data.ledOnHour + ':00 - ' + data.ledOffHour + ':00';

// CHỈ cập nhật input khi KHÔNG đang edit
if (!isEditingLEDSchedule) {
  document.getElementById('ledOnHour').value = data.ledOnHour;
  document.getElementById('ledOffHour').value = data.ledOffHour;
}
      // Máy sục khí mode
// ✅ CODE MỚI - ĐÚNG
if (data.enableAutoAirPump !== undefined) {  // ← SỬA enableAutoAirPump
  document.getElementById('airPumpMode').innerHTML = data.enableAutoAirPump ? 'TỰ ĐỘNG' : 'THỦ CÔNG';
  document.getElementById('airPumpMode').className = data.enableAutoAirPump ? 'card-badge badge-auto' : 'card-badge badge-manual';
}

// Lịch máy sục khí
if (data.airPumpOnHour !== undefined && data.airPumpOffHour !== undefined) {
  let scheduleText = '';
  if (data.airPumpOnHour === 0 && data.airPumpOffHour === 24) {
    scheduleText = 'Luôn bật 24/7';
  } else {
    scheduleText = data.airPumpOnHour + ':00 - ' + data.airPumpOffHour + ':00';
  }
  document.getElementById('airPumpSchedule').innerHTML = scheduleText;
  
  // CHỈ cập nhật input khi KHÔNG đang edit
  if (!isEditingAirPumpSchedule) {
    document.getElementById('airPumpOnHour').value = data.airPumpOnHour;
    document.getElementById('airPumpOffHour').value = data.airPumpOffHour;
  }
}
      // Toggle switches
 // Toggle switches
document.getElementById('toggleAutoFeed').checked = data.enableAutoFeed;
document.getElementById('toggleAutoFilter').checked = data.enableAutoFilter;
document.getElementById('toggleAutoLED').checked = data.ledAutoMode;
document.getElementById('toggleAutoAirPump').checked = data.enableAutoAirPump;  // ← THÊM DÒNG NÀY
// Toggle switches (Schedule tab) - THÊM MỚI
if (document.getElementById('toggleFeedAuto')) {
  document.getElementById('toggleFeedAuto').checked = data.enableAutoFeed;
}
if (document.getElementById('toggleFilterAuto')) {
  document.getElementById('toggleFilterAuto').checked = data.enableAutoFilter;
}
if (document.getElementById('toggleLEDAutoSchedule')) {
  document.getElementById('toggleLEDAutoSchedule').checked = data.ledAutoMode;
}
if (document.getElementById('toggleAirPumpAuto')) {
  document.getElementById('toggleAirPumpAuto').checked = data.enableAutoAirPump;
}
// Lịch bơm lọc - CHỈ cập nhật khi KHÔNG đang edit
if (data.filterIntervalHours && data.filterDurationMinutes) {
  if (!isEditingFilter) {
    document.getElementById('filterInterval').value = data.filterIntervalHours;
    document.getElementById('filterDuration').value = data.filterDurationMinutes;
  }
  // Luôn cập nhật hiển thị lịch hiện tại
  document.getElementById('currentFilterSchedule').innerHTML = 
    `Mỗi ${data.filterIntervalHours}h, chạy ${data.filterDurationMinutes} phút`;
}
      // Feed schedule display
      // Feed schedule display - CHỈ cập nhật khi KHÔNG đang edit
if (!isEditingSchedule) {
  displayFeedSchedule(data.feedSchedule);
}
    });
}

// ========== FEED SCHEDULE DISPLAY ==========
// ========== FEED SCHEDULE DISPLAY (ĐÃ SỬA) ==========
function displayFeedSchedule(schedule) {
  // Lưu lịch hiện tại từ server
  selectedFeedHours = [...schedule];  // Clone array
  
  let html = '';
  for (let h = 0; h < 24; h++) {
    let isActive = selectedFeedHours.includes(h);
    html += `<div class="schedule-item ${isActive ? 'active' : ''}" 
                  onclick="toggleFeedHour(${h})">${h}h</div>`;
  }
  document.getElementById('feedScheduleDisplay').innerHTML = html;
}

let selectedFeedHours = [];

function toggleFeedHour(hour) {
  isEditingSchedule = true;  // Đánh dấu đang edit
  
  let idx = selectedFeedHours.indexOf(hour);
  if (idx > -1) {
    selectedFeedHours.splice(idx, 1);
  } else {
    selectedFeedHours.push(hour);
  }
  
  // Update display
  document.querySelectorAll('.schedule-item').forEach((el, i) => {
    if (selectedFeedHours.includes(i)) {
      el.classList.add('active');
    } else {
      el.classList.remove('active');
    }
  });
}

function updateFeedSchedule() {
  if (selectedFeedHours.length === 0) {
    showNotif('⚠️ Chọn ít nhất 1 giờ', 'warning');
    return;
  }
  
  let scheduleStr = selectedFeedHours.sort((a,b) => a-b).join(',');
  fetch('/api/schedule?feed=' + scheduleStr)
    .then(() => {
      showNotif('✅ Đã cập nhật lịch cho ăn', 'success');
      isEditingSchedule = false;  // Đã xong, cho phép cập nhật
      updateData();
    })
    .catch(err => {
      showNotif('❌ Lỗi: ' + err, 'error');
      isEditingSchedule = false;
    });
}

// ========== LED SCHEDULE ==========
// ========== LED SCHEDULE EDITING ==========
function markLEDScheduleEditing() {
  isEditingLEDSchedule = true;
  
  // Tự động reset sau 30 giây
  clearTimeout(window.ledScheduleEditTimeout);
  window.ledScheduleEditTimeout = setTimeout(() => {
    isEditingLEDSchedule = false;
  }, 30000);
}
// ========== AIR PUMP SCHEDULE EDITING ==========
function markAirPumpScheduleEditing() {
  isEditingAirPumpSchedule = true;
  
  clearTimeout(window.airPumpScheduleEditTimeout);
  window.airPumpScheduleEditTimeout = setTimeout(() => {
    isEditingAirPumpSchedule = false;
  }, 30000);
}

function updateAirPumpSchedule() {
  let onHour = document.getElementById('airPumpOnHour').value;
  let offHour = document.getElementById('airPumpOffHour').value;
  
  // Validate
  if (onHour < 0 || onHour > 23) {
    showNotif('⚠️ Giờ bật phải từ 0-23', 'warning');
    return;
  }
  
  if (offHour < 1 || offHour > 24) {
    showNotif('⚠️ Giờ tắt phải từ 1-24', 'warning');
    return;
  }
  
  fetch(`/api/schedule?airPumpOn=${onHour}&airPumpOff=${offHour}`)
    .then(() => {
      showNotif('✅ Đã cập nhật lịch máy sục khí', 'success');
      isEditingAirPumpSchedule = false;
      updateData();
    })
    .catch(err => {
      showNotif('❌ Lỗi: ' + err, 'error');
      isEditingAirPumpSchedule = false;
    });
}
function updateLEDSchedule() {
  let onHour = document.getElementById('ledOnHour').value;
  let offHour = document.getElementById('ledOffHour').value;
  
  // Validate
  if (onHour < 0 || onHour > 23 || offHour < 0 || offHour > 23) {
    showNotif('⚠️ Giờ phải từ 0-23', 'warning');
    return;
  }
  
  fetch(`/api/schedule?ledOn=${onHour}&ledOff=${offHour}`)
    .then(() => {
      showNotif('✅ Đã cập nhật lịch LED', 'success');
      isEditingLEDSchedule = false;  // Reset flag
      updateData();
    })
    .catch(err => {
      showNotif('❌ Lỗi: ' + err, 'error');
      isEditingLEDSchedule = false;  // Reset flag nếu lỗi
    });
}
// ========== FILTER SCHEDULE ==========
// ========== FILTER SCHEDULE EDITING ==========
function markFilterEditing() {
  isEditingFilter = true;
  
  // Tự động reset sau 30 giây
  clearTimeout(window.filterEditTimeout);
  window.filterEditTimeout = setTimeout(() => {
    isEditingFilter = false;
  }, 30000);
}
function updateFilterSchedule() {
  let interval = document.getElementById('filterInterval').value;
  let duration = document.getElementById('filterDuration').value;
  
  // Validate
  if (interval < 1 || interval > 24) {
    showNotif('⚠️ Chu kỳ lọc phải từ 1-24 giờ', 'warning');
    return;
  }
  
  if (duration < 5 || duration > 120) {
    showNotif('⚠️ Thời gian chạy phải từ 5-120 phút', 'warning');
    return;
  }
  
  fetch(`/api/schedule?filterInterval=${interval}&filterDuration=${duration}`)
    .then(() => {
      showNotif('✅ Đã cập nhật lịch bơm lọc', 'success');
      isEditingFilter = false;  // Reset flag
      updateData();
    })
    .catch(err => {
      showNotif('❌ Lỗi: ' + err, 'error');
      isEditingFilter = false;  // Reset flag nếu lỗi
    });
}
function controlPump(action) {
  if (action === 'on') {
    // BẬT BƠM - giống cũ
    fetch('/api/pump?action=on')
      .then(r => r.text())
      .then(msg => {
        if (msg === 'WARNING_LOW_WATER') {
          if (confirm('⚠️ CẢNH BÁO: Mực nước đang THẤP!\n\n' +
                      'Bật bơm trong điều kiện này có thể làm hỏng bơm.\n' +
                      'Bạn có chắc chắn muốn tiếp tục?\n\n' +
                      '✅ OK = Bật bơm (chịu trách nhiệm)\n' +
                      '❌ Cancel = Hủy bỏ')) {
            showNotif('✅ Đã bật bơm - MỰC NƯỚC THẤP! Hãy cẩn thận!', 'warning');
            updateData();
          } else {
            fetch('/api/pump?action=off')
              .then(() => {
                showNotif('❌ Đã hủy bật bơm', 'error');
                updateData();
              });
          }
        } else {
          showNotif('✅ Đã bật bơm', 'success');
          updateData();
        }
      })
      .catch(err => showNotif('❌ Lỗi: ' + err, 'error'));
      
  } else if (action === 'off') {
    // ===== TẮT BƠM - XỬ LÝ BẢO VỆ AUTO =====
    fetch('/api/pump?action=off')
      .then(r => {
        if (r.status === 403) {
          // ❌ BỊ CHẶN - Đang trong chu kỳ auto
          return r.text().then(msg => {
            if (msg === 'AUTO_CYCLE_RUNNING') {
              showNotif(
                '⚠️ KHÔNG THỂ TẮT BƠM!\n\n' +
                'Bơm đang trong CHU KỲ LỌC TỰ ĐỘNG.\n\n' +
                'Lý do:\n' +
                '• Đảm bảo chu kỳ lọc hoàn chỉnh\n' +
                '• Bảo vệ hệ thống lọc\n\n' +
                'Bạn có 2 lựa chọn:\n' +
                '1️⃣ ĐỢI bơm tự động tắt (còn vài phút)\n' +
                '2️⃣ VÀO TAB "CÀI ĐẶT" → TẮT toggle "Tự động bơm lọc"\n\n' +
                '⚡ Sau khi tắt auto, bạn có thể tắt thủ công.',
                'warning'
              );
              throw new Error('AUTO_CYCLE_RUNNING');
            }
          });
        }
        
        if (!r.ok) throw new Error('Network error');
        return r.text();
      })
      .then(msg => {
        if (!msg) return;  // Skip nếu đã xử lý lỗi
        
        showNotif('⏹️ Đã tắt bơm', 'success');
        updateData();
      })
      .catch(err => {
        if (err.message !== 'AUTO_CYCLE_RUNNING') {
          console.error('Pump error:', err);
          showNotif('❌ Lỗi: ' + err, 'error');
        }
      });
  }
}

// ========== FEED ==========
function feedNow() {
  fetch('/api/feed')
    .then(r => r.text())
    .then(() => {
      showNotif('🐟 Đã cho cá ăn!', 'success');
      updateData();
    })
    .catch(err => showNotif('❌ Lỗi: ' + err, 'error'));
}

// ========== LED CONTROL ==========
// ========== LED CONTROL (ĐÃ SỬA) ==========

// Chỉ cập nhật hiển thị, chưa gửi lên ESP32
function updateBrightnessDisplay() {
  isEditingLED = true;  // Đánh dấu đang edit
  let val = document.getElementById('brightness').value;
  document.getElementById('brightVal').innerText = val;
    clearTimeout(window.ledEditTimeout);
  window.ledEditTimeout = setTimeout(() => {
    isEditingLED = false;
  }, 30000);
}

// Chỉ cập nhật preview màu, chưa gửi lên ESP32
function updateColorPreview() {
  isEditingLED = true;  // Đánh dấu đang edit
  let color = document.getElementById('color').value;
  document.getElementById('colorPreview').style.background = color;
   // Tự động reset sau 30 giây
  clearTimeout(window.ledEditTimeout);
  window.ledEditTimeout = setTimeout(() => {
    isEditingLED = false;
  }, 30000);
}
// Nút ÁP DỤNG - gửi cả màu và độ sáng lên ESP32
function applyLEDSettings() {
  let brightness = document.getElementById('brightness').value;
  let color = document.getElementById('color').value;
  
  console.log('Applying LED settings:', brightness, color);
  
  // Gửi cả 2 trong 1 request
  fetch(`/api/led?brightness=${brightness}&color=${encodeURIComponent(color)}`)
    .then(r => {
      if (!r.ok) throw new Error('Network error');
      return r.text();
    })
    .then(() => {
      showNotif('✨ Đã áp dụng màu và độ sáng LED', 'success');
      isEditingLED = false;  // Đã xong, cho phép cập nhật
      // Cập nhật lại để sync
      setTimeout(updateData, 500);
    })
    .catch(err => {
      console.error('LED error:', err);
      showNotif('❌ Lỗi: ' + err, 'error');
      isEditingLED = false;  // Reset flag nếu lỗi
    });
}
function ledManual(state) {
  fetch('/api/led?manual=' + state)
    .then(r => {
      if (r.status === 403) {
        return r.text().then(msg => {
          if (msg === 'AUTO_OVERRIDE_OFF') {
            showNotif(
              '⚠️ KHÔNG THỂ TẮT LED!\n\n' +
              'LED đang trong GIỜ TỰ ĐỘNG.\n' +
              'Bạn vẫn có thể:\n' +
              '✅ Đổi màu LED\n' +
              '✅ Điều chỉnh độ sáng\n\n' +
              'Để tắt LED:\n' +
              '1. Vào tab "Cài Đặt"\n' +
              '2. TẮT toggle "Tự động LED"\n' +
              '3. Hoặc đợi sau ' + statsData.ledOffHour + ':00',
              'warning'
            );
            throw new Error('AUTO_OVERRIDE_OFF');
          }
        });
      }
      if (!r.ok) throw new Error('Network error');
      return r.text();
    })
    .then(msg => {
      if (!msg) return;
      
      let notifMsg = state == 'on' ? '💡 Đã bật đèn' : '🌙 Đã tắt đèn';
      showNotif(notifMsg, 'success');
      updateData();
    })
    .catch(err => {
      if (err.message !== 'AUTO_OVERRIDE_OFF') {
        console.error('LED error:', err);
        showNotif('❌ Lỗi: ' + err, 'error');
      }
    });
}

function toggleLEDAuto() {
  fetch('/api/led?auto=true')
    .then(() => showNotif('🔄 Đã bật chế độ tự động', 'success'));
}
// ========== AIR PUMP CONTROL ==========
function controlAirPump(action) {
  fetch('/api/airpump?action=' + action)
    .then(r => {
      if (r.status === 403) {  // ← THÊM CHECK LỖI 403
        return r.text().then(msg => {
          if (msg === 'AUTO_OVERRIDE') {
            showNotif(
              '⚠️ KHÔNG THỂ ĐIỀU KHIỂN!\n\n' +
              'Máy sục khí đang trong GIỜ TỰ ĐỘNG.\n' +
              'Để điều khiển thủ công:\n' +
              '1. Vào tab "Cài Đặt"\n' +
              '2. TẮT toggle "Tự động máy sục khí"\n' +
              '3. Hoặc đợi ngoài giờ auto',
              'warning'
            );
            throw new Error('AUTO_OVERRIDE');
          }
        });
      }
      if (!r.ok) throw new Error('Network error');
      return r.text();
    })
    .then(msg => {
      if (!msg) return;  // Skip nếu đã xử lý lỗi
      
      let notifMsg = '';
      if (action === 'on') notifMsg = '🫧 Đã bật máy sục khí';
      else if (action === 'off') notifMsg = '⏹️ Đã tắt máy sục khí';
      
      showNotif(notifMsg, 'success');
      updateData();
    })
    .catch(err => {
      if (err.message !== 'AUTO_OVERRIDE') {
        console.error('Air pump error:', err);
        showNotif('❌ Lỗi: ' + err, 'error');
      }
    });
}
// ========== CAMERA ==========
function updateCamera() {
  let camIP = ')rawliteral" + cameraIP + R"rawliteral(';
  let img = document.getElementById('camera');
  img.src = 'http://' + camIP + ':81/stream?t=' + new Date().getTime();
}

function refreshCamera() {
  updateCamera();
  showNotif('🔄 Đang làm mới camera...', 'success');
}

function capturePhoto() {
  let camIP = ')rawliteral" + cameraIP + R"rawliteral(';
  window.open('http://' + camIP + '/capture', '_blank');
  showNotif('📸 Đang chụp ảnh...', 'success');
}

// ========== HISTORY ==========
function loadHistory() {
  fetch('/api/history')
    .then(r => r.json())
    .then(data => {
      let html = '';
      if (data.history.length === 0) {
        html = '<div class="info-item">Chưa có lịch sử</div>';
      } else {
        data.history.reverse().forEach(item => {
          html += `
            <div class="history-item">
              <div class="history-time">${item.time}</div>
              <div class="history-event">${item.event}</div>
              <div class="history-value">${item.value}</div>
            </div>
          `;
        });
      }
      document.getElementById('historyList').innerHTML = html;
    });
}

// ========== AUTO MODES ==========
function toggleAutoMode(type, enabled) {
  let param = '';
  let label = '';
  
  if (type === 'feed') {
    param = 'autoFeed';
    label = 'cho ăn';
  } else if (type === 'filter') {
    param = 'autoFilter';
    label = 'bơm lọc';
  } else if (type === 'led') {
    param = 'autoLED';
    label = 'LED';
  } else if (type === 'airpump') {  // ← THÊM ĐOẠN NÀY
    param = 'autoAirPump';
    label = 'máy sục khí';
  }
  
  fetch(`/api/settings?${param}=${enabled}`)
    .then(() => {
      let msg = enabled ? '✅ Đã BẬT tự động ' : '⚠️ Đã TẮT tự động ';
      msg += label;
      showNotif(msg, enabled ? 'success' : 'warning');
      updateData();
    })
    .catch(err => {
      console.error('Settings error:', err);
      showNotif('❌ Lỗi: ' + err, 'error');
    });
}
// ===== THÊM HÀM MỚI NÀY =====
function toggleAutoModeFromSchedule(type, enabled) {
  // Gọi hàm cũ
  toggleAutoMode(type, enabled);
  
  // Thông báo rõ ràng hơn
  let label = '';
  if (type === 'feed') label = 'cho ăn';
  else if (type === 'filter') label = 'bơm lọc';
  else if (type === 'led') label = 'LED';
  else if (type === 'airpump') label = 'máy sục khí';
  
  if (enabled) {
    showNotif(`🔄 Đã BẬT tự động ${label}. Hệ thống sẽ chạy theo lịch.`, 'success');
  } else {
    showNotif(`⏸️ Đã TẮT tự động ${label}. Chỉ điều khiển thủ công.`, 'warning');
  }
}
// ========== NOTIFICATION ==========
function showNotif(message, type) {
  let notif = document.getElementById('notification');
  notif.innerHTML = message;
  notif.className = 'notification ' + type + ' show';
  
  setTimeout(() => {
    notif.classList.remove('show');
  }, 3000);
}

// ========== SYSTEM TIME ==========
function updateSystemTime() {
  let now = new Date();
  let timeStr = now.toLocaleTimeString('vi-VN');
  document.getElementById('systemTime').innerHTML = '⏰ ' + timeStr;
  
  // Update IP
  document.getElementById('systemIP').innerHTML = window.location.hostname;
}

// ========== CAMERA ERROR HANDLER ==========
document.getElementById('camera').addEventListener('error', function() {
  console.log('Camera error, retrying...');
  setTimeout(updateCamera, 3000);
});

// ========== INIT ==========
setInterval(updateData, 2000);
setInterval(updateSystemTime, 1000);
updateData();
updateSystemTime();

// Load feed schedule from stats
fetch('/api/stats').then(r => r.json()).then(data => {
  selectedFeedHours = data.feedSchedule || [];
  displayFeedSchedule(selectedFeedHours);
});
// Khởi tạo màu LED ban đầu
setTimeout(() => {
  let color = document.getElementById('color').value;
  document.getElementById('colorPreview').style.background = color;
}, 500);
</script>

</body>
</html>
)rawliteral";
  
  return html;
}