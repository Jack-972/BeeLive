/* BeeGuardAI - XIAO ESP32-S3 - Version corrigée */

#include <BeeGuardAI_Hornet_inferencing.h>
#include "edge-impulse-sdk/dsp/image/image.hpp"
#include "esp_camera.h"
#include "esp_heap_caps.h"

// --- PINs XIAO ESP32-S3 ---
#define PWDN_GPIO_NUM  -1
#define RESET_GPIO_NUM -1
#define XCLK_GPIO_NUM  10
#define SIOD_GPIO_NUM  40
#define SIOC_GPIO_NUM  39
#define Y9_GPIO_NUM    48
#define Y8_GPIO_NUM    11
#define Y7_GPIO_NUM    12
#define Y6_GPIO_NUM    14
#define Y5_GPIO_NUM    16
#define Y4_GPIO_NUM    18
#define Y3_GPIO_NUM    17
#define Y2_GPIO_NUM    15
#define VSYNC_GPIO_NUM 38
#define HREF_GPIO_NUM  47
#define PCLK_GPIO_NUM  13
#define LED_PIN        21

// --- Tailles ---
// Le buffer doit accueillir la frame BRUTE de la caméra (avant resize)
// QVGA = 320x240, RGB888 = x3 octets
#define CAM_WIDTH   320
#define CAM_HEIGHT  240

// Taille pour la décompression JPEG → doit être la résolution CAMÉRA
#define SNAPSHOT_CAM_SIZE  (CAM_WIDTH * CAM_HEIGHT * 3)   // 230 400 octets

// Taille finale pour Edge Impulse (après redimensionnement)
#define SNAPSHOT_EI_SIZE   (EI_CLASSIFIER_INPUT_WIDTH * EI_CLASSIFIER_INPUT_HEIGHT * 3)

// On alloue le MAX des deux pour être sûr (en pratique = taille caméra)
#define SNAPSHOT_ALLOC_SIZE  max(SNAPSHOT_CAM_SIZE, SNAPSHOT_EI_SIZE)

static bool debug_nn    = false;
static bool is_initialised = false;
uint8_t *snapshot_buf   = nullptr;

// ---------------------------------------------------------------
static camera_config_t camera_config = {
    .pin_pwdn     = PWDN_GPIO_NUM,
    .pin_reset    = RESET_GPIO_NUM,
    .pin_xclk     = XCLK_GPIO_NUM,
    .pin_sscb_sda = SIOD_GPIO_NUM,
    .pin_sscb_scl = SIOC_GPIO_NUM,
    .pin_d7       = Y9_GPIO_NUM,
    .pin_d6       = Y8_GPIO_NUM,
    .pin_d5       = Y7_GPIO_NUM,
    .pin_d4       = Y6_GPIO_NUM,
    .pin_d3       = Y5_GPIO_NUM,
    .pin_d2       = Y4_GPIO_NUM,
    .pin_d1       = Y3_GPIO_NUM,
    .pin_d0       = Y2_GPIO_NUM,
    .pin_vsync    = VSYNC_GPIO_NUM,
    .pin_href     = HREF_GPIO_NUM,
    .pin_pclk     = PCLK_GPIO_NUM,
    .xclk_freq_hz = 20000000,
    .ledc_timer   = LEDC_TIMER_0,
    .ledc_channel = LEDC_CHANNEL_0,
    .pixel_format = PIXFORMAT_JPEG,
    .frame_size   = FRAMESIZE_QVGA,    // 320x240
    .jpeg_quality = 12,
    .fb_count     = 1,
    .fb_location  = CAMERA_FB_IN_PSRAM,
    .grab_mode    = CAMERA_GRAB_WHEN_EMPTY,
};

bool ei_camera_init(void);
bool ei_camera_capture(uint32_t img_width, uint32_t img_height, uint8_t *out_buf);
static int ei_camera_get_data(size_t offset, size_t length, float *out_ptr);

// ---------------------------------------------------------------
void setup() {
    Serial.begin(115200);
    delay(2000);
    Serial.println("\n========== BeeGuardAI DEMARRAGE ==========");

    Serial.printf("Buffer caméra brut  : %d octets (%dx%dx3)\n",
                  SNAPSHOT_CAM_SIZE, CAM_WIDTH, CAM_HEIGHT);
    Serial.printf("Buffer EI requis    : %d octets (%dx%dx3)\n",
                  SNAPSHOT_EI_SIZE, EI_CLASSIFIER_INPUT_WIDTH, EI_CLASSIFIER_INPUT_HEIGHT);
    Serial.printf("Buffer alloué       : %d octets\n", SNAPSHOT_ALLOC_SIZE);

    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, HIGH);

    if (!ei_camera_init()) {
        Serial.println("[ERREUR] Initialisation caméra échouée !");
        while(1) delay(1000);
    }
    Serial.println("[OK] Caméra initialisée");
    Serial.printf("[MEM] PSRAM libre : %u Ko\n",
                  heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024);
}

// ---------------------------------------------------------------
void loop() {
    if (ei_sleep(5) != EI_IMPULSE_OK) return;

    // Allocation en PSRAM avec la BONNE taille
    snapshot_buf = (uint8_t*)heap_caps_malloc(SNAPSHOT_ALLOC_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (snapshot_buf == nullptr) {
        Serial.println("[ERREUR] Echec allocation PSRAM !");
        delay(500);
        return;
    }

    bool ok = ei_camera_capture(
        (size_t)EI_CLASSIFIER_INPUT_WIDTH,
        (size_t)EI_CLASSIFIER_INPUT_HEIGHT,
        snapshot_buf
    );

    if (!ok) {
        Serial.println("[ERREUR] Capture échouée.");
        heap_caps_free(snapshot_buf);
        snapshot_buf = nullptr;
        return;
    }

    ei::signal_t signal;
    signal.total_length = EI_CLASSIFIER_INPUT_WIDTH * EI_CLASSIFIER_INPUT_HEIGHT;
    signal.get_data     = &ei_camera_get_data;

    ei_impulse_result_t result = {0};
    EI_IMPULSE_ERROR err = run_classifier(&signal, &result, debug_nn);

    if (err == EI_IMPULSE_OK) {
        bool hornet_detected = false;

        for (uint16_t i = 0; i < EI_CLASSIFIER_LABEL_COUNT; i++) {
            Serial.print(ei_classifier_inferencing_categories[i]);
            Serial.print(":");
            Serial.print(result.classification[i].value);
            if (i < EI_CLASSIFIER_LABEL_COUNT - 1) Serial.print(",");

            if (strstr(ei_classifier_inferencing_categories[i], "Hornet")
                && result.classification[i].value > 0.8f) {
                hornet_detected = true;
            }
        }
        Serial.println();

        digitalWrite(LED_PIN, hornet_detected ? LOW : HIGH);
    } else {
        Serial.printf("[ERREUR] run_classifier : %d\n", err);
    }

    heap_caps_free(snapshot_buf);
    snapshot_buf = nullptr;
}

// ---------------------------------------------------------------
bool ei_camera_init(void) {
    if (is_initialised) return true;
    esp_err_t err = esp_camera_init(&camera_config);
    if (err != ESP_OK) {
        Serial.printf("[CAMERA] Erreur init: 0x%x\n", err);
        return false;
    }
    is_initialised = true;
    return true;
}

bool ei_camera_capture(uint32_t img_width, uint32_t img_height, uint8_t *out_buf) {
    if (!is_initialised) return false;

    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb) {
        Serial.println("[CAMERA] fb_get() NULL !");
        return false;
    }

    // Conversion JPEG → RGB888
    // IMPORTANT : écrit CAM_WIDTH*CAM_HEIGHT*3 octets dans out_buf
    // Le buffer DOIT être >= SNAPSHOT_CAM_SIZE
    bool converted = fmt2rgb888(fb->buf, fb->len, PIXFORMAT_JPEG, out_buf);
    esp_camera_fb_return(fb);

    if (!converted) {
        Serial.println("[CAMERA] fmt2rgb888 échouée !");
        return false;
    }

    // Redimensionnement 320x240 → 200x200 (EI_CLASSIFIER_INPUT)
    // Opération in-place possible car on réduit la taille
    if ((int)img_width != CAM_WIDTH || (int)img_height != CAM_HEIGHT) {
        ei::image::processing::crop_and_interpolate_rgb888(
            out_buf, CAM_WIDTH, CAM_HEIGHT,
            out_buf, img_width, img_height
        );
    }

    return true;
}

static int ei_camera_get_data(size_t offset, size_t length, float *out_ptr) {
    size_t pixel_ix   = offset * 3;
    size_t out_ptr_ix = 0;
    while (out_ptr_ix < length) {
        out_ptr[out_ptr_ix] = (float)(
            (snapshot_buf[pixel_ix + 2] << 16) |
            (snapshot_buf[pixel_ix + 1] << 8)  |
             snapshot_buf[pixel_ix]
        );
        out_ptr_ix++;
        pixel_ix += 3;
    }
    return 0;
}