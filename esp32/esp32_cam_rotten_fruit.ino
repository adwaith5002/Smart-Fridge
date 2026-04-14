#include <WiFi.h>
#include <esp_http_server.h>
#include <Smart-Fridge_inferencing.h>
#include "edge-impulse-sdk/dsp/image/image.hpp"
#include "esp_camera.h"
#include <PubSubClient.h>
#include <LittleFS.h>                          // [LOG] Flash filesystem

#include "driver/periph_ctrl.h"

#include "driver/gpio.h"
#include "soc/uart_reg.h"

/* =========================================================
 *  CUSTOM UART DRIVER — UART1, TX = GPIO2
 * ========================================================= */
#define UART1_BASE        0x3FF50000UL
#define UART1_FIFO        (*((volatile uint32_t*)(UART1_BASE + 0x00)))
#define UART1_STATUS      (*((volatile uint32_t*)(UART1_BASE + 0x1C)))
#define UART1_CONF0       (*((volatile uint32_t*)(UART1_BASE + 0x20)))
#define UART1_CLKDIV      (*((volatile uint32_t*)(UART1_BASE + 0x14)))

#define UART_TXFIFO_CNT(s) (((s) >> 16) & 0xFF)
#define UART_TXFIFO_MAX   127

void CustomUART_Init(void) {
    periph_module_enable(PERIPH_UART1_MODULE);
    gpio_set_direction((gpio_num_t)2, GPIO_MODE_OUTPUT);
    gpio_matrix_out(2, U1TXD_OUT_IDX, false, false);
    UART1_CLKDIV = (80000000UL / 115200UL);
    UART1_CONF0  = (3 << 0) | (0 << 4) | (1 << 6);
}

void CustomUART_SendByte(uint8_t byte) {
    while (UART_TXFIFO_CNT(UART1_STATUS) >= UART_TXFIFO_MAX);
    UART1_FIFO = byte;
}

void CustomUART_SendString(const char *str) {
    while (*str) CustomUART_SendByte((uint8_t)(*str++));
}

void CustomUART_Log(float fresh, float rotten, const char* status) {
    char buf[80];
    snprintf(buf, sizeof(buf),
        "[UART1] Fresh:%.2f Rotten:%.2f => %s\r\n", fresh, rotten, status);
    CustomUART_SendString(buf);
}
/* ========= END CUSTOM UART DRIVER ========= */


/* WiFi Credentials -------------------------------------------------------- */
const char* ssid = "Redmi Note 12 Pro+ 5G";
const char* password = "appimass";

/* ThingsBoard Credentials ------------------------------------------------- */
const char* tb_server = "mqtt.thingsboard.cloud";
const int   tb_port   = 1883;
const char* tb_token  = "60iIEYd7Dt1HSR2tCNLD";
WiFiClient   espClient;
PubSubClient tbClient(espClient);

/* Flash LED Pin ----------------------------------------------------------- */
#define FLASH_GPIO_NUM 4

/* [LOG] Flash log settings ------------------------------------------------ */
#define LOG_FILE_PATH     "/log.csv"
#define LOG_MAX_BYTES     200000UL   // ~200 KB — roll over to protect flash

/* Camera Model ------------------------------------------------------------ */
#define CAMERA_MODEL_AI_THINKER

#if defined(CAMERA_MODEL_AI_THINKER)
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
#endif

/* Constant defines -------------------------------------------------------- */
#define EI_CAMERA_RAW_FRAME_BUFFER_COLS           320
#define EI_CAMERA_RAW_FRAME_BUFFER_ROWS           240
#define EI_CAMERA_FRAME_BYTE_SIZE                 3

/* Private variables ------------------------------------------------------- */
static bool debug_nn = false;
static bool is_initialised = false;
uint8_t *snapshot_buf;
httpd_handle_t stream_httpd = NULL;

static camera_config_t camera_config = {
    .pin_pwdn = PWDN_GPIO_NUM,
    .pin_reset = RESET_GPIO_NUM,
    .pin_xclk = XCLK_GPIO_NUM,
    .pin_sscb_sda = SIOD_GPIO_NUM,
    .pin_sscb_scl = SIOC_GPIO_NUM,
    .pin_d7 = Y9_GPIO_NUM,
    .pin_d6 = Y8_GPIO_NUM,
    .pin_d5 = Y7_GPIO_NUM,
    .pin_d4 = Y6_GPIO_NUM,
    .pin_d3 = Y5_GPIO_NUM,
    .pin_d2 = Y4_GPIO_NUM,
    .pin_d1 = Y3_GPIO_NUM,
    .pin_d0 = Y2_GPIO_NUM,
    .pin_vsync = VSYNC_GPIO_NUM,
    .pin_href = HREF_GPIO_NUM,
    .pin_pclk = PCLK_GPIO_NUM,
    .xclk_freq_hz = 20000000,
    .ledc_timer = LEDC_TIMER_0,
    .ledc_channel = LEDC_CHANNEL_0,
    .pixel_format = PIXFORMAT_JPEG,
    .frame_size = FRAMESIZE_QVGA,
    .jpeg_quality = 12,
    .fb_count = 1,
    .fb_location = CAMERA_FB_IN_PSRAM,
    .grab_mode = CAMERA_GRAB_WHEN_EMPTY,
};

/* =========================================================
 * [LOG] Flash Logging Functions
 * ========================================================= */

void flashLog_Init() {
    if (!LittleFS.begin(true)) {   // true = format if mount fails
        Serial.println("[LOG] LittleFS mount FAILED");
        return;
    }
    Serial.println("[LOG] LittleFS mounted");

    // Write CSV header if file doesn't exist yet
    if (!LittleFS.exists(LOG_FILE_PATH)) {
        File f = LittleFS.open(LOG_FILE_PATH, FILE_WRITE);
        if (f) {
            f.println("uptime_ms,fresh_score,rotten_score,status");
            f.close();
            Serial.println("[LOG] Log file created with header");
        }
    } else {
        File f = LittleFS.open(LOG_FILE_PATH, FILE_READ);
        if (f) {
            Serial.printf("[LOG] Existing log: %u bytes\n", f.size());
            f.close();
        }
    }
}

void flashLog_Append(float fresh, float rotten, const char* status) {
    // Check file size — roll over if too large
    if (LittleFS.exists(LOG_FILE_PATH)) {
        File check = LittleFS.open(LOG_FILE_PATH, FILE_READ);
        if (check) {
            size_t sz = check.size();
            check.close();
            if (sz >= LOG_MAX_BYTES) {
                LittleFS.remove(LOG_FILE_PATH);
                Serial.println("[LOG] Log rolled over (size limit reached)");
                // Re-create header
                File f = LittleFS.open(LOG_FILE_PATH, FILE_WRITE);
                if (f) { f.println("uptime_ms,fresh_score,rotten_score,status"); f.close(); }
            }
        }
    }

    File f = LittleFS.open(LOG_FILE_PATH, FILE_APPEND);
    if (!f) {
        Serial.println("[LOG] Failed to open log for append");
        return;
    }
    char line[80];
    snprintf(line, sizeof(line), "%lu,%.4f,%.4f,%s",
             (unsigned long)millis(), fresh, rotten, status);
    f.println(line);
    f.close();
}

/* [LOG] HTTP handler: GET /log  → download the CSV ----------------------- */
static esp_err_t log_download_handler(httpd_req_t *req) {
    File f = LittleFS.open(LOG_FILE_PATH, FILE_READ);
    if (!f) {
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Log file not found");
        return ESP_FAIL;
    }
    httpd_resp_set_type(req, "text/csv");
    httpd_resp_set_hdr(req, "Content-Disposition", "attachment; filename=\"fridge_log.csv\"");

    uint8_t buf[512];
    while (f.available()) {
        int bytes_read = f.read(buf, sizeof(buf));
        httpd_resp_send_chunk(req, (const char*)buf, bytes_read);
    }
    f.close();
    httpd_resp_send_chunk(req, NULL, 0);  // end chunked response
    return ESP_OK;
}

/* [LOG] HTTP handler: GET /clearlog  → wipe the log ---------------------- */
static esp_err_t log_clear_handler(httpd_req_t *req) {
    LittleFS.remove(LOG_FILE_PATH);
    File f = LittleFS.open(LOG_FILE_PATH, FILE_WRITE);
    if (f) { f.println("uptime_ms,fresh_score,rotten_score,status"); f.close(); }
    httpd_resp_sendstr(req, "Log cleared.\n");
    Serial.println("[LOG] Log cleared via HTTP");
    return ESP_OK;
}

/* Web Stream Handler ------------------------------------------------------ */
#define PART_BOUNDARY "123456789000000000000987654321"
static const char* _STREAM_CONTENT_TYPE = "multipart/x-mixed-replace;boundary=" PART_BOUNDARY;
static const char* _STREAM_BOUNDARY = "\r\n--" PART_BOUNDARY "\r\n";
static const char* _STREAM_PART = "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n";

static esp_err_t stream_handler(httpd_req_t *req) {
    camera_fb_t * fb = NULL;
    esp_err_t res = ESP_OK;
    size_t _jpg_buf_len = 0;
    uint8_t * _jpg_buf = NULL;
    char * part_buf[64];

    res = httpd_resp_set_type(req, _STREAM_CONTENT_TYPE);
    if(res != ESP_OK) return res;

    while(true) {
        fb = esp_camera_fb_get();
        if (!fb) { res = ESP_FAIL; }
        else { _jpg_buf_len = fb->len; _jpg_buf = fb->buf; }

        if(res == ESP_OK) {
            size_t hlen = snprintf((char *)part_buf, 64, _STREAM_PART, _jpg_buf_len);
            res = httpd_resp_send_chunk(req, (const char *)part_buf, hlen);
        }
        if(res == ESP_OK) res = httpd_resp_send_chunk(req, (const char *)_jpg_buf, _jpg_buf_len);
        if(res == ESP_OK) res = httpd_resp_send_chunk(req, _STREAM_BOUNDARY, strlen(_STREAM_BOUNDARY));
        if(fb) esp_camera_fb_return(fb);
        if(res != ESP_OK) break;
        vTaskDelay(1 / portTICK_PERIOD_MS);
    }
    return res;
}

void startCameraServer() {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;

    httpd_uri_t stream_uri  = { .uri = "/",         .method = HTTP_GET, .handler = stream_handler,       .user_ctx = NULL };
    httpd_uri_t log_uri     = { .uri = "/log",       .method = HTTP_GET, .handler = log_download_handler, .user_ctx = NULL }; // [LOG]
    httpd_uri_t clrlog_uri  = { .uri = "/clearlog",  .method = HTTP_GET, .handler = log_clear_handler,    .user_ctx = NULL }; // [LOG]

    if (httpd_start(&stream_httpd, &config) == ESP_OK) {
        httpd_register_uri_handler(stream_httpd, &stream_uri);
        httpd_register_uri_handler(stream_httpd, &log_uri);     // [LOG]
        httpd_register_uri_handler(stream_httpd, &clrlog_uri);  // [LOG]
    }
}

/* ThingsBoard MQTT -------------------------------------------------------- */
void sendToThingsBoard(float fresh_score, float rotten_score, const char* status) {
    if (!tbClient.connected()) {
        Serial.print("Connecting to ThingsBoard...");
        if (tbClient.connect("ESP32_SmartFridge", tb_token, NULL)) {
            Serial.println("connected");
        } else {
            Serial.printf("failed, rc=%d\n", tbClient.state());
            return;
        }
    }
    char payload[128];
    snprintf(payload, sizeof(payload),
        "{\"fresh_score\":%.4f,\"rotten_score\":%.4f,\"status\":\"%s\"}",
        fresh_score, rotten_score, status);
    tbClient.publish("v1/devices/me/telemetry", payload);
    Serial.printf("TB Sent: %s\n", payload);
}

void sendRottenAlarm(float rotten_score) {
    if (!tbClient.connected()) {
        if (!tbClient.connect("ESP32_SmartFridge", tb_token, NULL)) return;
    }
    char payload[128];
    snprintf(payload, sizeof(payload),
        "{\"alarm\":\"ROTTEN_DETECTED\",\"severity\":\"CRITICAL\",\"rotten_score\":%.4f}",
        rotten_score);
    tbClient.publish("v1/devices/me/telemetry", payload);
    Serial.println("ROTTEN ALARM sent to ThingsBoard!");
}

/* Edge Impulse Helpers ---------------------------------------------------- */
bool ei_camera_init(void);
bool ei_camera_capture(uint32_t img_width, uint32_t img_height, uint8_t *out_buf);
static int ei_camera_get_data(size_t offset, size_t length, float *out_ptr);

/* Setup ------------------------------------------------------------------- */
void setup() {
    Serial.begin(115200);

    CustomUART_Init();
    CustomUART_SendString("[UART1] Custom driver ready\r\n");

    pinMode(FLASH_GPIO_NUM, OUTPUT);
    digitalWrite(FLASH_GPIO_NUM, LOW);

    flashLog_Init();   // [LOG] Mount LittleFS and prepare log file

    WiFi.begin(ssid, password);
    Serial.print("Connecting to WiFi");
    while (WiFi.status() != WL_CONNECTED) { delay(500); Serial.print("."); }
    Serial.println("\nWiFi connected");
    CustomUART_SendString("[UART1] WiFi connected\r\n");

    tbClient.setServer(tb_server, tb_port);

    if (ei_camera_init() == false) {
        Serial.println("Failed to initialize Camera!");
    } else {
        Serial.println("Camera initialized");
    }

    startCameraServer();
    Serial.print("Stream:    http://"); Serial.println(WiFi.localIP());
    Serial.print("Download log: http://"); Serial.print(WiFi.localIP()); Serial.println("/log");       // [LOG]
    Serial.print("Clear log:    http://"); Serial.print(WiFi.localIP()); Serial.println("/clearlog"); // [LOG]

    ei_sleep(2000);
}

/* Loop -------------------------------------------------------------------- */
void loop() {
    tbClient.loop();

    if (ei_sleep(5) != EI_IMPULSE_OK) return;

    snapshot_buf = (uint8_t*)malloc(EI_CAMERA_RAW_FRAME_BUFFER_COLS * EI_CAMERA_RAW_FRAME_BUFFER_ROWS * EI_CAMERA_FRAME_BYTE_SIZE);
    if(snapshot_buf == nullptr) return;

    ei::signal_t signal;
    signal.total_length = EI_CLASSIFIER_INPUT_WIDTH * EI_CLASSIFIER_INPUT_HEIGHT;
    signal.get_data = &ei_camera_get_data;

    digitalWrite(FLASH_GPIO_NUM, HIGH);
    delay(100);
    bool capture_status = ei_camera_capture((size_t)EI_CLASSIFIER_INPUT_WIDTH, (size_t)EI_CLASSIFIER_INPUT_HEIGHT, snapshot_buf);
    digitalWrite(FLASH_GPIO_NUM, LOW);

    if (capture_status == false) { free(snapshot_buf); return; }

    ei_impulse_result_t result = { 0 };
    EI_IMPULSE_ERROR err = run_classifier(&signal, &result, debug_nn);
    if (err != EI_IMPULSE_OK) { free(snapshot_buf); return; }

    float fresh_score = 0.0, rotten_score = 0.0;
    for (uint16_t i = 0; i < EI_CLASSIFIER_LABEL_COUNT; i++) {
        if (strcmp(ei_classifier_inferencing_categories[i], "fresh")  == 0) fresh_score  = result.classification[i].value;
        if (strcmp(ei_classifier_inferencing_categories[i], "rotten") == 0) rotten_score = result.classification[i].value;
    }

    const char* status;
    if (fresh_score > 0.6) {
        Serial.println("FRESH");
        status = "FRESH";
    } else if (rotten_score > 0.6) {
        Serial.println("ROTTEN");
        status = "ROTTEN";
        sendRottenAlarm(rotten_score);
    } else {
        Serial.println("UNCERTAIN");
        status = "UNCERTAIN";
    }
    Serial.printf("Fresh: %.2f, Rotten: %.2f -> %s\n", fresh_score, rotten_score, status);

    CustomUART_Log(fresh_score, rotten_score, status);
    flashLog_Append(fresh_score, rotten_score, status);  // [LOG] Write to flash
    sendToThingsBoard(fresh_score, rotten_score, status);

    free(snapshot_buf);
}

/* Camera Implementation --------------------------------------------------- */
bool ei_camera_init(void) {
    if (is_initialised) return true;
    esp_err_t err = esp_camera_init(&camera_config);
    if (err != ESP_OK) return false;
    is_initialised = true;
    return true;
}

bool ei_camera_capture(uint32_t img_width, uint32_t img_height, uint8_t *out_buf) {
    if (!is_initialised) return false;
    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb) return false;
    bool converted = fmt2rgb888(fb->buf, fb->len, PIXFORMAT_JPEG, snapshot_buf);
    esp_camera_fb_return(fb);
    if (!converted) return false;
    if ((img_width != EI_CAMERA_RAW_FRAME_BUFFER_COLS) || (img_height != EI_CAMERA_RAW_FRAME_BUFFER_ROWS)) {
        ei::image::processing::crop_and_interpolate_rgb888(out_buf, EI_CAMERA_RAW_FRAME_BUFFER_COLS, EI_CAMERA_RAW_FRAME_BUFFER_ROWS, out_buf, img_width, img_height);
    }
    return true;
}

static int ei_camera_get_data(size_t offset, size_t length, float *out_ptr) {
    size_t pixel_ix = offset * 3;
    size_t pixels_left = length;
    size_t out_ptr_ix = 0;
    while (pixels_left != 0) {
        out_ptr[out_ptr_ix] = (snapshot_buf[pixel_ix + 2] << 16) + (snapshot_buf[pixel_ix + 1] << 8) + snapshot_buf[pixel_ix];
        out_ptr_ix++; pixel_ix += 3; pixels_left--;
    }
    return 0;
}