/**
 * pqc_benchmark.c
 * Phase 3 – Performance Measurement Framework
 * with Live Mock Sensor Data (Smart-Home scenario)
 *
 * Operations measured:
 *   - ML-KEM-512  (KeyGen / Encaps / Decaps)
 *   - ML-DSA-44   (KeyGen / Sign / Verify) on sensor payloads
 *   - Classical   (ECDH P-256, ECDSA P-256) for comparison
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <inttypes.h>
#include <math.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_cpu.h"
#include "esp_private/esp_clk.h"
#include "esp_random.h"

// mbedTLS classical crypto
#include "mbedtls/ecdh.h"
#include "mbedtls/ecdsa.h"
#include "mbedtls/entropy.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/sha256.h"

// ---------- Adjust paths to match your PQCMicro layout ----------
#include "crypto/ml-kem-512/api.h"
#include "crypto/ml-dsa-44/api.h"

// ---------- hybrid_ecc_mlkem prototype ----------
// void hybrid_ecc_mlkem_task(void *pvParameters);

static const char *TAG = "PQC_BENCH";

#define NUM_ITERATIONS      1000          // raise to 500–1000 for final runs
#define WARMUP_ITERATIONS   5
#define STACK_SIZE_KEM      65536        // 65536(high), 49152(low)  if more memory is needed (FreeRTOS stack high-water mark)
#define STACK_SIZE_DSA      81920        // 81920(high), 65536(low) if more memory is needed
#define STACK_SIZE_HYBRID_KEM   65536      // 64 KB
#define STACK_SIZE_HYBRID_DSA   81920      // 80 KB (for ECC + ML-DSA)
#define SENSOR_INTERVAL_MS  2000

// ============================================================
//  Mock Sensor Data
// ============================================================
typedef struct {
    float    temperature;     // °C
    float    humidity;        // %
    uint8_t  motion;          // 0/1
    uint8_t  door_open;       // 0/1
    uint16_t battery_mv;      // mV
    uint32_t timestamp;       // seconds since boot
    uint32_t sequence;
} sensor_reading_t;

static sensor_reading_t g_sensor;
static uint32_t g_seq = 0;

static void mock_sensor_update(void)
{
    // Simple but realistic variation
    g_sensor.temperature = 22.0f + (esp_random() % 100) / 20.0f - 2.5f;   // ~19.5–24.5
    g_sensor.humidity    = 45.0f + (esp_random() % 200) / 10.0f;          // ~45–65
    g_sensor.motion      = (esp_random() % 100) < 15;                     // 15% chance
    g_sensor.door_open   = (esp_random() % 100) < 8;
    g_sensor.battery_mv  = 3100 + (esp_random() % 200);                   // 3.1–3.3 V
    g_sensor.timestamp   = (uint32_t)(esp_timer_get_time() / 1000000ULL);
    g_sensor.sequence    = ++g_seq;
}

static void sensor_to_payload(const sensor_reading_t *s, uint8_t *buf, size_t *len)
{
    // Compact binary payload (easy to sign / encrypt)
    // Format: seq(4) | ts(4) | temp*100(2) | hum*100(2) | flags(1) | batt(2)
    uint8_t *p = buf;
    memcpy(p, &s->sequence, 4);          p += 4;
    memcpy(p, &s->timestamp, 4);         p += 4;

    int16_t t = (int16_t)(s->temperature * 100.0f);
    int16_t h = (int16_t)(s->humidity * 100.0f);
    memcpy(p, &t, 2);                    p += 2;
    memcpy(p, &h, 2);                    p += 2;

    uint8_t flags = (s->motion ? 0x01 : 0) | (s->door_open ? 0x02 : 0);
    *p++ = flags;

    memcpy(p, &s->battery_mv, 2);        p += 2;

    *len = p - buf;                      // 15 bytes
}

static void print_sensor(const sensor_reading_t *s)
{
    ESP_LOGI(TAG, "Sensor  seq=%" PRIu32 "  T=%.1f°C  H=%.1f%%  motion=%d  door=%d  batt=%u mV",
             s->sequence, s->temperature, s->humidity,
             s->motion, s->door_open, s->battery_mv);
}

// ============================================================
//  Benchmark result helpers
// ============================================================
typedef struct {
    const char *name;
    int64_t min_us, max_us, total_us;
    uint64_t min_cycles, max_cycles, total_cycles;
    size_t peak_heap;
    int iterations;
} bench_result_t;

static void result_init(bench_result_t *r, const char *name)
{
    r->name = name;
    r->min_us = INT64_MAX;
    r->max_us = 0;
    r->total_us = 0;
    r->min_cycles = UINT64_MAX;
    r->max_cycles = 0;
    r->total_cycles = 0;
    r->peak_heap = 0;
    r->iterations = 0;
}

static void result_add(bench_result_t *r, int64_t us, uint64_t cycles)
{
    if (us < r->min_us) r->min_us = us;
    if (us > r->max_us) r->max_us = us;
    r->total_us += us;

    if (cycles < r->min_cycles) r->min_cycles = cycles;
    if (cycles > r->max_cycles) r->max_cycles = cycles;
    r->total_cycles += cycles;

    r->iterations++;
}

static void result_print(const bench_result_t *r)
{
    if (r->iterations == 0) return;
    double avg_us     = (double)r->total_us / r->iterations;
    double avg_cycles = (double)r->total_cycles / r->iterations;

    ESP_LOGI(TAG, "%-24s | %8.1f | %7" PRId64 " | %7" PRId64 " | %11.0f | %6u",
             r->name, avg_us, r->min_us, r->max_us, avg_cycles, (unsigned)r->peak_heap);
}

static inline uint64_t get_cycles(void) { return esp_cpu_get_cycle_count(); }
static size_t get_free_heap(void) { return heap_caps_get_free_size(MALLOC_CAP_8BIT); }

// ============================================================
//  ML-KEM-512  (session establishment on sensor node)
// ============================================================
static void bench_mlkem_task(void *pv)
{
    ESP_LOGI(TAG, "\n========== ML-KEM-512 Pure Cryptographic Benchmark ==========");
    ESP_LOGI(TAG, "Iterations: %d", NUM_ITERATIONS);

    const size_t pk_len = PQCLEAN_MLKEM512_CLEAN_CRYPTO_PUBLICKEYBYTES;
    const size_t sk_len = PQCLEAN_MLKEM512_CLEAN_CRYPTO_SECRETKEYBYTES;
    const size_t ct_len = PQCLEAN_MLKEM512_CLEAN_CRYPTO_CIPHERTEXTBYTES;
    const size_t ss_len = PQCLEAN_MLKEM512_CLEAN_CRYPTO_BYTES;

    uint8_t *pk  = heap_caps_malloc(pk_len, MALLOC_CAP_8BIT);
    uint8_t *sk  = heap_caps_malloc(sk_len, MALLOC_CAP_8BIT);
    uint8_t *ct  = heap_caps_malloc(ct_len, MALLOC_CAP_8BIT);
    uint8_t *ss1 = heap_caps_malloc(ss_len, MALLOC_CAP_8BIT);
    uint8_t *ss2 = heap_caps_malloc(ss_len, MALLOC_CAP_8BIT);

    if (!pk || !sk || !ct || !ss1 || !ss2) {
        ESP_LOGE(TAG, "ML-KEM allocation failed");
        vTaskDelete(NULL);
        return;
    }

    bench_result_t kg, enc, dec;
    result_init(&kg,  "ML-KEM-512 KeyGen");
    result_init(&enc, "ML-KEM-512 Encaps");
    result_init(&dec, "ML-KEM-512 Decaps");

    size_t heap_before = get_free_heap();

    // Warm-up
    for (int i = 0; i < 5; i++) {
        PQCLEAN_MLKEM512_CLEAN_crypto_kem_keypair(pk, sk);
        PQCLEAN_MLKEM512_CLEAN_crypto_kem_enc(ct, ss1, pk);
        PQCLEAN_MLKEM512_CLEAN_crypto_kem_dec(ss2, ct, sk);
    }

    // ---------- Measurement loop with live metrics ----------
    for (int i = 0; i < NUM_ITERATIONS; i++) {
        uint64_t c0, c1;
        int64_t  t0, t1;
        int64_t  kg_us, enc_us, dec_us;
        uint64_t kg_cycles, enc_cycles, dec_cycles;

        // KeyGen
        c0 = get_cycles();
        t0 = esp_timer_get_time();
        PQCLEAN_MLKEM512_CLEAN_crypto_kem_keypair(pk, sk);
        t1 = esp_timer_get_time();
        c1 = get_cycles();
        kg_us = t1 - t0;
        kg_cycles = c1 - c0;
        result_add(&kg, kg_us, kg_cycles);

        // Encaps
        c0 = get_cycles();
        t0 = esp_timer_get_time();
        PQCLEAN_MLKEM512_CLEAN_crypto_kem_enc(ct, ss1, pk);
        t1 = esp_timer_get_time();
        c1 = get_cycles();
        enc_us = t1 - t0;
        enc_cycles = c1 - c0;
        result_add(&enc, enc_us, enc_cycles);

        // Decaps
        c0 = get_cycles();
        t0 = esp_timer_get_time();
        PQCLEAN_MLKEM512_CLEAN_crypto_kem_dec(ss2, ct, sk);
        t1 = esp_timer_get_time();
        c1 = get_cycles();
        dec_us = t1 - t0;
        dec_cycles = c1 - c0;
        result_add(&dec, dec_us, dec_cycles);

        // ===== LIVE METRICS (printed every iteration) =====
        ESP_LOGI("BENCH",
                 "algo=ML-KEM-512 iter=%d/%d | "
                 "KeyGen=%lld us (%llu cy) | "
                 "Encaps=%lld us (%llu cy) | "
                 "Decaps=%lld us (%llu cy) | "
                 "heap=%u",
                 i + 1, NUM_ITERATIONS,
                 (long long)kg_us,  (unsigned long long)kg_cycles,
                 (long long)enc_us, (unsigned long long)enc_cycles,
                 (long long)dec_us, (unsigned long long)dec_cycles,
                 (unsigned)get_free_heap());

        // Yield to keep watchdog happy
        if ((i & 0x07) == 0) {
            vTaskDelay(1);
        }
    }

    size_t heap_used = heap_before - get_free_heap();
    kg.peak_heap = enc.peak_heap = dec.peak_heap = heap_used;

    // ---------- Final Summary ----------
    ESP_LOGI(TAG, "---------------------------------------------------------------");
    ESP_LOGI(TAG, "%-24s | %8s | %7s | %7s | %11s | %6s",
             "Operation", "Avg us", "Min", "Max", "Avg cycles", "HeapΔ");
    ESP_LOGI(TAG, "---------------------------------------------------------------");
    result_print(&kg);
    result_print(&enc);
    result_print(&dec);
    ESP_LOGI(TAG, "---------------------------------------------------------------");

    double kg_ops  = 1000000.0 / ((double)kg.total_us  / kg.iterations);
    double enc_ops = 1000000.0 / ((double)enc.total_us / enc.iterations);
    double dec_ops = 1000000.0 / ((double)dec.total_us / dec.iterations);

    ESP_LOGI(TAG, "Public key size     : %u bytes", (unsigned)pk_len);
    ESP_LOGI(TAG, "Secret key size     : %u bytes", (unsigned)sk_len);
    ESP_LOGI(TAG, "Ciphertext size     : %u bytes", (unsigned)ct_len);
    ESP_LOGI(TAG, "Shared secret size  : %u bytes", (unsigned)ss_len);
    ESP_LOGI(TAG, "KeyGen throughput   : %.1f ops/sec", kg_ops);
    ESP_LOGI(TAG, "Encaps throughput   : %.1f ops/sec", enc_ops);
    ESP_LOGI(TAG, "Decaps throughput   : %.1f ops/sec", dec_ops);
    ESP_LOGI(TAG, "Heap used           : %u bytes", (unsigned)heap_used);
    ESP_LOGI(TAG, "CPU frequency       : %d MHz", esp_clk_cpu_freq() / 1000000);
    ESP_LOGI(TAG, "===============================================================");

    free(pk); free(sk); free(ct); free(ss1); free(ss2);
    vTaskDelete(NULL);
}
// ============================================================
//  ML-DSA-44  (sign live sensor payloads)
// ============================================================
static void bench_mldsa_task(void *pv)
{
    ESP_LOGI(TAG, "\n========== ML-DSA-44 Pure Cryptographic Benchmark ==========");
    ESP_LOGI(TAG, "Iterations: %d", NUM_ITERATIONS);

    const size_t pk_len  = PQCLEAN_MLDSA44_CLEAN_CRYPTO_PUBLICKEYBYTES;
    const size_t sk_len  = PQCLEAN_MLDSA44_CLEAN_CRYPTO_SECRETKEYBYTES;
    const size_t sig_max = PQCLEAN_MLDSA44_CLEAN_CRYPTO_BYTES;

    uint8_t *pk  = heap_caps_malloc(pk_len,  MALLOC_CAP_8BIT);
    uint8_t *sk  = heap_caps_malloc(sk_len,  MALLOC_CAP_8BIT);
    uint8_t *sig = heap_caps_malloc(sig_max, MALLOC_CAP_8BIT);
    size_t siglen = 0;

    if (!pk || !sk || !sig) {
        ESP_LOGE(TAG, "ML-DSA allocation failed");
        vTaskDelete(NULL);
        return;
    }

    // Fixed message for pure crypto measurement
    const uint8_t msg[] = "ESP32 pure cryptographic benchmark message for ML-DSA-44";
    const size_t  mlen  = sizeof(msg) - 1;

    bench_result_t kg, sign, verify;
    result_init(&kg,     "ML-DSA-44 KeyGen");
    result_init(&sign,   "ML-DSA-44 Sign");
    result_init(&verify, "ML-DSA-44 Verify");

    size_t heap_before = get_free_heap();

    // Warm-up
    for (int i = 0; i < 5; i++) {
        PQCLEAN_MLDSA44_CLEAN_crypto_sign_keypair(pk, sk);
        PQCLEAN_MLDSA44_CLEAN_crypto_sign_signature(sig, &siglen, msg, mlen, sk);
        PQCLEAN_MLDSA44_CLEAN_crypto_sign_verify(sig, siglen, msg, mlen, pk);
    }

    // ---------- Measurement loop with live metrics ----------
    for (int i = 0; i < NUM_ITERATIONS; i++) {
        uint64_t c0, c1;
        int64_t  t0, t1;
        int64_t  kg_us, sign_us, verify_us;
        uint64_t kg_cycles, sign_cycles, verify_cycles;

        // KeyGen
        c0 = get_cycles();
        t0 = esp_timer_get_time();
        PQCLEAN_MLDSA44_CLEAN_crypto_sign_keypair(pk, sk);
        t1 = esp_timer_get_time();
        c1 = get_cycles();
        kg_us = t1 - t0;
        kg_cycles = c1 - c0;
        result_add(&kg, kg_us, kg_cycles);

        // Sign
        c0 = get_cycles();
        t0 = esp_timer_get_time();
        PQCLEAN_MLDSA44_CLEAN_crypto_sign_signature(sig, &siglen, msg, mlen, sk);
        t1 = esp_timer_get_time();
        c1 = get_cycles();
        sign_us = t1 - t0;
        sign_cycles = c1 - c0;
        result_add(&sign, sign_us, sign_cycles);

        // Verify
        c0 = get_cycles();
        t0 = esp_timer_get_time();
        int ok = PQCLEAN_MLDSA44_CLEAN_crypto_sign_verify(sig, siglen, msg, mlen, pk);
        t1 = esp_timer_get_time();
        c1 = get_cycles();
        verify_us = t1 - t0;
        verify_cycles = c1 - c0;
        result_add(&verify, verify_us, verify_cycles);

        if (ok != 0) {
            ESP_LOGW(TAG, "Verify failed on iteration %d", i + 1);
        }

        // ===== LIVE METRICS (printed every iteration) =====
        ESP_LOGI("BENCH",
                 "algo=ML-DSA-44 iter=%d/%d | "
                 "KeyGen=%lld us (%llu cy) | "
                 "Sign=%lld us (%llu cy) | "
                 "Verify=%lld us (%llu cy) | "
                 "siglen=%u | heap=%u",
                 i + 1, NUM_ITERATIONS,
                 (long long)kg_us,     (unsigned long long)kg_cycles,
                 (long long)sign_us,   (unsigned long long)sign_cycles,
                 (long long)verify_us, (unsigned long long)verify_cycles,
                 (unsigned)siglen,
                 (unsigned)get_free_heap());

        // Yield to keep watchdog happy (DSA is heavy)
        if ((i & 0x03) == 0) {
            vTaskDelay(1);
        }
    }

    size_t heap_used = heap_before - get_free_heap();
    kg.peak_heap = sign.peak_heap = verify.peak_heap = heap_used;

    // ---------- Final Summary ----------
    ESP_LOGI(TAG, "---------------------------------------------------------------");
    ESP_LOGI(TAG, "%-24s | %8s | %7s | %7s | %11s | %6s",
             "Operation", "Avg us", "Min", "Max", "Avg cycles", "HeapΔ");
    ESP_LOGI(TAG, "---------------------------------------------------------------");
    result_print(&kg);
    result_print(&sign);
    result_print(&verify);
    ESP_LOGI(TAG, "---------------------------------------------------------------");

    double kg_ops     = 1000000.0 / ((double)kg.total_us     / kg.iterations);
    double sign_ops   = 1000000.0 / ((double)sign.total_us   / sign.iterations);
    double verify_ops = 1000000.0 / ((double)verify.total_us / verify.iterations);

    ESP_LOGI(TAG, "Public key size     : %u bytes", (unsigned)pk_len);
    ESP_LOGI(TAG, "Secret key size     : %u bytes", (unsigned)sk_len);
    ESP_LOGI(TAG, "Signature size      : %u bytes", (unsigned)siglen);
    ESP_LOGI(TAG, "KeyGen throughput   : %.1f ops/sec", kg_ops);
    ESP_LOGI(TAG, "Sign throughput     : %.1f ops/sec", sign_ops);
    ESP_LOGI(TAG, "Verify throughput   : %.1f ops/sec", verify_ops);
    ESP_LOGI(TAG, "Heap used           : %u bytes", (unsigned)heap_used);
    ESP_LOGI(TAG, "CPU frequency       : %d MHz", esp_clk_cpu_freq() / 1000000);
    ESP_LOGI(TAG, "===============================================================");

    free(pk);
    free(sk);
    free(sig);
    vTaskDelete(NULL);
}
// ============================================================
//  Classical comparison (mbedTLS) – same sensor payloads
// ============================================================
static void bench_ecc_task(void *pv)
{
    ESP_LOGI(TAG, "\n========== ECDSA secp256r1 (P-256) Pure Cryptographic Benchmark ==========");
    ESP_LOGI(TAG, "Iterations: %d", NUM_ITERATIONS);

    // Standard ECDSA P-256 key and signature sizes
    const size_t pk_len  = 64; // Raw public key (X + Y coordinates)
    const size_t sk_len  = 32; // Private key scalar
    const size_t sig_max = MBEDTLS_ECDSA_MAX_LEN; // ~72 bytes for DER encoded sig

    uint8_t *sig = heap_caps_malloc(sig_max, MALLOC_CAP_8BIT);
    size_t siglen = 0;

    if (!sig) {
        ESP_LOGE(TAG, "ECC allocation failed");
        vTaskDelete(NULL);
        return;
    }

    // Initialize mbedTLS contexts for ECDSA and RNG
    mbedtls_ecdsa_context ecdsa;
    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context ctr_drbg;

    mbedtls_ecdsa_init(&ecdsa);
    mbedtls_entropy_init(&entropy);
    mbedtls_ctr_drbg_init(&ctr_drbg);

    // Seed the Random Number Generator
    const char *pers = "esp32_ecdsa_bench";
    if (mbedtls_ctr_drbg_seed(&ctr_drbg, mbedtls_entropy_func, &entropy,
                              (const uint8_t *)pers, strlen(pers)) != 0) {
        ESP_LOGE(TAG, "mbedtls_ctr_drbg_seed failed");
        heap_caps_free(sig);
        vTaskDelete(NULL);
        return;
    }

    // Fixed message and pre-calculated SHA-256 hash for pure crypto timing
    const uint8_t msg[] = "ESP32 pure cryptographic benchmark message for ECDSA-P256";
    const size_t  mlen  = sizeof(msg) - 1;
    uint8_t hash[32];
    mbedtls_sha256(msg, mlen, hash, 0);

    bench_result_t kg, sign, verify;
    result_init(&kg,     "ECDSA-P256 KeyGen");
    result_init(&sign,   "ECDSA-P256 Sign");
    result_init(&verify, "ECDSA-P256 Verify");

    size_t heap_before = get_free_heap();

    // Warm-up phase (cache warming & context stabilization)
    for (int i = 0; i < 5; i++) {
        mbedtls_ecdsa_genkey(&ecdsa, MBEDTLS_ECP_DP_SECP256R1, mbedtls_ctr_drbg_random, &ctr_drbg);
        mbedtls_ecdsa_write_signature(&ecdsa, MBEDTLS_MD_SHA256, hash, sizeof(hash), 
                                      sig, sig_max, &siglen, 
                                      mbedtls_ctr_drbg_random, &ctr_drbg);
        mbedtls_ecdsa_read_signature(&ecdsa, hash, sizeof(hash), sig, siglen);
    }

    // ---------- Measurement loop with live metrics ----------
    for (int i = 0; i < NUM_ITERATIONS; i++) {
        uint64_t c0, c1;
        int64_t  t0, t1;
        int64_t  kg_us, sign_us, verify_us;
        uint64_t kg_cycles, sign_cycles, verify_cycles;

        // KeyGen
        c0 = get_cycles();
        t0 = esp_timer_get_time();
        mbedtls_ecdsa_genkey(&ecdsa, MBEDTLS_ECP_DP_SECP256R1, mbedtls_ctr_drbg_random, &ctr_drbg);
        t1 = esp_timer_get_time();
        c1 = get_cycles();
        kg_us = t1 - t0;
        kg_cycles = c1 - c0;
        result_add(&kg, kg_us, kg_cycles);

        // Sign
        c0 = get_cycles();
        t0 = esp_timer_get_time();
        int sign_res = mbedtls_ecdsa_write_signature(&ecdsa, MBEDTLS_MD_SHA256, hash, sizeof(hash),
                                                     sig, sig_max, &siglen,
                                                     mbedtls_ctr_drbg_random, &ctr_drbg);
        t1 = esp_timer_get_time();
        c1 = get_cycles();
        sign_us = t1 - t0;
        sign_cycles = c1 - c0;
        result_add(&sign, sign_us, sign_cycles);

        if (sign_res != 0) {
            ESP_LOGW(TAG, "Sign failed on iteration %d with code -0x%04X", i + 1, -sign_res);
        }

        // Verify
        c0 = get_cycles();
        t0 = esp_timer_get_time();
        int verify_res = mbedtls_ecdsa_read_signature(&ecdsa, hash, sizeof(hash), sig, siglen);
        t1 = esp_timer_get_time();
        c1 = get_cycles();
        verify_us = t1 - t0;
        verify_cycles = c1 - c0;
        result_add(&verify, verify_us, verify_cycles);

        if (verify_res != 0) {
            ESP_LOGW(TAG, "Verify failed on iteration %d with code -0x%04X", i + 1, -verify_res);
        }

        // ===== LIVE METRICS (printed every iteration) =====
        ESP_LOGI("BENCH",
                 "algo=ECDSA-P256 iter=%d/%d | "
                 "KeyGen=%lld us (%llu cy) | "
                 "Sign=%lld us (%llu cy) | "
                 "Verify=%lld us (%llu cy) | "
                 "siglen=%u | heap=%u",
                 i + 1, NUM_ITERATIONS,
                 (long long)kg_us,     (unsigned long long)kg_cycles,
                 (long long)sign_us,   (unsigned long long)sign_cycles,
                 (long long)verify_us, (unsigned long long)verify_cycles,
                 (unsigned)siglen,
                 (unsigned)get_free_heap());

        // Yield to keep task watchdog happy
        if ((i & 0x03) == 0) {
            vTaskDelay(1);
        }
    }

    size_t heap_used = heap_before - get_free_heap();
    kg.peak_heap = sign.peak_heap = verify.peak_heap = heap_used;

    // ---------- Final Summary ----------
    ESP_LOGI(TAG, "---------------------------------------------------------------");
    ESP_LOGI(TAG, "%-24s | %8s | %7s | %7s | %11s | %6s",
             "Operation", "Avg us", "Min", "Max", "Avg cycles", "HeapΔ");
    ESP_LOGI(TAG, "---------------------------------------------------------------");
    result_print(&kg);
    result_print(&sign);
    result_print(&verify);
    ESP_LOGI(TAG, "---------------------------------------------------------------");

    double kg_ops     = 1000000.0 / ((double)kg.total_us     / kg.iterations);
    double sign_ops   = 1000000.0 / ((double)sign.total_us   / sign.iterations);
    double verify_ops = 1000000.0 / ((double)verify.total_us / verify.iterations);

    ESP_LOGI(TAG, "Public key size     : %u bytes", (unsigned)pk_len);
    ESP_LOGI(TAG, "Secret key size     : %u bytes", (unsigned)sk_len);
    ESP_LOGI(TAG, "Signature size      : %u bytes", (unsigned)siglen);
    ESP_LOGI(TAG, "KeyGen throughput   : %.1f ops/sec", kg_ops);
    ESP_LOGI(TAG, "Sign throughput     : %.1f ops/sec", sign_ops);
    ESP_LOGI(TAG, "Verify throughput   : %.1f ops/sec", verify_ops);
    ESP_LOGI(TAG, "Heap used           : %u bytes", (unsigned)heap_used);
    ESP_LOGI(TAG, "CPU frequency       : %d MHz", esp_clk_cpu_freq() / 1000000);
    ESP_LOGI(TAG, "===============================================================");

    // Clean up mbedTLS contexts & memory
    mbedtls_ecdsa_free(&ecdsa);
    mbedtls_ctr_drbg_free(&ctr_drbg);
    mbedtls_entropy_free(&entropy);
    heap_caps_free(sig);

    vTaskDelete(NULL);
}

// ============================================================
//  Live sensor task (optional background stream)
// ============================================================
__attribute__((unused))
static void live_sensor_task(void *pv)
{
    while (1) {
        mock_sensor_update();
        print_sensor(&g_sensor);
        vTaskDelay(pdMS_TO_TICKS(SENSOR_INTERVAL_MS));
    }
}

// ============================================================
//  app_main
// ============================================================
void app_main(void)
{
    ESP_LOGI(TAG, "================================================");
    ESP_LOGI(TAG, "  Phase 3 – PQC Benchmark with Live Mock Sensors");
    ESP_LOGI(TAG, "  CPU        : %d MHz", esp_clk_cpu_freq() / 1000000);
    ESP_LOGI(TAG, "  Free heap  : %u bytes", (unsigned)get_free_heap());
    ESP_LOGI(TAG, "  Iterations : %d", NUM_ITERATIONS);
    ESP_LOGI(TAG, "================================================");

    // Optional: continuous sensor stream in background
    // xTaskCreate(live_sensor_task, "sensor", 4096, NULL, 3, NULL);

    // Run benchmarks sequentially (RAM limited)
    xTaskCreate(bench_mlkem_task,     "kem",   STACK_SIZE_KEM, NULL, 5, NULL);
    vTaskDelay(pdMS_TO_TICKS(25000));

    // xTaskCreate(bench_mldsa_task,     "dsa",   STACK_SIZE_DSA, NULL, 5, NULL);
    // vTaskDelay(pdMS_TO_TICKS(50000));

    // xTaskCreate(bench_classical_task, "class", 32768, NULL, 5, NULL);

    // xTaskCreate(hybrid_ecc_mlkem_task, "hybrid_kem", STACK_SIZE_HYBRID_KEM, NULL, 5, NULL);
    // vTaskDelay(pdMS_TO_TICKS(30000)); 

    // while (1) {
    //     vTaskDelay(pdMS_TO_TICKS(10000));
    // }

    vTaskDelete(NULL);   // delete the main task itself
}