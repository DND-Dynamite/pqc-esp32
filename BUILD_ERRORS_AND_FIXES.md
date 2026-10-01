# Build Errors and Fixes

## Summary
This document catalogs all compilation and linking errors encountered during the ESP32 PQC project build, along with their resolutions.

---

## Error 1: Missing Header File `esp_clk.h`

### Error Message
```
/home/lenovo/pqc_esp32/main/pqc_benchmark.c:25:10: fatal error: esp_clk.h: No such file or directory
   25 | #include "esp_clk.h"
      |          ^~~~~~~~~~~
compilation terminated.
```

### Root Cause
The header `esp_clk.h` is located in the private directory `esp_private/` within the `esp_hw_support` component. Direct inclusion without the path prefix fails.

### Solution
**File:** `main/pqc_benchmark.c`

Changed:
```c
#include "esp_clk.h"
```

To:
```c
#include "esp_private/esp_clk.h"
```

### Status
✅ Fixed

---

## Error 2: ML-DSA Function Signature Mismatch

### Error Message
```
/home/lenovo/pqc_esp32/main/pqc_benchmark.c:266:9: error: too many arguments to function 'PQCLEAN_MLDSA44_CLEAN_crypto_sign_signature'
  266 |         PQCLEAN_MLDSA44_CLEAN_crypto_sign_signature(sig, &siglen, payload, payload_len, NULL, 0, sk);
      |         ^~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
In file included from /home/lenovo/pqc_esp32/main/pqc_benchmark.c:37:
/home/lenovo/pqc_esp32/components/pqcmicro/src/crypto/ml-dsa-44/api.h:34:5: note: declared here
   34 | int PQCLEAN_MLDSA44_CLEAN_crypto_sign_signature(uint8_t *sig, size_t *siglen,
      |     ^~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
```

### Root Cause
The code was calling ML-DSA functions with extra context parameters (`NULL, 0`) intended for the context-aware versions of these functions. The non-context versions only accept 5 parameters.

### Solution
**File:** `main/pqc_benchmark.c`

Removed extra context parameters from 4 function calls:

1. **Warmup iteration call (line ~266):**
   ```c
   // Before:
   PQCLEAN_MLDSA44_CLEAN_crypto_sign_signature(sig, &siglen, payload, payload_len, NULL, 0, sk);
   PQCLEAN_MLDSA44_CLEAN_crypto_sign_verify(sig, siglen, payload, payload_len, NULL, 0, pk);
   
   // After:
   PQCLEAN_MLDSA44_CLEAN_crypto_sign_signature(sig, &siglen, payload, payload_len, sk);
   PQCLEAN_MLDSA44_CLEAN_crypto_sign_verify(sig, siglen, payload, payload_len, pk);
   ```

2. **Benchmarking loop calls (lines ~283, ~289):**
   ```c
   // Before:
   PQCLEAN_MLDSA44_CLEAN_crypto_sign_signature(sig, &siglen, payload, payload_len, NULL, 0, sk);
   int ok = PQCLEAN_MLDSA44_CLEAN_crypto_sign_verify(sig, siglen, payload, payload_len, NULL, 0, pk);
   
   // After:
   PQCLEAN_MLDSA44_CLEAN_crypto_sign_signature(sig, &siglen, payload, payload_len, sk);
   int ok = PQCLEAN_MLDSA44_CLEAN_crypto_sign_verify(sig, siglen, payload, payload_len, pk);
   ```

### Status
✅ Fixed

---

## Error 3: Unused Function Warning

### Error Message
```
/home/lenovo/pqc_esp32/main/pqc_benchmark.c:377:13: warning: 'live_sensor_task' defined but not used [-Wunused-function]
 377 | static void live_sensor_task(void *pv)
      |             ^~~~~~~~~~~~~~~~
```

### Root Cause
The `live_sensor_task` function is defined but not called anywhere (its `xTaskCreate` call is commented out), triggering a compiler warning.

### Solution
**File:** `main/pqc_benchmark.c`

Added GCC attribute to suppress the warning:

```c
// Before:
static void live_sensor_task(void *pv)

// After:
__attribute__((unused))
static void live_sensor_task(void *pv)
```

### Status
✅ Fixed

---

## Error 4: mbedTLS ECDH Context Structure Issue

### Error Message
```
/home/lenovo/pqc_esp32/main/hybrid_ecc_mlkem_task.c:69:39: error: 'mbedtls_ecdh_context' has no member named 'grp'
   69 |     ret = mbedtls_ecp_group_load(&ecdh.grp, MBEDTLS_ECP_DP_SECP256R1);
      |                                       ^
/home/lenovo/pqc_esp32/main/hybrid_ecc_mlkem_task.c:76:40: error: 'mbedtls_ecdh_context' has no member named 'd'
   76 |     ret = mbedtls_ecdh_gen_public(&ecdh.grp, &ecdh.d, &ecdh.Q, ...);
      |                                        ^
```

### Root Cause
The mbedTLS version in ESP-IDF uses an older API where `mbedtls_ecdh_context` members (`grp`, `d`, `Q`) are private or not directly accessible. The newer API requires separate structures.

### Solution
**File:** `main/hybrid_ecc_mlkem_task.c`

Replaced the ECDH context approach with separate ECP structures:

**Before:**
```c
mbedtls_ecdh_context ecdh;
mbedtls_ecdh_init(&ecdh);

ret = mbedtls_ecp_group_load(&ecdh.grp, MBEDTLS_ECP_DP_SECP256R1);
ret = mbedtls_ecdh_gen_public(&ecdh.grp, &ecdh.d, &ecdh.Q, ...);

// Cleanup:
mbedtls_ecdh_free(&ecdh);
```

**After:**
```c
mbedtls_ecp_group grp;
mbedtls_mpi d;
mbedtls_ecp_point Q;

mbedtls_ecp_group_init(&grp);
mbedtls_mpi_init(&d);
mbedtls_ecp_point_init(&Q);

ret = mbedtls_ecp_group_load(&grp, MBEDTLS_ECP_DP_SECP256R1);
ret = mbedtls_ecdh_gen_public(&grp, &d, &Q, mbedtls_ctr_drbg_random, &ctr_drbg);

// Cleanup:
mbedtls_ecp_group_free(&grp);
mbedtls_mpi_free(&d);
mbedtls_ecp_point_free(&Q);
```

### Status
✅ Fixed

---

## Error 5: Linker Undefined Reference

### Error Message
```
/home/lenovo/.espressif/tools/xtensa-esp-elf/esp-13.2.0_20250707/xtensa-esp-elf/bin/ld: 
esp-idf/main/libmain.a(pqc_benchmark.c.obj):(.literal.app_main+0x30): 
undefined reference to `hybrid_ecc_mlkem_task'
collect2: error: ld returned 1 exit status
```

### Root Cause
The `hybrid_ecc_mlkem_task` function was declared as `static` in `hybrid_ecc_mlkem_task.c`, making it invisible to `pqc_benchmark.c` which tries to call it via `xTaskCreate()`.

### Solution
**File:** `main/hybrid_ecc_mlkem_task.c`

Removed the `static` keyword to make the function globally linkable:

**Before:**
```c
static void hybrid_ecc_mlkem_task(void *pvParameters)
```

**After:**
```c
void hybrid_ecc_mlkem_task(void *pvParameters)
```

The function is already declared as a prototype in `pqc_benchmark.c`:
```c
// In pqc_benchmark.c (line 40):
void hybrid_ecc_mlkem_task(void *pvParameters);
```

### Status
✅ Fixed

---

## Summary Table

| Error # | Type | File | Fix |
|---------|------|------|-----|
| 1 | Missing Header | pqc_benchmark.c | Use `esp_private/esp_clk.h` path |
| 2 | API Mismatch | pqc_benchmark.c | Remove extra context parameters from ML-DSA calls |
| 3 | Unused Function | pqc_benchmark.c | Add `__attribute__((unused))` |
| 4 | Private Members | hybrid_ecc_mlkem_task.c | Use separate ECP structures instead of ECDH context |
| 5 | Linker | hybrid_ecc_mlkem_task.c | Remove `static` keyword for external linkage |

---

## Build Status
After applying all fixes above, the project should compile successfully with no errors.

### To rebuild:
```bash
cd /home/lenovo/pqc_esp32
idf.py build
```

### To build and flash:
```bash
idf.py build flash monitor
```
