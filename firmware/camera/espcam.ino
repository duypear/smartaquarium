/*
 * ========================================
 * ESP32-CAM LIVE STREAM - FIXED
 * Camera cho hệ thống Smart Aquarium
 * ========================================
 */

#include "esp_camera.h"
#include <WiFi.h>
#include "esp_http_server.h"

// ==================== CẤU HÌNH WIFI ====================
const char* ssid = "Free Wifi";      // ⚠️ Giống ESP32-C3
const char* password = "123456789";  // ⚠️ Giống ESP32-C3

// ==================== CẤU HÌNH CAMERA AI-THINKER ====================
#define PWDN_GPIO_NUM     32
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM      0
#define SIOD_GPIO_NUM     26
#define SIOC_GPIO_NUM     27

#define Y9_GPIO_NUM       35
#define Y8_GPIO_NUM       34
#define Y7_GPIO_NUM       39
#define Y6_GPIO_NUM       36
#define Y5_GPIO_NUM       21
#define Y4_GPIO_NUM       19
#define Y3_GPIO_NUM       18
#define Y2_GPIO_NUM        5
#define VSYNC_GPIO_NUM    25
#define HREF_GPIO_NUM     23
#define PCLK_GPIO_NUM     22

// ==================== BIẾN TOÀN CỤC ====================
httpd_handle_t camera_httpd = NULL;
httpd_handle_t stream_httpd = NULL;

// ==================== STREAM HANDLER (ĐÃ SỬA) ====================
static esp_err_t stream_handler(httpd_req_t *req) {
  camera_fb_t * fb = NULL;
  esp_err_t res = ESP_OK;
  size_t _jpg_buf_len = 0;
  uint8_t * _jpg_buf = NULL;
  char part_buf[64];  // ⚠️ SỬA: Không dùng pointer array

  // Set content type
  res = httpd_resp_set_type(req, "multipart/x-mixed-replace; boundary=frame");
  if (res != ESP_OK) {
    return res;
  }

  while (true) {
    fb = esp_camera_fb_get();
    if (!fb) {
      Serial.println("Camera capture failed");
      res = ESP_FAIL;
      break;  // ⚠️ SỬA: Break thay vì continue
    }

    // Xử lý ảnh JPEG
    if (fb->format != PIXFORMAT_JPEG) {
      bool jpeg_converted = frame2jpg(fb, 80, &_jpg_buf, &_jpg_buf_len);
      esp_camera_fb_return(fb);
      fb = NULL;
      if (!jpeg_converted) {
        Serial.println("JPEG compression failed");
        res = ESP_FAIL;
        break;
      }
    } else {
      _jpg_buf_len = fb->len;
      _jpg_buf = fb->buf;
    }

    // Gửi header
    if (res == ESP_OK) {
      size_t hlen = snprintf((char *)part_buf, 64, 
                            "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n", 
                            _jpg_buf_len);
      res = httpd_resp_send_chunk(req, (const char *)part_buf, hlen);
    }

    // Gửi ảnh
    if (res == ESP_OK) {
      res = httpd_resp_send_chunk(req, (const char *)_jpg_buf, _jpg_buf_len);
    }

    // Gửi boundary
    if (res == ESP_OK) {
      res = httpd_resp_send_chunk(req, "\r\n--frame\r\n", 13);
    }

    // Dọn dẹp
    if (fb) {
      esp_camera_fb_return(fb);
      fb = NULL;
      _jpg_buf = NULL;
    } else if (_jpg_buf) {
      free(_jpg_buf);
      _jpg_buf = NULL;
    }

    if (res != ESP_OK) {
      break;
    }
  }

  return res;
}

// ==================== CAPTURE HANDLER ====================
static esp_err_t capture_handler(httpd_req_t *req) {
  camera_fb_t * fb = NULL;
  esp_err_t res = ESP_OK;

  fb = esp_camera_fb_get();
  if (!fb) {
    Serial.println("Camera capture failed");
    httpd_resp_send_500(req);
    return ESP_FAIL;
  }

  httpd_resp_set_type(req, "image/jpeg");
  httpd_resp_set_hdr(req, "Content-Disposition", "inline; filename=capture.jpg");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

  res = httpd_resp_send(req, (const char *)fb->buf, fb->len);
  esp_camera_fb_return(fb);
  return res;
}

// ==================== ROOT PAGE ====================
static esp_err_t index_handler(httpd_req_t *req) {
  const char* html = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>ESP32-CAM Stream</title>
<style>
body {
  font-family: Arial, sans-serif;
  text-align: center;
  background: #1a1a2e;
  color: white;
  margin: 0;
  padding: 20px;
}
h1 {
  color: #00d4ff;
  text-shadow: 0 0 10px rgba(0, 212, 255, 0.8);
}
.stream-container {
  max-width: 800px;
  margin: 20px auto;
  background: #16213e;
  padding: 20px;
  border-radius: 15px;
  box-shadow: 0 0 30px rgba(0, 212, 255, 0.3);
}
img {
  width: 100%;
  border: 3px solid #00d4ff;
  border-radius: 10px;
  box-shadow: 0 0 20px rgba(0, 212, 255, 0.5);
}
.button {
  display: inline-block;
  background: #00d4ff;
  color: #1a1a2e;
  padding: 15px 30px;
  margin: 10px;
  border-radius: 8px;
  text-decoration: none;
  font-weight: bold;
  transition: all 0.3s;
}
.button:hover {
  background: #00b8e6;
  transform: translateY(-2px);
  box-shadow: 0 5px 15px rgba(0, 212, 255, 0.6);
}
.info {
  background: #0f3460;
  padding: 15px;
  border-radius: 8px;
  margin: 20px auto;
  max-width: 600px;
}
.status {
  color: #4ecca3;
  font-weight: bold;
}
</style>
</head>
<body>
<h1>📷 ESP32-CAM Live Stream</h1>

<div class="stream-container">
  <img src="/stream" id="stream" alt="Loading stream...">
</div>

<div>
  <a href="/capture" class="button" download="aquarium.jpg">📸 Chụp ảnh</a>
</div>

<div class="info">
  <p>Stream URL: <span class="status">http://[IP]:81/stream</span></p>
  <p>Capture URL: <span class="status">http://[IP]/capture</span></p>
</div>

<script>
// Tự động reload nếu stream bị lỗi
document.getElementById('stream').onerror = function() {
  console.log('Stream error, reloading...');
  setTimeout(() => {
    this.src = '/stream?t=' + new Date().getTime();
  }, 1000);
};
</script>
</body>
</html>
)rawliteral";
  
  httpd_resp_send(req, html, strlen(html));
  return ESP_OK;
}

// ==================== KHỞI TẠO CAMERA ====================
void setupCamera() {
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sscb_sda = SIOD_GPIO_NUM;
  config.pin_sscb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;

  // Cấu hình chất lượng
  if (psramFound()) {
    config.frame_size = FRAMESIZE_VGA;  // ⚠️ SỬA: VGA thay vì SVGA (ổn định hơn)
    config.jpeg_quality = 12;           // 10-15 là tốt nhất cho stream
    config.fb_count = 2;
    Serial.println("PSRAM found - Using VGA resolution");
  } else {
    config.frame_size = FRAMESIZE_SVGA;
    config.jpeg_quality = 15;
    config.fb_count = 1;
    Serial.println("No PSRAM - Using SVGA resolution");
  }

  // Khởi tạo camera
  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("❌ Camera init failed with error 0x%x\n", err);
    return;
  }

  // Cấu hình sensor để stream tốt hơn
  sensor_t * s = esp_camera_sensor_get();
  if (s != NULL) {
    s->set_brightness(s, 0);     // -2 to 2
    s->set_contrast(s, 0);       // -2 to 2
    s->set_saturation(s, 0);     // -2 to 2
    s->set_special_effect(s, 0); // 0 = No Effect
    s->set_whitebal(s, 1);       // 0 = disable , 1 = enable
    s->set_awb_gain(s, 1);       // 0 = disable , 1 = enable
    s->set_wb_mode(s, 0);        // 0 to 4
    s->set_exposure_ctrl(s, 1);  // 0 = disable , 1 = enable
    s->set_aec2(s, 0);           // 0 = disable , 1 = enable
    s->set_ae_level(s, 0);       // -2 to 2
    s->set_aec_value(s, 300);    // 0 to 1200
    s->set_gain_ctrl(s, 1);      // 0 = disable , 1 = enable
    s->set_agc_gain(s, 0);       // 0 to 30
    s->set_gainceiling(s, (gainceiling_t)0); // 0 to 6
    s->set_bpc(s, 0);            // 0 = disable , 1 = enable
    s->set_wpc(s, 1);            // 0 = disable , 1 = enable
    s->set_raw_gma(s, 1);        // 0 = disable , 1 = enable
    s->set_lenc(s, 1);           // 0 = disable , 1 = enable
    s->set_hmirror(s, 0);        // 0 = disable , 1 = enable (lật ngang)
    s->set_vflip(s, 0);          // 0 = disable , 1 = enable (lật dọc)
    s->set_dcw(s, 1);            // 0 = disable , 1 = enable
    s->set_colorbar(s, 0);       // 0 = disable , 1 = enable
    
    Serial.println("✓ Camera sensor configured");
  }

  Serial.println("✓ Camera initialized successfully");
}

// ==================== KHỞI TẠO WEB SERVER ====================
void startCameraServer() {
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.server_port = 80;
  config.ctrl_port = 32768;
  
  httpd_uri_t index_uri = {
    .uri       = "/",
    .method    = HTTP_GET,
    .handler   = index_handler,
    .user_ctx  = NULL
  };

  httpd_uri_t capture_uri = {
    .uri       = "/capture",
    .method    = HTTP_GET,
    .handler   = capture_handler,
    .user_ctx  = NULL
  };

  Serial.printf("Starting web server on port: %d\n", config.server_port);
  if (httpd_start(&camera_httpd, &config) == ESP_OK) {
    httpd_register_uri_handler(camera_httpd, &index_uri);
    httpd_register_uri_handler(camera_httpd, &capture_uri);
    Serial.println("✓ Web server started on port 80");
  } else {
    Serial.println("❌ Failed to start web server");
  }

  // Stream server trên port 81
  config.server_port = 81;
  config.ctrl_port = 32769;  // ⚠️ SỬA: Port khác với server chính
  
  httpd_uri_t stream_uri = {
    .uri       = "/stream",
    .method    = HTTP_GET,
    .handler   = stream_handler,
    .user_ctx  = NULL
  };

  Serial.printf("Starting stream server on port: %d\n", config.server_port);
  if (httpd_start(&stream_httpd, &config) == ESP_OK) {
    httpd_register_uri_handler(stream_httpd, &stream_uri);
    Serial.println("✓ Stream server started on port 81");
  } else {
    Serial.println("❌ Failed to start stream server");
  }
}

// ==================== SETUP ====================
void setup() {
  Serial.begin(115200);
  Serial.setDebugOutput(true);
  Serial.println();

  Serial.println("========================================");
  Serial.println("ESP32-CAM AQUARIUM CAMERA");
  Serial.println("========================================");

  // Khởi tạo camera
  setupCamera();

  // Kết nối WiFi
  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("");
    Serial.println("✓ WiFi connected");
    Serial.print("IP Address: ");
    Serial.println(WiFi.localIP());
    Serial.println("");
    Serial.println("📋 URLs:");
    Serial.print("  Main Page:   http://");
    Serial.println(WiFi.localIP());
    Serial.print("  Stream:      http://");
    Serial.print(WiFi.localIP());
    Serial.println(":81/stream");
    Serial.print("  Capture:     http://");
    Serial.print(WiFi.localIP());
    Serial.println("/capture");
  } else {
    Serial.println("");
    Serial.println("❌ WiFi connection failed!");
    return;
  }

  // Khởi động server
  startCameraServer();

  Serial.println("========================================");
  Serial.println("✅ SYSTEM READY!");
  Serial.println("========================================");
}

// ==================== LOOP ====================
void loop() {
  // Camera server chạy tự động
  delay(10);
}