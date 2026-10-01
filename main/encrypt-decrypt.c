

// // pasted-text.txt
// // comment the parts of code that print live data into serial monitor. instead of creating keys and encaps and decaps it, generate whatever keys you want and then do encryption and decryption for respective algorithm. note the time for encryption and decryption along with others as well.


// // Worked for 3m 5s
// // The modified file is ready at:

// // /home/workdir/artifacts/pqc_benchmark.c

// // Changes made
// // All live per-iteration serial spam removed
// // Every ESP_LOGI("BENCH", ...) that printed KeyGen/Encaps/Decaps/Sign/Verify times (and heap) on every iteration has been removed/commented out across:
// // bench_mlkem_task
// // bench_mldsa_task
// // bench_classical_task
// // bench_hybrid_ecc_mlkem_task
// // phase4_optimized_crypto
// // bench_hybrid_x25519_mlkem_chacha_task
// // Keys generated once (outside the measurement loop)
// // For every algorithm:
// // Warm-up still runs.
// // KeyGen is measured a small fixed number of times (10) outside the main loop so you still get KeyGen timing statistics.
// // A final keypair is generated once and then reused.
// // Measurement loop now focuses on encryption / decryption (or Sign / Verify)
// // ML-KEM-512: Encaps + Decaps only
// // ML-DSA-44: Sign + Verify only
// // ECDSA P-256: Sign + Verify only
// // Hybrid ECDH-P256 + ML-KEM: Encaps + Decaps only
// // Phase-4 optimized hybrid: Encaps + Decaps only
// // Hybrid X25519 + ML-KEM + ChaCha20-Poly1305: Encaps + Decaps + AEAD Encrypt + AEAD Decrypt
// // Final summary tables (Avg / Min / Max / cycles / HeapΔ + throughput) are unchanged and still printed once at the end of each benchmark.

// // where is the code ?


// // Worked for 7s
// // The modified code is here:

// // /home/workdir/artifacts/pqc_benchmark.c

// // (1510 lines, ~60 KB)

// // You can download it from the artifacts folder, or I can show specific sections / make further changes if needed.




// // pqc_benchmark.c
// // pqc_benchmark.c
// /**
//  * pqc_benchmark.c
//  * Phase 3 – Performance Measurement Framework
//  * with Live Mock Sensor Data (Smart-Home scenario)
//  *
//  * Operations measured:
//  *   - ML-KEM-512  (KeyGen / Encaps / Decaps)
//  *   - ML-DSA-44   (KeyGen / Sign / Verify) on sensor payloads
//  *   - Classical   (ECDH P-256, ECDSA P-256) for comparison
//  */

// #include <stdio.h>
// #include <string.h>
// #include <stdlib.h>
// #include <inttypes.h>
// #include <math.h>

// #include "freertos/FreeRTOS.h"
// #include "freertos/task.h"
// #include "esp_timer.h"
// #include "esp_heap_caps.h"
// #include "esp_system.h"
// #include "esp_log.h"
// #include "esp_cpu.h"
// #include "esp_private/esp_clk.h"
// #include "esp_random.h"

// // mbedTLS classical crypto
// #include "mbedtls/ecdh.h"
// #include "mbedtls/ecdsa.h"
// #include "mbedtls/entropy.h"
// #include "mbedtls/ctr_drbg.h"
// #include "mbedtls/sha256.h"

// // ---------- Adjust paths to match your PQCMicro layout ----------
// #include "crypto/ml-kem-512/api.h"
// #include "crypto/ml-dsa-44/api.h"

// // ---------- hybrid_ecc_mlkem prototype ----------
// // void hybrid_ecc_mlkem_task(void *pvParameters);

// // X25519-ChaCha20-Poly1305 for hybrid encryption
// #include "mbedtls/ecdh.h"
// #include "mbedtls/chachapoly.h"
// #include "mbedtls/hkdf.h"          // optional but recommended for proper KDF

// static const char *TAG = "PQC_BENCH";

// #define NUM_ITERATIONS 1000 // raise to 500–1000 for final runs
// #define WARMUP_ITERATIONS 5
// #define STACK_SIZE_KEM 65536        // 65536(high), 49152(low)  if more memory is needed (FreeRTOS stack high-water mark)
// #define STACK_SIZE_DSA 81920        // 81920(high), 65536(low) if more memory is needed
// #define STACK_SIZE_HYBRID_KEM 65536 // 64 KB
// #define STACK_SIZE_HYBRID_DSA 81920 // 80 KB (for ECC + ML-DSA)
// #define SENSOR_INTERVAL_MS 2000

// // ------------------------------------------------------------------
// // Configuration (enable dual-core + accelerators via these defines)
// // ------------------------------------------------------------------
// #define USE_DUAL_CORE 1
// #define USE_HW_ACCELERATORS 1
// #define STACK_SIZE_WORKER 8192

// // ============================================================
// //  Mock Sensor Data
// // ============================================================
// typedef struct
// {
//     float temperature;   // °C
//     float humidity;      // %
//     uint8_t motion;      // 0/1
//     uint8_t door_open;   // 0/1
//     uint16_t battery_mv; // mV
//     uint32_t timestamp;  // seconds since boot
//     uint32_t sequence;
// } sensor_reading_t;

// static sensor_reading_t g_sensor;
// static uint32_t g_seq = 0;

// static void mock_sensor_update(void)
// {
//     // Simple but realistic variation
//     g_sensor.temperature = 22.0f + (esp_random() % 100) / 20.0f - 2.5f; // ~19.5–24.5
//     g_sensor.humidity = 45.0f + (esp_random() % 200) / 10.0f;           // ~45–65
//     g_sensor.motion = (esp_random() % 100) < 15;                        // 15% chance
//     g_sensor.door_open = (esp_random() % 100) < 8;
//     g_sensor.battery_mv = 3100 + (esp_random() % 200); // 3.1–3.3 V
//     g_sensor.timestamp = (uint32_t)(esp_timer_get_time() / 1000000ULL);
//     g_sensor.sequence = ++g_seq;
// }

// static void sensor_to_payload(const sensor_reading_t *s, uint8_t *buf, size_t *len)
// {
//     // Compact binary payload (easy to sign / encrypt)
//     // Format: seq(4) | ts(4) | temp*100(2) | hum*100(2) | flags(1) | batt(2)
//     uint8_t *p = buf;
//     memcpy(p, &s->sequence, 4);
//     p += 4;
//     memcpy(p, &s->timestamp, 4);
//     p += 4;

//     int16_t t = (int16_t)(s->temperature * 100.0f);
//     int16_t h = (int16_t)(s->humidity * 100.0f);
//     memcpy(p, &t, 2);
//     p += 2;
//     memcpy(p, &h, 2);
//     p += 2;

//     uint8_t flags = (s->motion ? 0x01 : 0) | (s->door_open ? 0x02 : 0);
//     *p++ = flags;

//     memcpy(p, &s->battery_mv, 2);
//     p += 2;

//     *len = p - buf; // 15 bytes
// }

// static void print_sensor(const sensor_reading_t *s)
// {
//     ESP_LOGI(TAG, "Sensor  seq=%" PRIu32 "  T=%.1f°C  H=%.1f%%  motion=%d  door=%d  batt=%u mV",
//              s->sequence, s->temperature, s->humidity,
//              s->motion, s->door_open, s->battery_mv);
// }

// // ============================================================
// //  Benchmark result helpers
// // ============================================================
// typedef struct
// {
//     const char *name;
//     int64_t min_us, max_us, total_us;
//     uint64_t min_cycles, max_cycles, total_cycles;
//     size_t peak_heap;
//     int iterations;
// } bench_result_t;

// static void result_init(bench_result_t *r, const char *name)
// {
//     r->name = name;
//     r->min_us = INT64_MAX;
//     r->max_us = 0;
//     r->total_us = 0;
//     r->min_cycles = UINT64_MAX;
//     r->max_cycles = 0;
//     r->total_cycles = 0;
//     r->peak_heap = 0;
//     r->iterations = 0;
// }

// static void result_add(bench_result_t *r, int64_t us, uint64_t cycles)
// {
//     if (us < r->min_us)
//         r->min_us = us;
//     if (us > r->max_us)
//         r->max_us = us;
//     r->total_us += us;

//     if (cycles < r->min_cycles)
//         r->min_cycles = cycles;
//     if (cycles > r->max_cycles)
//         r->max_cycles = cycles;
//     r->total_cycles += cycles;

//     r->iterations++;
// }

// static void result_print(const bench_result_t *r)
// {
//     if (r->iterations == 0)
//         return;
//     double avg_us = (double)r->total_us / r->iterations;
//     double avg_cycles = (double)r->total_cycles / r->iterations;

//     ESP_LOGI(TAG, "%-24s | %8.1f | %7" PRId64 " | %7" PRId64 " | %11.0f | %6u",
//              r->name, avg_us, r->min_us, r->max_us, avg_cycles, (unsigned)r->peak_heap);
// }

// static inline uint64_t get_cycles(void) { return esp_cpu_get_cycle_count(); }
// static size_t get_free_heap(void) { return heap_caps_get_free_size(MALLOC_CAP_8BIT); }

// // -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --
// // Dual-core worker (example: parallel work on one half of data)
// // ------------------------------------------------------------------
// typedef struct
// {
//     uint8_t *data;
//     size_t len;
//     int core_id;
// } worker_arg_t;

// static SemaphoreHandle_t sem_done = NULL;

// static void dual_core_worker(void *arg)
// {
//     worker_arg_t *w = (worker_arg_t *)arg;

//     // Simulate / perform heavy work (NTT, poly arithmetic, etc.)
//     // In real Kyber/Dilithium you would call the partitioned poly/NTT functions here
//     for (size_t i = 0; i < w->len; i++)
//     {
//         w->data[i] ^= (uint8_t)(i & 0xFF); // dummy work
//     }

//     ESP_LOGI(TAG, "Worker finished on core %d", xPortGetCoreID());
//     xSemaphoreGive(sem_done);
//     vTaskDelete(NULL);
// }

// // ------------------------------------------------------------------
// // Hardware-accelerated SHA-256 (uses ESP32 SHA peripheral via mbedTLS)
// // ------------------------------------------------------------------
// static void sha256_hw(const uint8_t *in, size_t len, uint8_t out[32])
// {
//     mbedtls_sha256_context ctx;
//     mbedtls_sha256_init(&ctx);
//     mbedtls_sha256_starts(&ctx, 0); // 0 = SHA-256
//     mbedtls_sha256_update(&ctx, in, len);
//     mbedtls_sha256_finish(&ctx, out);
//     mbedtls_sha256_free(&ctx);
// }

// // ============================================================
// //  ML-KEM-512  (session establishment on sensor node)
// // ============================================================
// static void bench_mlkem_task(void *pv)
// {
//     ESP_LOGI(TAG, "\n========== ML-KEM-512 Pure Cryptographic Benchmark ==========");
//     ESP_LOGI(TAG, "Iterations: %d", NUM_ITERATIONS);

//     const size_t pk_len = PQCLEAN_MLKEM512_CLEAN_CRYPTO_PUBLICKEYBYTES;
//     const size_t sk_len = PQCLEAN_MLKEM512_CLEAN_CRYPTO_SECRETKEYBYTES;
//     const size_t ct_len = PQCLEAN_MLKEM512_CLEAN_CRYPTO_CIPHERTEXTBYTES;
//     const size_t ss_len = PQCLEAN_MLKEM512_CLEAN_CRYPTO_BYTES;

//     uint8_t *pk = heap_caps_malloc(pk_len, MALLOC_CAP_8BIT);
//     uint8_t *sk = heap_caps_malloc(sk_len, MALLOC_CAP_8BIT);
//     uint8_t *ct = heap_caps_malloc(ct_len, MALLOC_CAP_8BIT);
//     uint8_t *ss1 = heap_caps_malloc(ss_len, MALLOC_CAP_8BIT);
//     uint8_t *ss2 = heap_caps_malloc(ss_len, MALLOC_CAP_8BIT);

//     if (!pk || !sk || !ct || !ss1 || !ss2)
//     {
//         ESP_LOGE(TAG, "ML-KEM allocation failed");
//         vTaskDelete(NULL);
//         return;
//     }

//     bench_result_t kg, enc, dec;
//     result_init(&kg, "ML-KEM-512 KeyGen");
//     result_init(&enc, "ML-KEM-512 Encaps");
//     result_init(&dec, "ML-KEM-512 Decaps");

//     size_t heap_before = get_free_heap();

//     // Warm-up + generate keys once (outside measurement loop)
//     for (int i = 0; i < WARMUP_ITERATIONS; i++)
//     {
//         PQCLEAN_MLKEM512_CLEAN_crypto_kem_keypair(pk, sk);
//         PQCLEAN_MLKEM512_CLEAN_crypto_kem_enc(ct, ss1, pk);
//         PQCLEAN_MLKEM512_CLEAN_crypto_kem_dec(ss2, ct, sk);
//     }

//     // Measure KeyGen a few times (outside main loop) for reference
//     for (int i = 0; i < 10; i++)
//     {
//         uint64_t c0 = get_cycles();
//         int64_t t0 = esp_timer_get_time();
//         PQCLEAN_MLKEM512_CLEAN_crypto_kem_keypair(pk, sk);
//         int64_t t1 = esp_timer_get_time();
//         uint64_t c1 = get_cycles();
//         result_add(&kg, t1 - t0, c1 - c0);
//     }

//     // Generate final keys for Encaps/Decaps measurement
//     PQCLEAN_MLKEM512_CLEAN_crypto_kem_keypair(pk, sk);

//     // ---------- Measurement loop: Encaps + Decaps only (no live serial spam) ----------
//     for (int i = 0; i < NUM_ITERATIONS; i++)
//     {
//         uint64_t c0, c1;
//         int64_t t0, t1;

//         // Encaps (encryption equivalent)
//         c0 = get_cycles();
//         t0 = esp_timer_get_time();
//         PQCLEAN_MLKEM512_CLEAN_crypto_kem_enc(ct, ss1, pk);
//         t1 = esp_timer_get_time();
//         c1 = get_cycles();
//         result_add(&enc, t1 - t0, c1 - c0);

//         // Decaps (decryption equivalent)
//         c0 = get_cycles();
//         t0 = esp_timer_get_time();
//         PQCLEAN_MLKEM512_CLEAN_crypto_kem_dec(ss2, ct, sk);
//         t1 = esp_timer_get_time();
//         c1 = get_cycles();
//         result_add(&dec, t1 - t0, c1 - c0);

//         // Yield to keep watchdog happy
//         if ((i & 0x07) == 0)
//         {
//             vTaskDelay(1);
//         }
//     }

//     size_t heap_used = heap_before - get_free_heap();
//     kg.peak_heap = enc.peak_heap = dec.peak_heap = heap_used;

//     // ---------- Final Summary ----------
//     ESP_LOGI(TAG, "---------------------------------------------------------------");
//     ESP_LOGI(TAG, "%-24s | %8s | %7s | %7s | %11s | %6s",
//              "Operation", "Avg us", "Min", "Max", "Avg cycles", "HeapΔ");
//     ESP_LOGI(TAG, "---------------------------------------------------------------");
//     result_print(&kg);
//     result_print(&enc);
//     result_print(&dec);
//     ESP_LOGI(TAG, "---------------------------------------------------------------");

//     double kg_ops = 1000000.0 / ((double)kg.total_us / kg.iterations);
//     double enc_ops = 1000000.0 / ((double)enc.total_us / enc.iterations);
//     double dec_ops = 1000000.0 / ((double)dec.total_us / dec.iterations);

//     ESP_LOGI(TAG, "Public key size     : %u bytes", (unsigned)pk_len);
//     ESP_LOGI(TAG, "Secret key size     : %u bytes", (unsigned)sk_len);
//     ESP_LOGI(TAG, "Ciphertext size     : %u bytes", (unsigned)ct_len);
//     ESP_LOGI(TAG, "Shared secret size  : %u bytes", (unsigned)ss_len);
//     ESP_LOGI(TAG, "KeyGen throughput   : %.1f ops/sec", kg_ops);
//     ESP_LOGI(TAG, "Encaps throughput   : %.1f ops/sec", enc_ops);
//     ESP_LOGI(TAG, "Decaps throughput   : %.1f ops/sec", dec_ops);
//     ESP_LOGI(TAG, "Heap used           : %u bytes", (unsigned)heap_used);
//     ESP_LOGI(TAG, "CPU frequency       : %d MHz", esp_clk_cpu_freq() / 1000000);
//     ESP_LOGI(TAG, "===============================================================");

//     free(pk);
//     free(sk);
//     free(ct);
//     free(ss1);
//     free(ss2);
//     vTaskDelete(NULL);
// }
// // ============================================================
// //  ML-DSA-44  (sign live sensor payloads)
// // ============================================================
// static void bench_mldsa_task(void *pv)
// {
//     ESP_LOGI(TAG, "\n========== ML-DSA-44 Pure Cryptographic Benchmark ==========");
//     ESP_LOGI(TAG, "Iterations: %d", NUM_ITERATIONS);

//     const size_t pk_len = PQCLEAN_MLDSA44_CLEAN_CRYPTO_PUBLICKEYBYTES;
//     const size_t sk_len = PQCLEAN_MLDSA44_CLEAN_CRYPTO_SECRETKEYBYTES;
//     const size_t sig_max = PQCLEAN_MLDSA44_CLEAN_CRYPTO_BYTES;

//     uint8_t *pk = heap_caps_malloc(pk_len, MALLOC_CAP_8BIT);
//     uint8_t *sk = heap_caps_malloc(sk_len, MALLOC_CAP_8BIT);
//     uint8_t *sig = heap_caps_malloc(sig_max, MALLOC_CAP_8BIT);
//     size_t siglen = 0;

//     if (!pk || !sk || !sig)
//     {
//         ESP_LOGE(TAG, "ML-DSA allocation failed");
//         vTaskDelete(NULL);
//         return;
//     }

//     // Fixed message for pure crypto measurement
//     const uint8_t msg[] = "ESP32 pure cryptographic benchmark message for ML-DSA-44";
//     const size_t mlen = sizeof(msg) - 1;

//     bench_result_t kg, sign, verify;
//     result_init(&kg, "ML-DSA-44 KeyGen");
//     result_init(&sign, "ML-DSA-44 Sign");
//     result_init(&verify, "ML-DSA-44 Verify");

//     size_t heap_before = get_free_heap();

//     // Warm-up
//     for (int i = 0; i < WARMUP_ITERATIONS; i++)
//     {
//         PQCLEAN_MLDSA44_CLEAN_crypto_sign_keypair(pk, sk);
//         PQCLEAN_MLDSA44_CLEAN_crypto_sign_signature(sig, &siglen, msg, mlen, sk);
//         PQCLEAN_MLDSA44_CLEAN_crypto_sign_verify(sig, siglen, msg, mlen, pk);
//     }

//     // Measure KeyGen a few times for reference
//     for (int i = 0; i < 10; i++)
//     {
//         uint64_t c0 = get_cycles();
//         int64_t t0 = esp_timer_get_time();
//         PQCLEAN_MLDSA44_CLEAN_crypto_sign_keypair(pk, sk);
//         int64_t t1 = esp_timer_get_time();
//         uint64_t c1 = get_cycles();
//         result_add(&kg, t1 - t0, c1 - c0);
//     }

//     // Final keypair for Sign/Verify measurement
//     PQCLEAN_MLDSA44_CLEAN_crypto_sign_keypair(pk, sk);

//     // ---------- Measurement loop: Sign + Verify only (no live serial spam) ----------
//     for (int i = 0; i < NUM_ITERATIONS; i++)
//     {
//         uint64_t c0, c1;
//         int64_t t0, t1;

//         // Sign
//         c0 = get_cycles();
//         t0 = esp_timer_get_time();
//         PQCLEAN_MLDSA44_CLEAN_crypto_sign_signature(sig, &siglen, msg, mlen, sk);
//         t1 = esp_timer_get_time();
//         c1 = get_cycles();
//         result_add(&sign, t1 - t0, c1 - c0);

//         // Verify
//         c0 = get_cycles();
//         t0 = esp_timer_get_time();
//         int ok = PQCLEAN_MLDSA44_CLEAN_crypto_sign_verify(sig, siglen, msg, mlen, pk);
//         t1 = esp_timer_get_time();
//         c1 = get_cycles();
//         result_add(&verify, t1 - t0, c1 - c0);

//         if (ok != 0)
//         {
//             ESP_LOGW(TAG, "Verify failed on iteration %d", i + 1);
//         }

//         // Yield to keep watchdog happy (DSA is heavy)
//         if ((i & 0x03) == 0)
//         {
//             vTaskDelay(1);
//         }
//     }

//     size_t heap_used = heap_before - get_free_heap();
//     kg.peak_heap = sign.peak_heap = verify.peak_heap = heap_used;

//     // ---------- Final Summary ----------
//     ESP_LOGI(TAG, "---------------------------------------------------------------");
//     ESP_LOGI(TAG, "%-24s | %8s | %7s | %7s | %11s | %6s",
//              "Operation", "Avg us", "Min", "Max", "Avg cycles", "HeapΔ");
//     ESP_LOGI(TAG, "---------------------------------------------------------------");
//     result_print(&kg);
//     result_print(&sign);
//     result_print(&verify);
//     ESP_LOGI(TAG, "---------------------------------------------------------------");

//     double kg_ops = 1000000.0 / ((double)kg.total_us / kg.iterations);
//     double sign_ops = 1000000.0 / ((double)sign.total_us / sign.iterations);
//     double verify_ops = 1000000.0 / ((double)verify.total_us / verify.iterations);

//     ESP_LOGI(TAG, "Public key size     : %u bytes", (unsigned)pk_len);
//     ESP_LOGI(TAG, "Secret key size     : %u bytes", (unsigned)sk_len);
//     ESP_LOGI(TAG, "Signature size      : %u bytes", (unsigned)siglen);
//     ESP_LOGI(TAG, "KeyGen throughput   : %.1f ops/sec", kg_ops);
//     ESP_LOGI(TAG, "Sign throughput     : %.1f ops/sec", sign_ops);
//     ESP_LOGI(TAG, "Verify throughput   : %.1f ops/sec", verify_ops);
//     ESP_LOGI(TAG, "Heap used           : %u bytes", (unsigned)heap_used);
//     ESP_LOGI(TAG, "CPU frequency       : %d MHz", esp_clk_cpu_freq() / 1000000);
//     ESP_LOGI(TAG, "===============================================================");

//     free(pk);
//     free(sk);
//     free(sig);
//     vTaskDelete(NULL);
// }
// // ============================================================
// //  Classical comparison (mbedTLS) – same sensor payloads
// // ============================================================
// static void bench_classical_task(void *pv)
// {
//     ESP_LOGI(TAG, "\n========== ECDSA secp256r1 (P-256) Pure Cryptographic Benchmark ==========");
//     ESP_LOGI(TAG, "Iterations: %d", NUM_ITERATIONS);

//     // Standard ECDSA P-256 key and signature sizes
//     const size_t pk_len = 64;                     // Raw public key (X + Y coordinates)
//     const size_t sk_len = 32;                     // Private key scalar
//     const size_t sig_max = MBEDTLS_ECDSA_MAX_LEN; // ~72 bytes for DER encoded sig

//     uint8_t *sig = heap_caps_malloc(sig_max, MALLOC_CAP_8BIT);
//     size_t siglen = 0;

//     if (!sig)
//     {
//         ESP_LOGE(TAG, "ECC allocation failed");
//         vTaskDelete(NULL);
//         return;
//     }

//     // Initialize mbedTLS contexts for ECDSA and RNG
//     mbedtls_ecdsa_context ecdsa;
//     mbedtls_entropy_context entropy;
//     mbedtls_ctr_drbg_context ctr_drbg;

//     mbedtls_ecdsa_init(&ecdsa);
//     mbedtls_entropy_init(&entropy);
//     mbedtls_ctr_drbg_init(&ctr_drbg);

//     // Seed the Random Number Generator
//     const char *pers = "esp32_ecdsa_bench";
//     if (mbedtls_ctr_drbg_seed(&ctr_drbg, mbedtls_entropy_func, &entropy,
//                               (const uint8_t *)pers, strlen(pers)) != 0)
//     {
//         ESP_LOGE(TAG, "mbedtls_ctr_drbg_seed failed");
//         heap_caps_free(sig);
//         vTaskDelete(NULL);
//         return;
//     }

//     // Fixed message and pre-calculated SHA-256 hash for pure crypto timing
//     const uint8_t msg[] = "ESP32 pure cryptographic benchmark message for ECDSA-P256";
//     const size_t mlen = sizeof(msg) - 1;
//     uint8_t hash[32];
//     mbedtls_sha256(msg, mlen, hash, 0);

//     bench_result_t kg, sign, verify;
//     result_init(&kg, "ECDSA-P256 KeyGen");
//     result_init(&sign, "ECDSA-P256 Sign");
//     result_init(&verify, "ECDSA-P256 Verify");

//     size_t heap_before = get_free_heap();

//     // Warm-up phase (cache warming & context stabilization)
//     for (int i = 0; i < WARMUP_ITERATIONS; i++)
//     {
//         mbedtls_ecdsa_genkey(&ecdsa, MBEDTLS_ECP_DP_SECP256R1, mbedtls_ctr_drbg_random, &ctr_drbg);
//         mbedtls_ecdsa_write_signature(&ecdsa, MBEDTLS_MD_SHA256, hash, sizeof(hash),
//                                       sig, sig_max, &siglen,
//                                       mbedtls_ctr_drbg_random, &ctr_drbg);
//         mbedtls_ecdsa_read_signature(&ecdsa, hash, sizeof(hash), sig, siglen);
//     }

//     // Measure KeyGen a few times for reference
//     for (int i = 0; i < 10; i++)
//     {
//         uint64_t c0 = get_cycles();
//         int64_t t0 = esp_timer_get_time();
//         mbedtls_ecdsa_genkey(&ecdsa, MBEDTLS_ECP_DP_SECP256R1, mbedtls_ctr_drbg_random, &ctr_drbg);
//         int64_t t1 = esp_timer_get_time();
//         uint64_t c1 = get_cycles();
//         result_add(&kg, t1 - t0, c1 - c0);
//     }

//     // Final key for Sign/Verify
//     mbedtls_ecdsa_genkey(&ecdsa, MBEDTLS_ECP_DP_SECP256R1, mbedtls_ctr_drbg_random, &ctr_drbg);

//     // ---------- Measurement loop: Sign + Verify only (no live serial spam) ----------
//     for (int i = 0; i < NUM_ITERATIONS; i++)
//     {
//         uint64_t c0, c1;
//         int64_t t0, t1;

//         // Sign
//         c0 = get_cycles();
//         t0 = esp_timer_get_time();
//         int sign_res = mbedtls_ecdsa_write_signature(&ecdsa, MBEDTLS_MD_SHA256, hash, sizeof(hash),
//                                                      sig, sig_max, &siglen,
//                                                      mbedtls_ctr_drbg_random, &ctr_drbg);
//         t1 = esp_timer_get_time();
//         c1 = get_cycles();
//         result_add(&sign, t1 - t0, c1 - c0);

//         if (sign_res != 0)
//         {
//             ESP_LOGW(TAG, "Sign failed on iteration %d with code -0x%04X", i + 1, -sign_res);
//         }

//         // Verify
//         c0 = get_cycles();
//         t0 = esp_timer_get_time();
//         int verify_res = mbedtls_ecdsa_read_signature(&ecdsa, hash, sizeof(hash), sig, siglen);
//         t1 = esp_timer_get_time();
//         c1 = get_cycles();
//         result_add(&verify, t1 - t0, c1 - c0);

//         if (verify_res != 0)
//         {
//             ESP_LOGW(TAG, "Verify failed on iteration %d with code -0x%04X", i + 1, -verify_res);
//         }

//         // Yield to keep task watchdog happy
//         if ((i & 0x03) == 0)
//         {
//             vTaskDelay(1);
//         }
//     }

//     size_t heap_used = heap_before - get_free_heap();
//     kg.peak_heap = sign.peak_heap = verify.peak_heap = heap_used;

//     // ---------- Final Summary ----------
//     ESP_LOGI(TAG, "---------------------------------------------------------------");
//     ESP_LOGI(TAG, "%-24s | %8s | %7s | %7s | %11s | %6s",
//              "Operation", "Avg us", "Min", "Max", "Avg cycles", "HeapΔ");
//     ESP_LOGI(TAG, "---------------------------------------------------------------");
//     result_print(&kg);
//     result_print(&sign);
//     result_print(&verify);
//     ESP_LOGI(TAG, "---------------------------------------------------------------");

//     double kg_ops = 1000000.0 / ((double)kg.total_us / kg.iterations);
//     double sign_ops = 1000000.0 / ((double)sign.total_us / sign.iterations);
//     double verify_ops = 1000000.0 / ((double)verify.total_us / verify.iterations);

//     ESP_LOGI(TAG, "Public key size     : %u bytes", (unsigned)pk_len);
//     ESP_LOGI(TAG, "Secret key size     : %u bytes", (unsigned)sk_len);
//     ESP_LOGI(TAG, "Signature size      : %u bytes", (unsigned)siglen);
//     ESP_LOGI(TAG, "KeyGen throughput   : %.1f ops/sec", kg_ops);
//     ESP_LOGI(TAG, "Sign throughput     : %.1f ops/sec", sign_ops);
//     ESP_LOGI(TAG, "Verify throughput   : %.1f ops/sec", verify_ops);
//     ESP_LOGI(TAG, "Heap used           : %u bytes", (unsigned)heap_used);
//     ESP_LOGI(TAG, "CPU frequency       : %d MHz", esp_clk_cpu_freq() / 1000000);
//     ESP_LOGI(TAG, "===============================================================");

//     // Clean up mbedTLS contexts & memory
//     mbedtls_ecdsa_free(&ecdsa);
//     mbedtls_ctr_drbg_free(&ctr_drbg);
//     mbedtls_entropy_free(&entropy);
//     heap_caps_free(sig);

//     vTaskDelete(NULL);
// }

// static void bench_hybrid_ecc_mlkem_task(void *pv)
// {
//     ESP_LOGI(TAG, "\n========== Hybrid ECDH-P256 + ML-KEM-512 Pure Cryptographic Benchmark ==========");
//     ESP_LOGI(TAG, "Iterations: %d", NUM_ITERATIONS);

//     // Size constants
//     const size_t ecc_pk_len = 65; // Uncompressed SECP256R1 point (0x04 + X + Y)
//     const size_t ecc_sk_len = 32;
//     const size_t mlkem_pk_len = PQCLEAN_MLKEM512_CLEAN_CRYPTO_PUBLICKEYBYTES;
//     const size_t mlkem_sk_len = PQCLEAN_MLKEM512_CLEAN_CRYPTO_SECRETKEYBYTES;
//     const size_t mlkem_ct_len = PQCLEAN_MLKEM512_CLEAN_CRYPTO_CIPHERTEXTBYTES;
//     const size_t ss_len = PQCLEAN_MLKEM512_CLEAN_CRYPTO_BYTES; // 32 bytes for both

//     // Memory allocation
//     uint8_t *mlkem_pk = heap_caps_malloc(mlkem_pk_len, MALLOC_CAP_8BIT);
//     uint8_t *mlkem_sk = heap_caps_malloc(mlkem_sk_len, MALLOC_CAP_8BIT);
//     uint8_t *mlkem_ct = heap_caps_malloc(mlkem_ct_len, MALLOC_CAP_8BIT);
//     uint8_t *ss_kem = heap_caps_malloc(ss_len, MALLOC_CAP_8BIT);
//     uint8_t *ss_ecc = heap_caps_malloc(ss_len, MALLOC_CAP_8BIT);
//     uint8_t *ss_hybrid = heap_caps_malloc(ss_len, MALLOC_CAP_8BIT);

//     if (!mlkem_pk || !mlkem_sk || !mlkem_ct || !ss_kem || !ss_ecc || !ss_hybrid)
//     {
//         ESP_LOGE(TAG, "Hybrid allocation failed");
//         vTaskDelete(NULL);
//         return;
//     }

//     // mbedTLS RNG + group setup
//     mbedtls_entropy_context entropy;
//     mbedtls_ctr_drbg_context ctr_drbg;
//     mbedtls_ecp_group grp;

//     mbedtls_entropy_init(&entropy);
//     mbedtls_ctr_drbg_init(&ctr_drbg);
//     mbedtls_ecp_group_init(&grp);
//     mbedtls_ecp_group_load(&grp, MBEDTLS_ECP_DP_SECP256R1);

//     const char *pers = "esp32_hybrid_bench";
//     if (mbedtls_ctr_drbg_seed(&ctr_drbg, mbedtls_entropy_func, &entropy,
//                               (const uint8_t *)pers, strlen(pers)) != 0)
//     {
//         ESP_LOGE(TAG, "mbedtls_ctr_drbg_seed failed");
//         goto cleanup_alloc;
//     }

//     // ECC key material
//     mbedtls_mpi d_static, d_eph;
//     mbedtls_ecp_point Q_static, Q_eph, shared_pt;

//     mbedtls_mpi_init(&d_static);
//     mbedtls_mpi_init(&d_eph);
//     mbedtls_ecp_point_init(&Q_static);
//     mbedtls_ecp_point_init(&Q_eph);
//     mbedtls_ecp_point_init(&shared_pt);

//     bench_result_t kg, enc, dec;
//     result_init(&kg, "Hybrid KeyGen");
//     result_init(&enc, "Hybrid Encaps");
//     result_init(&dec, "Hybrid Decaps");

//     size_t heap_before = get_free_heap();

//     // ---------- Warm-up (cache + context stabilization) ----------
//     for (int i = 0; i < WARMUP_ITERATIONS; i++)
//     {
//         mbedtls_ecp_gen_keypair(&grp, &d_static, &Q_static, mbedtls_ctr_drbg_random, &ctr_drbg);
//         PQCLEAN_MLKEM512_CLEAN_crypto_kem_keypair(mlkem_pk, mlkem_sk);

//         mbedtls_ecp_gen_keypair(&grp, &d_eph, &Q_eph, mbedtls_ctr_drbg_random, &ctr_drbg);
//         mbedtls_ecp_mul(&grp, &shared_pt, &d_static, &Q_eph, mbedtls_ctr_drbg_random, &ctr_drbg);
//         mbedtls_mpi_write_binary(&shared_pt.MBEDTLS_PRIVATE(X), ss_ecc, ss_len);

//         PQCLEAN_MLKEM512_CLEAN_crypto_kem_enc(mlkem_ct, ss_kem, mlkem_pk);
//         PQCLEAN_MLKEM512_CLEAN_crypto_kem_dec(ss_kem, mlkem_ct, mlkem_sk);
//     }

//     // Measure KeyGen a few times for reference
//     for (int i = 0; i < 10; i++)
//     {
//         uint64_t c0 = get_cycles();
//         int64_t t0 = esp_timer_get_time();
//         mbedtls_ecp_gen_keypair(&grp, &d_static, &Q_static, mbedtls_ctr_drbg_random, &ctr_drbg);
//         PQCLEAN_MLKEM512_CLEAN_crypto_kem_keypair(mlkem_pk, mlkem_sk);
//         int64_t t1 = esp_timer_get_time();
//         uint64_t c1 = get_cycles();
//         result_add(&kg, t1 - t0, c1 - c0);
//     }

//     // Final static keys for Encaps/Decaps
//     mbedtls_ecp_gen_keypair(&grp, &d_static, &Q_static, mbedtls_ctr_drbg_random, &ctr_drbg);
//     PQCLEAN_MLKEM512_CLEAN_crypto_kem_keypair(mlkem_pk, mlkem_sk);

//     // ---------- Measurement loop: Encaps + Decaps only (no live serial spam) ----------
//     for (int i = 0; i < NUM_ITERATIONS; i++)
//     {
//         uint64_t c0, c1;
//         int64_t t0, t1;
//         int ret;

//         // ---- Hybrid Encaps (ephemeral ECC + ECDH + ML-KEM encaps + XOR) ----
//         c0 = get_cycles();
//         t0 = esp_timer_get_time();

//         ret = mbedtls_ecp_gen_keypair(&grp, &d_eph, &Q_eph,
//                                       mbedtls_ctr_drbg_random, &ctr_drbg);
//         if (ret == 0)
//         {
//             ret = mbedtls_ecp_mul(&grp, &shared_pt, &d_static, &Q_eph,
//                                   mbedtls_ctr_drbg_random, &ctr_drbg);
//         }
//         if (ret == 0)
//         {
//             ret = mbedtls_mpi_write_binary(&shared_pt.MBEDTLS_PRIVATE(X), ss_ecc, ss_len);
//         }

//         PQCLEAN_MLKEM512_CLEAN_crypto_kem_enc(mlkem_ct, ss_kem, mlkem_pk);

//         // Combine shared secrets
//         for (size_t j = 0; j < ss_len; j++)
//         {
//             ss_hybrid[j] = ss_ecc[j] ^ ss_kem[j];
//         }

//         t1 = esp_timer_get_time();
//         c1 = get_cycles();
//         result_add(&enc, t1 - t0, c1 - c0);

//         if (ret != 0)
//         {
//             ESP_LOGW(TAG, "Encaps ECC step failed on iteration %d with code -0x%04X", i + 1, -ret);
//         }

//         // ---- Hybrid Decaps (ECDH + ML-KEM decaps + XOR) ----
//         c0 = get_cycles();
//         t0 = esp_timer_get_time();

//         ret = mbedtls_ecp_mul(&grp, &shared_pt, &d_static, &Q_eph,
//                               mbedtls_ctr_drbg_random, &ctr_drbg);
//         if (ret == 0)
//         {
//             ret = mbedtls_mpi_write_binary(&shared_pt.MBEDTLS_PRIVATE(X), ss_ecc, ss_len);
//         }

//         PQCLEAN_MLKEM512_CLEAN_crypto_kem_dec(ss_kem, mlkem_ct, mlkem_sk);

//         // Combine shared secrets
//         for (size_t j = 0; j < ss_len; j++)
//         {
//             ss_hybrid[j] = ss_ecc[j] ^ ss_kem[j];
//         }

//         t1 = esp_timer_get_time();
//         c1 = get_cycles();
//         result_add(&dec, t1 - t0, c1 - c0);

//         if (ret != 0)
//         {
//             ESP_LOGW(TAG, "Decaps ECC step failed on iteration %d with code -0x%04X", i + 1, -ret);
//         }

//         // Yield occasionally for watchdog
//         if ((i & 0x03) == 0)
//         {
//             vTaskDelay(1);
//         }
//     }

//     size_t heap_used = heap_before - get_free_heap();
//     kg.peak_heap = enc.peak_heap = dec.peak_heap = heap_used;

//     // ---------- Final Summary ----------
//     ESP_LOGI(TAG, "---------------------------------------------------------------");
//     ESP_LOGI(TAG, "%-24s | %8s | %7s | %7s | %11s | %6s",
//              "Operation", "Avg us", "Min", "Max", "Avg cycles", "HeapΔ");
//     ESP_LOGI(TAG, "---------------------------------------------------------------");
//     result_print(&kg);
//     result_print(&enc);
//     result_print(&dec);
//     ESP_LOGI(TAG, "---------------------------------------------------------------");

//     double kg_ops = 1000000.0 / ((double)kg.total_us / kg.iterations);
//     double enc_ops = 1000000.0 / ((double)enc.total_us / enc.iterations);
//     double dec_ops = 1000000.0 / ((double)dec.total_us / dec.iterations);

//     ESP_LOGI(TAG, "ECC public key size      : %u bytes", (unsigned)ecc_pk_len);
//     ESP_LOGI(TAG, "ECC secret key size      : %u bytes", (unsigned)ecc_sk_len);
//     ESP_LOGI(TAG, "ML-KEM public key size   : %u bytes", (unsigned)mlkem_pk_len);
//     ESP_LOGI(TAG, "ML-KEM secret key size   : %u bytes", (unsigned)mlkem_sk_len);
//     ESP_LOGI(TAG, "ML-KEM ciphertext size   : %u bytes", (unsigned)mlkem_ct_len);
//     ESP_LOGI(TAG, "Hybrid shared secret size: %u bytes", (unsigned)ss_len);
//     ESP_LOGI(TAG, "KeyGen throughput        : %.1f ops/sec", kg_ops);
//     ESP_LOGI(TAG, "Encaps throughput        : %.1f ops/sec", enc_ops);
//     ESP_LOGI(TAG, "Decaps throughput        : %.1f ops/sec", dec_ops);
//     ESP_LOGI(TAG, "Heap used                : %u bytes", (unsigned)heap_used);
//     ESP_LOGI(TAG, "CPU frequency            : %d MHz", esp_clk_cpu_freq() / 1000000);
//     ESP_LOGI(TAG, "===============================================================");

//     // Clean-up
//     mbedtls_mpi_free(&d_static);
//     mbedtls_mpi_free(&d_eph);
//     mbedtls_ecp_point_free(&Q_static);
//     mbedtls_ecp_point_free(&Q_eph);
//     mbedtls_ecp_point_free(&shared_pt);
//     mbedtls_ecp_group_free(&grp);
//     mbedtls_ctr_drbg_free(&ctr_drbg);
//     mbedtls_entropy_free(&entropy);

// cleanup_alloc:
//     heap_caps_free(mlkem_pk);
//     heap_caps_free(mlkem_sk);
//     heap_caps_free(mlkem_ct);
//     heap_caps_free(ss_kem);
//     heap_caps_free(ss_ecc);
//     heap_caps_free(ss_hybrid);

//     vTaskDelete(NULL);
// }

// // ------------------------------------------------------------------
// // Phase 4 – Optimized Hybrid ECDH-P256 + ML-KEM-512 Benchmark
// // ------------------------------------------------------------------
// void phase4_optimized_crypto(void)
// {
//     ESP_LOGI(TAG, "\n========== Phase 4: Optimized Hybrid ECDH-P256 + ML-KEM-512 ==========");
//     ESP_LOGI(TAG, "Dual-Core        : %s", USE_DUAL_CORE ? "ENABLED" : "DISABLED");
//     ESP_LOGI(TAG, "HW Accelerators  : %s", USE_HW_ACCELERATORS ? "ENABLED" : "DISABLED");
//     ESP_LOGI(TAG, "Iterations       : %d", NUM_ITERATIONS);

//     // ---- Size constants ----
//     const size_t mlkem_pk_len = PQCLEAN_MLKEM512_CLEAN_CRYPTO_PUBLICKEYBYTES;
//     const size_t mlkem_sk_len = PQCLEAN_MLKEM512_CLEAN_CRYPTO_SECRETKEYBYTES;
//     const size_t mlkem_ct_len = PQCLEAN_MLKEM512_CLEAN_CRYPTO_CIPHERTEXTBYTES;
//     const size_t ss_len = PQCLEAN_MLKEM512_CLEAN_CRYPTO_BYTES; // 32

//     // ---- 1. Memory optimization: everything on heap ----
//     uint8_t *mlkem_pk = heap_caps_malloc(mlkem_pk_len, MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL);
//     uint8_t *mlkem_sk = heap_caps_malloc(mlkem_sk_len, MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL);
//     uint8_t *mlkem_ct = heap_caps_malloc(mlkem_ct_len, MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL);
//     uint8_t *ss_kem = heap_caps_malloc(ss_len, MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL);
//     uint8_t *ss_ecc = heap_caps_malloc(ss_len, MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL);
//     uint8_t *ss_hybrid = heap_caps_malloc(ss_len, MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL);

//     if (!mlkem_pk || !mlkem_sk || !mlkem_ct || !ss_kem || !ss_ecc || !ss_hybrid)
//     {
//         ESP_LOGE(TAG, "Heap allocation failed");
//         goto cleanup;
//     }

//     // ---- mbedTLS contexts ----
//     mbedtls_entropy_context entropy;
//     mbedtls_ctr_drbg_context ctr_drbg;
//     mbedtls_ecp_group grp;

//     mbedtls_entropy_init(&entropy);
//     mbedtls_ctr_drbg_init(&ctr_drbg);
//     mbedtls_ecp_group_init(&grp);
//     mbedtls_ecp_group_load(&grp, MBEDTLS_ECP_DP_SECP256R1);

//     const char *pers = "phase4_hybrid_opt";
//     if (mbedtls_ctr_drbg_seed(&ctr_drbg, mbedtls_entropy_func, &entropy,
//                               (const uint8_t *)pers, strlen(pers)) != 0)
//     {
//         ESP_LOGE(TAG, "mbedtls_ctr_drbg_seed failed");
//         goto cleanup_mbedtls;
//     }

//     mbedtls_mpi d_static, d_eph;
//     mbedtls_ecp_point Q_static, Q_eph, shared_pt;

//     mbedtls_mpi_init(&d_static);
//     mbedtls_mpi_init(&d_eph);
//     mbedtls_ecp_point_init(&Q_static);
//     mbedtls_ecp_point_init(&Q_eph);
//     mbedtls_ecp_point_init(&shared_pt);

//     // ---- Result collectors ----
//     bench_result_t kg, enc, dec;
//     result_init(&kg, "Opt Hybrid KeyGen");
//     result_init(&enc, "Opt Hybrid Encaps");
//     result_init(&dec, "Opt Hybrid Decaps");

//     size_t heap_before = get_free_heap();

//     // ---- Warm-up ----
//     for (int i = 0; i < WARMUP_ITERATIONS; i++)
//     {
//         mbedtls_ecp_gen_keypair(&grp, &d_static, &Q_static, mbedtls_ctr_drbg_random, &ctr_drbg);
//         PQCLEAN_MLKEM512_CLEAN_crypto_kem_keypair(mlkem_pk, mlkem_sk);

//         mbedtls_ecp_gen_keypair(&grp, &d_eph, &Q_eph, mbedtls_ctr_drbg_random, &ctr_drbg);
//         mbedtls_ecp_mul(&grp, &shared_pt, &d_static, &Q_eph, mbedtls_ctr_drbg_random, &ctr_drbg);
//         mbedtls_mpi_write_binary(&shared_pt.MBEDTLS_PRIVATE(X), ss_ecc, ss_len);

//         PQCLEAN_MLKEM512_CLEAN_crypto_kem_enc(mlkem_ct, ss_kem, mlkem_pk);
//         PQCLEAN_MLKEM512_CLEAN_crypto_kem_dec(ss_kem, mlkem_ct, mlkem_sk);
//     }

//     // Measure KeyGen a few times for reference
//     for (int i = 0; i < 10; i++)
//     {
//         uint64_t c0 = get_cycles();
//         int64_t t0 = esp_timer_get_time();
//         mbedtls_ecp_gen_keypair(&grp, &d_static, &Q_static, mbedtls_ctr_drbg_random, &ctr_drbg);
//         PQCLEAN_MLKEM512_CLEAN_crypto_kem_keypair(mlkem_pk, mlkem_sk);
//         int64_t t1 = esp_timer_get_time();
//         uint64_t c1 = get_cycles();
//         result_add(&kg, t1 - t0, c1 - c0);
//     }

//     // Final static keys
//     mbedtls_ecp_gen_keypair(&grp, &d_static, &Q_static, mbedtls_ctr_drbg_random, &ctr_drbg);
//     PQCLEAN_MLKEM512_CLEAN_crypto_kem_keypair(mlkem_pk, mlkem_sk);

//     // ---- Measurement loop: Encaps + Decaps only (no live serial spam) ----
//     for (int i = 0; i < NUM_ITERATIONS; i++)
//     {
//         uint64_t c0, c1;
//         int64_t t0, t1;
//         int ret;

//         // ===== Encaps =====
//         c0 = get_cycles();
//         t0 = esp_timer_get_time();

//         ret = mbedtls_ecp_gen_keypair(&grp, &d_eph, &Q_eph,
//                                       mbedtls_ctr_drbg_random, &ctr_drbg);
//         if (ret == 0)
//         {
//             ret = mbedtls_ecp_mul(&grp, &shared_pt, &d_static, &Q_eph,
//                                   mbedtls_ctr_drbg_random, &ctr_drbg);
//         }
//         if (ret == 0)
//         {
//             ret = mbedtls_mpi_write_binary(&shared_pt.MBEDTLS_PRIVATE(X), ss_ecc, ss_len);
//         }

//         PQCLEAN_MLKEM512_CLEAN_crypto_kem_enc(mlkem_ct, ss_kem, mlkem_pk);

//         // Combine shared secrets (XOR)
//         for (size_t j = 0; j < ss_len; j++)
//         {
//             ss_hybrid[j] = ss_ecc[j] ^ ss_kem[j];
//         }

//         t1 = esp_timer_get_time();
//         c1 = get_cycles();
//         result_add(&enc, t1 - t0, c1 - c0);

//         // ===== Decaps =====
//         c0 = get_cycles();
//         t0 = esp_timer_get_time();

//         ret = mbedtls_ecp_mul(&grp, &shared_pt, &d_static, &Q_eph,
//                               mbedtls_ctr_drbg_random, &ctr_drbg);
//         if (ret == 0)
//         {
//             ret = mbedtls_mpi_write_binary(&shared_pt.MBEDTLS_PRIVATE(X), ss_ecc, ss_len);
//         }

//         PQCLEAN_MLKEM512_CLEAN_crypto_kem_dec(ss_kem, mlkem_ct, mlkem_sk);

//         for (size_t j = 0; j < ss_len; j++)
//         {
//             ss_hybrid[j] = ss_ecc[j] ^ ss_kem[j];
//         }

//         t1 = esp_timer_get_time();
//         c1 = get_cycles();
//         result_add(&dec, t1 - t0, c1 - c0);

//         if ((i & 0x03) == 0)
//         {
//             vTaskDelay(1); // feed the watchdog
//         }
//     }

//     size_t heap_used = heap_before - get_free_heap();
//     kg.peak_heap = enc.peak_heap = dec.peak_heap = heap_used;

//     // ---- Final Summary ----
//     ESP_LOGI(TAG, "---------------------------------------------------------------");
//     ESP_LOGI(TAG, "%-24s | %8s | %7s | %7s | %11s | %6s",
//              "Operation", "Avg us", "Min", "Max", "Avg cycles", "HeapΔ");
//     ESP_LOGI(TAG, "---------------------------------------------------------------");
//     result_print(&kg);
//     result_print(&enc);
//     result_print(&dec);
//     ESP_LOGI(TAG, "---------------------------------------------------------------");

//     double kg_ops = 1000000.0 / ((double)kg.total_us / kg.iterations);
//     double enc_ops = 1000000.0 / ((double)enc.total_us / enc.iterations);
//     double dec_ops = 1000000.0 / ((double)dec.total_us / dec.iterations);

//     ESP_LOGI(TAG, "ML-KEM public key size   : %u bytes", (unsigned)mlkem_pk_len);
//     ESP_LOGI(TAG, "ML-KEM secret key size   : %u bytes", (unsigned)mlkem_sk_len);
//     ESP_LOGI(TAG, "ML-KEM ciphertext size   : %u bytes", (unsigned)mlkem_ct_len);
//     ESP_LOGI(TAG, "Hybrid shared secret size: %u bytes", (unsigned)ss_len);
//     ESP_LOGI(TAG, "KeyGen throughput        : %.1f ops/sec", kg_ops);
//     ESP_LOGI(TAG, "Encaps throughput        : %.1f ops/sec", enc_ops);
//     ESP_LOGI(TAG, "Decaps throughput        : %.1f ops/sec", dec_ops);
//     ESP_LOGI(TAG, "Heap used                : %u bytes", (unsigned)heap_used);
//     ESP_LOGI(TAG, "CPU frequency            : %d MHz", esp_clk_cpu_freq() / 1000000);
//     ESP_LOGI(TAG, "===============================================================");

//     // ---- Cleanup ----
//     mbedtls_mpi_free(&d_static);
//     mbedtls_mpi_free(&d_eph);
//     mbedtls_ecp_point_free(&Q_static);
//     mbedtls_ecp_point_free(&Q_eph);
//     mbedtls_ecp_point_free(&shared_pt);

// cleanup_mbedtls:
//     mbedtls_ecp_group_free(&grp);
//     mbedtls_ctr_drbg_free(&ctr_drbg);
//     mbedtls_entropy_free(&entropy);

// cleanup:
//     if (mlkem_pk)
//         heap_caps_free(mlkem_pk);
//     if (mlkem_sk)
//         heap_caps_free(mlkem_sk);
//     if (mlkem_ct)
//         heap_caps_free(mlkem_ct);
//     if (ss_kem)
//         heap_caps_free(ss_kem);
//     if (ss_ecc)
//         heap_caps_free(ss_ecc);
//     if (ss_hybrid)
//         heap_caps_free(ss_hybrid);

//     ESP_LOGI(TAG, "=== Phase 4 Optimized Hybrid Crypto Finished ===");
// }

// // ------------------------------------------------------------------
// // Task wrapper – this is what app_main launches
// // ------------------------------------------------------------------
// static void phase4_optimized_crypto_task(void *pv)
// {
//     vTaskDelay(pdMS_TO_TICKS(300)); // let the header print first
//     phase4_optimized_crypto();      // run the real optimized hybrid
//     vTaskDelete(NULL);
// }

// // ============================================================
// //  Hybrid X25519 + ML-KEM-512  +  ChaCha20-Poly1305 AEAD
// // ============================================================

// /**
//  * Hybrid KDF: Combines X25519 and ML-KEM shared secrets using SHA256
//  * @param ss_x25519  32-byte X25519 shared secret
//  * @param ss_mlkem   32-byte ML-KEM shared secret
//  * @param output     output buffer for derived key
//  * @param output_len desired key length (typically 32 for ChaCha20)
//  */
// static void hybrid_kdf(const uint8_t *ss_x25519, const uint8_t *ss_mlkem,
//                        uint8_t *output, size_t output_len)
// {
//     // Concatenate the two shared secrets as input key material
//     uint8_t ikm[64];  // 32 + 32 bytes
//     memcpy(ikm, ss_x25519, 32);
//     memcpy(ikm + 32, ss_mlkem, 32);

//     // Simple KDF: SHA256(ikm || counter)
//     mbedtls_sha256_context sha_ctx;
    
//     // Derive key material using SHA256 with counter
//     for (size_t i = 0; i < output_len; i += 32) {
//         uint8_t counter = (uint8_t)(i / 32);
//         mbedtls_sha256_init(&sha_ctx);
//         mbedtls_sha256_starts(&sha_ctx, 0);  // 0 = SHA256 (not SHA224)
//         mbedtls_sha256_update(&sha_ctx, ikm, 64);
//         mbedtls_sha256_update(&sha_ctx, &counter, 1);
        
//         uint8_t hash[32];
//         mbedtls_sha256_finish(&sha_ctx, hash);
//         mbedtls_sha256_free(&sha_ctx);
        
//         size_t copy_len = (output_len - i > 32) ? 32 : (output_len - i);
//         memcpy(output + i, hash, copy_len);
//     }
// }

// static void bench_hybrid_x25519_mlkem_chacha_task(void *pv)
// {
//     ESP_LOGI(TAG, "\n========== Hybrid X25519 + ML-KEM-512 + ChaCha20-Poly1305 ==========");
//     ESP_LOGI(TAG, "Iterations: %d", NUM_ITERATIONS);

//     // ---- sizes ----
//     const size_t x25519_len   = 32;
//     const size_t mlkem_pk_len = PQCLEAN_MLKEM512_CLEAN_CRYPTO_PUBLICKEYBYTES;
//     const size_t mlkem_sk_len = PQCLEAN_MLKEM512_CLEAN_CRYPTO_SECRETKEYBYTES;
//     const size_t mlkem_ct_len = PQCLEAN_MLKEM512_CLEAN_CRYPTO_CIPHERTEXTBYTES;
//     const size_t ss_len       = 32;                     // both KEMs produce 32-byte SS
//     const size_t aead_key_len = 32;                     // ChaCha20-Poly1305 key
//     const size_t nonce_len    = 12;
//     const size_t tag_len      = 16;

//     // ---- buffers (all on heap) ----
//     uint8_t *mlkem_pk   = heap_caps_malloc(mlkem_pk_len, MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL);
//     uint8_t *mlkem_sk   = heap_caps_malloc(mlkem_sk_len, MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL);
//     uint8_t *mlkem_ct   = heap_caps_malloc(mlkem_ct_len, MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL);
//     uint8_t *ss_mlkem   = heap_caps_malloc(ss_len,       MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL);
//     uint8_t *ss_x25519  = heap_caps_malloc(ss_len,       MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL);
//     uint8_t *ss_hybrid  = heap_caps_malloc(aead_key_len, MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL);
//     uint8_t *nonce      = heap_caps_malloc(nonce_len,    MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL);
//     uint8_t *tag        = heap_caps_malloc(tag_len,      MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL);

//     // Sensor payload buffers
//     uint8_t payload[32];          // our compact 15-byte sensor reading + padding
//     uint8_t ciphertext[32];
//     uint8_t decrypted[32];
//     size_t  payload_len = 0;

//     if (!mlkem_pk || !mlkem_sk || !mlkem_ct || !ss_mlkem || !ss_x25519 ||
//         !ss_hybrid || !nonce || !tag) {
//         ESP_LOGE(TAG, "Hybrid allocation failed");
//         vTaskDelete(NULL);
//         return;
//     }

//     // ---- mbedTLS RNG ----
//     mbedtls_entropy_context entropy;
//     mbedtls_ctr_drbg_context ctr_drbg;
//     mbedtls_entropy_init(&entropy);
//     mbedtls_ctr_drbg_init(&ctr_drbg);

//     const char *pers = "esp32_x25519_mlkem_chacha";
//     if (mbedtls_ctr_drbg_seed(&ctr_drbg, mbedtls_entropy_func, &entropy,
//                               (const uint8_t *)pers, strlen(pers)) != 0) {
//         ESP_LOGE(TAG, "CTR-DRBG seed failed");
//         goto cleanup;
//     }

//     // ---- X25519 structures (separate ECP components) ----
//     mbedtls_ecp_group grp;
//     mbedtls_mpi d_static, d_eph, z_static;
//     mbedtls_ecp_point Q_static, Q_eph;
    
//     mbedtls_ecp_group_init(&grp);
//     mbedtls_mpi_init(&d_static);
//     mbedtls_mpi_init(&d_eph);
//     mbedtls_mpi_init(&z_static);
//     mbedtls_ecp_point_init(&Q_static);
//     mbedtls_ecp_point_init(&Q_eph);

//     if (mbedtls_ecp_group_load(&grp, MBEDTLS_ECP_DP_CURVE25519) != 0) {
//         ESP_LOGE(TAG, "X25519 group load failed");
//         goto cleanup;
//     }

//     // ---- ChaCha20-Poly1305 context ----
//     mbedtls_chachapoly_context chacha;
//     mbedtls_chachapoly_init(&chacha);

//     // ---- result collectors ----
//     bench_result_t kg, enc, dec, aead_enc, aead_dec;
//     result_init(&kg,      "Hybrid-X25519+MLKEM KeyGen");
//     result_init(&enc,     "Hybrid-X25519+MLKEM Encaps");
//     result_init(&dec,     "Hybrid-X25519+MLKEM Decaps");
//     result_init(&aead_enc,"ChaCha20-Poly1305 Encrypt");
//     result_init(&aead_dec,"ChaCha20-Poly1305 Decrypt");

//     size_t heap_before = get_free_heap();

//     // ---- Warm-up + generate keys once ----
//     for (int i = 0; i < WARMUP_ITERATIONS; i++) {
//         mbedtls_ecdh_gen_public(&grp, &d_static, &Q_static,
//                                 mbedtls_ctr_drbg_random, &ctr_drbg);
//         PQCLEAN_MLKEM512_CLEAN_crypto_kem_keypair(mlkem_pk, mlkem_sk);

//         mbedtls_ecdh_gen_public(&grp, &d_eph, &Q_eph,
//                                 mbedtls_ctr_drbg_random, &ctr_drbg);
//         mbedtls_ecdh_compute_shared(&grp, &z_static, &Q_eph, &d_static,
//                                     mbedtls_ctr_drbg_random, &ctr_drbg);
//         mbedtls_mpi_write_binary(&z_static, ss_x25519, 32);
//         PQCLEAN_MLKEM512_CLEAN_crypto_kem_enc(mlkem_ct, ss_mlkem, mlkem_pk);
//         PQCLEAN_MLKEM512_CLEAN_crypto_kem_dec(ss_mlkem, mlkem_ct, mlkem_sk);
//         hybrid_kdf(ss_x25519, ss_mlkem, ss_hybrid, aead_key_len);
//     }

//     // Measure KeyGen a few times for reference
//     for (int i = 0; i < 10; i++) {
//         uint64_t c0 = get_cycles();
//         int64_t t0 = esp_timer_get_time();
//         mbedtls_ecdh_gen_public(&grp, &d_static, &Q_static,
//                                 mbedtls_ctr_drbg_random, &ctr_drbg);
//         PQCLEAN_MLKEM512_CLEAN_crypto_kem_keypair(mlkem_pk, mlkem_sk);
//         int64_t t1 = esp_timer_get_time();
//         uint64_t c1 = get_cycles();
//         result_add(&kg, t1 - t0, c1 - c0);
//     }

//     // Final static keys
//     mbedtls_ecdh_gen_public(&grp, &d_static, &Q_static,
//                             mbedtls_ctr_drbg_random, &ctr_drbg);
//     PQCLEAN_MLKEM512_CLEAN_crypto_kem_keypair(mlkem_pk, mlkem_sk);

//     // ---------- Measurement loop: Encaps/Decaps + AEAD Encrypt/Decrypt (no live serial spam) ----------
//     for (int i = 0; i < NUM_ITERATIONS; i++) {
//         uint64_t c0, c1;
//         int64_t  t0, t1;
//         int ret;

//         // ============================================================
//         // Hybrid Encaps  (ephemeral X25519 + ECDH + ML-KEM encaps)
//         // ============================================================
//         c0 = get_cycles();
//         t0 = esp_timer_get_time();

//         // ephemeral X25519 keypair
//         ret = mbedtls_ecdh_gen_public(&grp, &d_eph, &Q_eph,
//                                       mbedtls_ctr_drbg_random, &ctr_drbg);

//         // ECDH: static * ephemeral
//         ret |= mbedtls_ecdh_compute_shared(&grp, &z_static, &Q_eph, &d_static,
//                                            mbedtls_ctr_drbg_random, &ctr_drbg);

//         // extract 32-byte X25519 shared secret
//         mbedtls_mpi_write_binary(&z_static, ss_x25519, 32);

//         // ML-KEM encaps
//         PQCLEAN_MLKEM512_CLEAN_crypto_kem_enc(mlkem_ct, ss_mlkem, mlkem_pk);

//         // Proper hybrid KDF → AEAD key
//         hybrid_kdf(ss_x25519, ss_mlkem, ss_hybrid, aead_key_len);

//         t1 = esp_timer_get_time();
//         c1 = get_cycles();
//         result_add(&enc, t1 - t0, c1 - c0);

//         // ============================================================
//         // Hybrid Decaps  (symmetric to encaps)
//         // ============================================================
//         c0 = get_cycles();
//         t0 = esp_timer_get_time();

//         ret = mbedtls_ecdh_compute_shared(&grp, &z_static, &Q_eph, &d_static,
//                                           mbedtls_ctr_drbg_random, &ctr_drbg);
//         mbedtls_mpi_write_binary(&z_static, ss_x25519, 32);

//         PQCLEAN_MLKEM512_CLEAN_crypto_kem_dec(ss_mlkem, mlkem_ct, mlkem_sk);
//         hybrid_kdf(ss_x25519, ss_mlkem, ss_hybrid, aead_key_len);

//         t1 = esp_timer_get_time();
//         c1 = get_cycles();
//         result_add(&dec, t1 - t0, c1 - c0);

//         // ============================================================
//         // Sensor payload + ChaCha20-Poly1305 AEAD Encrypt / Decrypt
//         // ============================================================
//         mock_sensor_update();
//         sensor_to_payload(&g_sensor, payload, &payload_len);

//         // fresh random nonce every message
//         mbedtls_ctr_drbg_random(&ctr_drbg, nonce, nonce_len);

//         // Encrypt + tag
//         c0 = get_cycles();
//         t0 = esp_timer_get_time();

//         mbedtls_chachapoly_setkey(&chacha, ss_hybrid);
//         ret = mbedtls_chachapoly_encrypt_and_tag(&chacha,
//                                                  payload_len,
//                                                  nonce,
//                                                  NULL, 0,          // no AAD
//                                                  payload,
//                                                  ciphertext,
//                                                  tag);

//         t1 = esp_timer_get_time();
//         c1 = get_cycles();
//         result_add(&aead_enc, t1 - t0, c1 - c0);

//         // Decrypt + verify
//         c0 = get_cycles();
//         t0 = esp_timer_get_time();

//         ret = mbedtls_chachapoly_auth_decrypt(&chacha,
//                                               payload_len,
//                                               nonce,
//                                               NULL, 0,
//                                               tag,
//                                               ciphertext,
//                                               decrypted);

//         t1 = esp_timer_get_time();
//         c1 = get_cycles();
//         result_add(&aead_dec, t1 - t0, c1 - c0);

//         if (ret != 0 || memcmp(payload, decrypted, payload_len) != 0) {
//             ESP_LOGW(TAG, "AEAD verify failed on iter %d", i + 1);
//         }

//         if ((i & 0x03) == 0) vTaskDelay(1);
//     }

//     // ---- final summary ----
//     size_t heap_used = heap_before - get_free_heap();
//     kg.peak_heap = enc.peak_heap = dec.peak_heap =
//     aead_enc.peak_heap = aead_dec.peak_heap = heap_used;

//     ESP_LOGI(TAG, "---------------------------------------------------------------");
//     ESP_LOGI(TAG, "%-32s | %8s | %7s | %7s | %11s | %6s",
//              "Operation", "Avg us", "Min", "Max", "Avg cycles", "HeapΔ");
//     ESP_LOGI(TAG, "---------------------------------------------------------------");
//     result_print(&kg);
//     result_print(&enc);
//     result_print(&dec);
//     result_print(&aead_enc);
//     result_print(&aead_dec);
//     ESP_LOGI(TAG, "---------------------------------------------------------------");

//     ESP_LOGI(TAG, "X25519 public/private     : 32 / 32 bytes");
//     ESP_LOGI(TAG, "ML-KEM-512 pk/sk/ct       : %u / %u / %u bytes",
//              (unsigned)mlkem_pk_len, (unsigned)mlkem_sk_len, (unsigned)mlkem_ct_len);
//     ESP_LOGI(TAG, "Hybrid AEAD key           : %u bytes", (unsigned)aead_key_len);
//     ESP_LOGI(TAG, "ChaCha20-Poly1305 nonce/tag: %u / %u bytes", (unsigned)nonce_len, (unsigned)tag_len);
//     ESP_LOGI(TAG, "Sensor payload size       : %u bytes", (unsigned)payload_len);
//     ESP_LOGI(TAG, "Heap used                 : %u bytes", (unsigned)heap_used);
//     ESP_LOGI(TAG, "CPU frequency             : %d MHz", esp_clk_cpu_freq() / 1000000);
//     ESP_LOGI(TAG, "===============================================================");

// cleanup:
//     mbedtls_chachapoly_free(&chacha);
//     mbedtls_mpi_free(&d_static);
//     mbedtls_mpi_free(&d_eph);
//     mbedtls_mpi_free(&z_static);
//     mbedtls_ecp_point_free(&Q_static);
//     mbedtls_ecp_point_free(&Q_eph);
//     mbedtls_ecp_group_free(&grp);
//     mbedtls_ctr_drbg_free(&ctr_drbg);
//     mbedtls_entropy_free(&entropy);

//     heap_caps_free(mlkem_pk);
//     heap_caps_free(mlkem_sk);
//     heap_caps_free(mlkem_ct);
//     heap_caps_free(ss_mlkem);
//     heap_caps_free(ss_x25519);
//     heap_caps_free(ss_hybrid);
//     heap_caps_free(nonce);
//     heap_caps_free(tag);

//     vTaskDelete(NULL);
// }

// // ============================================================
// //  Live sensor task (optional background stream)
// // ============================================================
// __attribute__((unused)) static void live_sensor_task(void *pv)
// {
//     while (1)
//     {
//         mock_sensor_update();
//         print_sensor(&g_sensor);
//         vTaskDelay(pdMS_TO_TICKS(SENSOR_INTERVAL_MS));
//     }
// }

// void app_main(void)
// {
//     // ------------------------------------------------------------------
//     // System information
//     // ------------------------------------------------------------------
//     ESP_LOGI(TAG, "=======================================================");
//     ESP_LOGI(TAG, " ESP32 Hybrid Cryptography Benchmark Suite");
//     ESP_LOGI(TAG, "=======================================================");
//     ESP_LOGI(TAG, " CPU Frequency : %d MHz", esp_clk_cpu_freq() / 1000000);
//     ESP_LOGI(TAG, " Free Heap     : %u bytes", (unsigned)get_free_heap());
//     ESP_LOGI(TAG, " Min Free Heap : %u bytes", (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT));
//     ESP_LOGI(TAG, " Running on    : Core %d", xPortGetCoreID());
//     ESP_LOGI(TAG, " Iterations    : %d", NUM_ITERATIONS);
//     ESP_LOGI(TAG, "=======================================================");

//     // Optional: start background sensor task (uncomment if needed)
//     // xTaskCreatePinnedToCore(live_sensor_task, "sensor", 4096, NULL, 3, NULL, 1);

// #if defined(BENCH_MLKEM)
//     // ---------------------------------------------------------------
//     // Phase 3 style: Pure ML-KEM benchmark
//     // ---------------------------------------------------------------
//     ESP_LOGI(TAG, ">> Test Case: Pure ML-KEM-512 Benchmark");
//     xTaskCreatePinnedToCore(bench_mlkem_task, "kem", STACK_SIZE_KEM, NULL, 5, NULL, 0);
//     vTaskDelay(pdMS_TO_TICKS(25000));

// #elif defined(BENCH_MLDSA)
//     // ---------------------------------------------------------------
//     // Phase 3 style: Pure ML-DSA benchmark
//     // ---------------------------------------------------------------
//     ESP_LOGI(TAG, ">> Test Case: Pure ML-DSA Benchmark");
//     xTaskCreatePinnedToCore(bench_mldsa_task, "dsa", STACK_SIZE_DSA, NULL, 5, NULL, 0);
//     vTaskDelay(pdMS_TO_TICKS(50000));

// #elif defined(BENCH_CLASSICAL)
//     // ---------------------------------------------------------------
//     // Phase 3 style: Classical (ECDH / ECDSA) baseline
//     // ---------------------------------------------------------------
//     ESP_LOGI(TAG, ">> Test Case: Classical Cryptography Baseline");
//     xTaskCreatePinnedToCore(bench_classical_task, "class", 32768, NULL, 5, NULL, 0);
//     vTaskDelay(pdMS_TO_TICKS(20000));

// #elif defined(BENCH_HYBRID_KEM)
//     // ---------------------------------------------------------------
//     // Phase 3 style: Hybrid ECDH-P256 + ML-KEM
//     // ---------------------------------------------------------------
//     ESP_LOGI(TAG, ">> Test Case: Hybrid ECDH-P256 + ML-KEM-512");
//     xTaskCreatePinnedToCore(bench_hybrid_ecc_mlkem_task, "hybrid_kem",
//                             STACK_SIZE_HYBRID_KEM, NULL, 5, NULL, 0);
//     vTaskDelay(pdMS_TO_TICKS(35000));

// #elif defined(BENCH_OPTIMIZED)
//     // ---------------------------------------------------------------
//     // Phase 4: Optimization test cases
//     // ---------------------------------------------------------------
//     ESP_LOGI(TAG, ">> Test Case: Phase 4 Optimization Suite");
//     ESP_LOGI(TAG, "   Dual-Core     : %s", USE_DUAL_CORE ? "ENABLED" : "DISABLED");
//     ESP_LOGI(TAG, "   HW Accelerators: %s", USE_HW_ACCELERATORS ? "ENABLED" : "DISABLED");

//     // Run the optimized crypto demonstration
//     xTaskCreatePinnedToCore(
//         phase4_optimized_crypto_task, // wrapper around phase4_optimized_crypto()
//         "phase4_opt",
//         16384, // larger stack for safety
//         NULL,
//         5,
//         NULL,
//         0 // Core 0
//     );

//     // Give enough time for the optimization run + logging
//     vTaskDelay(pdMS_TO_TICKS(15000));

// #elif defined(BENCH_OPT_COMPARE)
//     // ---------------------------------------------------------------
//     // Advanced: Run multiple optimization configurations sequentially
//     // (useful for generating comparison tables)
//     // ---------------------------------------------------------------
//     ESP_LOGI(TAG, ">> Test Case: Optimization Comparison Suite");

//     ESP_LOGI(TAG, "--- 1/3 Baseline (no dual-core, no HW accel) ---");
//     // You can temporarily force defines or call a baseline version here
//     xTaskCreatePinnedToCore(phase4_optimized_crypto_task, "opt_base", 16384, NULL, 5, NULL, 0);
//     vTaskDelay(pdMS_TO_TICKS(12000));

//     ESP_LOGI(TAG, "--- 2/3 Dual-Core only ---");
//     // Re-run with different flags if you implement runtime switches
//     xTaskCreatePinnedToCore(phase4_optimized_crypto_task, "opt_dual", 16384, NULL, 5, NULL, 0);
//     vTaskDelay(pdMS_TO_TICKS(12000));

//     ESP_LOGI(TAG, "--- 3/3 Full Optimization (Dual-Core + HW) ---");
//     xTaskCreatePinnedToCore(phase4_optimized_crypto_task, "opt_full", 16384, NULL, 5, NULL, 0);
//     vTaskDelay(pdMS_TO_TICKS(15000));

// #elif defined(BENCH_HYBRID_X25519_CHACHA)
//     ESP_LOGI(TAG, ">> Test Case: Hybrid X25519 + ML-KEM-512 + ChaCha20-Poly1305");
//     xTaskCreatePinnedToCore(bench_hybrid_x25519_mlkem_chacha_task,
//                             "hybrid_x25519",
//                             STACK_SIZE_HYBRID_KEM,
//                             NULL, 5, NULL, 0);
//     vTaskDelay(pdMS_TO_TICKS(40000));

// #else
// // ---------------------------------------------------------------
// // Default fallback
// // ---------------------------------------------------------------
// #warning "No BENCH_* target defined! Defaulting to BENCH_OPTIMIZED"
//     ESP_LOGW(TAG, "No benchmark macro defined. Defaulting to Phase 4 Optimized test.");

//     xTaskCreatePinnedToCore(
//         phase4_optimized_crypto_task,
//         "phase4_opt",
//         16384,
//         NULL,
//         5,
//         NULL,
//         0);
//     vTaskDelay(pdMS_TO_TICKS(15000));
// #endif

//     // ------------------------------------------------------------------
//     // Keep main task alive (useful for monitoring / future smart-home loop)
//     // ------------------------------------------------------------------
//     while (1)
//     {
//         vTaskDelay(pdMS_TO_TICKS(10000));
//         ESP_LOGI(TAG, "[Alive] Free heap: %u | Min free: %u",
//                  (unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT),
//                  (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT));
//     }
// }
// Grok