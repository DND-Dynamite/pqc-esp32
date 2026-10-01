// #include <stdio.h>
// #include <string.h>
// #include "freertos/FreeRTOS.h"
// #include "freertos/task.h"
// #include "esp_timer.h"
// #include "esp_heap_caps.h"
// #include "esp_log.h"
// #include "esp_random.h"

// // --- Include according to the library you chose ---
// // For PQCMicro (C++):
// #include "PQCMicro.h"          // or the specific headers

// // For pure C (PQClean / mldsa-native / kybesp32 style):
// // #include "api.h"   // from your ml_kem and ml_dsa components

// static const char *TAG = "PQC_BASELINE";

// #define NUM_ITERATIONS 100   // increase to 500–1000 later

// void measure_mlkem() {
//     ESP_LOGI(TAG, "=== ML-KEM-512 Baseline ===");

//     // Allocate on heap (important – keys are large)
//     uint8_t *pk = heap_caps_malloc(CRYPTO_PUBLICKEYBYTES, MALLOC_CAP_DEFAULT);
//     uint8_t *sk = heap_caps_malloc(CRYPTO_SECRETKEYBYTES, MALLOC_CAP_DEFAULT);
//     uint8_t *ct = heap_caps_malloc(CRYPTO_CIPHERTEXTBYTES, MALLOC_CAP_DEFAULT);
//     uint8_t *ss1 = heap_caps_malloc(CRYPTO_BYTES, MALLOC_CAP_DEFAULT);
//     uint8_t *ss2 = heap_caps_malloc(CRYPTO_BYTES, MALLOC_CAP_DEFAULT);

//     if (!pk || !sk || !ct || !ss1 || !ss2) {
//         ESP_LOGE(TAG, "Heap allocation failed");
//         return;
//     }

//     int64_t t0, t1;
//     uint32_t total_keygen = 0, total_enc = 0, total_dec = 0;

//     for (int i = 0; i < NUM_ITERATIONS; i++) {
//         // KeyGen
//         t0 = esp_timer_get_time();
//         crypto_kem_keypair(pk, sk);          // or PQCKyber equivalent
//         t1 = esp_timer_get_time();
//         total_keygen += (t1 - t0);

//         // Encaps
//         t0 = esp_timer_get_time();
//         crypto_kem_enc(ct, ss1, pk);
//         t1 = esp_timer_get_time();
//         total_enc += (t1 - t0);

//         // Decaps
//         t0 = esp_timer_get_time();
//         crypto_kem_dec(ss2, ct, sk);
//         t1 = esp_timer_get_time();
//         total_dec += (t1 - t0);

//         // Sanity check
//         if (memcmp(ss1, ss2, CRYPTO_BYTES) != 0) {
//             ESP_LOGE(TAG, "Shared secret mismatch!");
//         }
//     }

//     ESP_LOGI(TAG, "KeyGen avg: %.2f us", total_keygen / (float)NUM_ITERATIONS);
//     ESP_LOGI(TAG, "Encaps avg: %.2f us", total_enc / (float)NUM_ITERATIONS);
//     ESP_LOGI(TAG, "Decaps avg: %.2f us", total_dec / (float)NUM_ITERATIONS);

//     // Memory
//     ESP_LOGI(TAG, "Free heap: %u bytes", heap_caps_get_free_size(MALLOC_CAP_DEFAULT));

//     free(pk); free(sk); free(ct); free(ss1); free(ss2);
// }

// void measure_mldsa() {
//     ESP_LOGI(TAG, "=== ML-DSA-44 Baseline ===");

//     uint8_t *pk = heap_caps_malloc(CRYPTO_SIGN_PUBLICKEYBYTES, MALLOC_CAP_DEFAULT);
//     uint8_t *sk = heap_caps_malloc(CRYPTO_SIGN_SECRETKEYBYTES, MALLOC_CAP_DEFAULT);
//     uint8_t *sig = heap_caps_malloc(CRYPTO_SIGN_BYTES, MALLOC_CAP_DEFAULT); // signature size
    
//     if (!pk || !sk || !sig) {
//         ESP_LOGE(TAG, "Heap allocation failed");
//         return;
//     }

//     const uint8_t msg[] = "Hello Post-Quantum ESP32!";
//     size_t mlen = sizeof(msg) - 1;
//     size_t siglen = 0;
    
//     int64_t t0, t1;
//     uint32_t total_keygen = 0, total_sign = 0, total_verify = 0;

//     for (int i = 0; i < NUM_ITERATIONS; i++) {
//         // KeyGen
//         t0 = esp_timer_get_time();
//         crypto_sign_keypair(pk, sk);
//         t1 = esp_timer_get_time();
//         total_keygen += (t1 - t0);

//         // Sign
//         t0 = esp_timer_get_time();
//         crypto_sign_signature(sig, &siglen, msg, mlen, sk);
//         t1 = esp_timer_get_time();
//         total_sign += (t1 - t0);

//         // Verify
//         t0 = esp_timer_get_time();
//         int verify_result = crypto_sign_verify(sig, siglen, msg, mlen, pk);
//         t1 = esp_timer_get_time();
//         total_verify += (t1 - t0);

//         if (verify_result != 0) {
//             ESP_LOGE(TAG, "Signature verification failed!");
//         }
//     }

//     ESP_LOGI(TAG, "KeyGen avg: %.2f us", total_keygen / (float)NUM_ITERATIONS);
//     ESP_LOGI(TAG, "Sign avg: %.2f us", total_sign / (float)NUM_ITERATIONS);
//     ESP_LOGI(TAG, "Verify avg: %.2f us", total_verify / (float)NUM_ITERATIONS);

//     ESP_LOGI(TAG, "Free heap: %u bytes", heap_caps_get_free_size(MALLOC_CAP_DEFAULT));

//     free(pk); free(sk); free(sig);
// }

// // main function, uncomment for single benchmark test

// // void app_main(void) {
// //     ESP_LOGI(TAG, "Starting Phase 2 Baseline");

// //     // Optional: print stack high-water mark
// //     ESP_LOGI(TAG, "Stack high water mark: %u", uxTaskGetStackHighWaterMark(NULL));

// //     measure_mlkem();
// //     measure_mldsa();

// //     // Keep running so you can read the logs
// //     while (1) {
// //         vTaskDelay(pdMS_TO_TICKS(5000));
// //     }
// // }