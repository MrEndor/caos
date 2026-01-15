##### Что такое векторные инструкции? Что такое Intel intrinsics, какие они бывают?

**Vector Instructions (SIMD -- Single Instruction, Multiple Data)** -- инструкции, которые обрабатывают **несколько
элементов данных за один раз**:

```
Обычная инструкция:
c = a + b   (одно число)

Векторная инструкция:
c[0:3] = a[0:3] + b[0:3]   (четыре числа одновременно)
```

**Intel intrinsics** -- это функции на C/C++, которые прямо соответствуют SIMD инструкциям процессора.

**Основные наборы инструкций:**

| Набор       | Год  | Элементы             | Примеры            |
|-------------|------|----------------------|--------------------|
| **SSE**     | 2000 | 4 × 32-бит float     | `_mm_add_ps`       |
| **SSE2**    | 2001 | 2 × 64-бит double    | `_mm_add_pd`       |
| **AVX**     | 2011 | 8 × 32-бит float     | `_mm256_add_ps`    |
| **AVX2**    | 2013 | 256-бит, целые числа | `_mm256_add_epi32` |
| **AVX-512** | 2017 | 512-бит (8× больше)  | `_mm512_add_ps`    |

##### Что такое SIMD, AVX?

**SIMD (Single Instruction, Multiple Data)** -- архитектурный стиль обработки данных, когда одна инструкция обрабатывает
несколько данных.

**AVX (Advanced Vector Extensions)** -- современное расширение x86-64, добавляющее:

- 256-битные регистры (YMM0-YMM15);
- Операции над 8 float'ами или 4 double'ами одновременно;
- Примерно 2x ускорение для правильно векторизированного кода.

**Пример без AVX:**

```c
float result[8];
for (int i = 0; i < 8; i++)
    result[i] = a[i] * b[i] + c[i];  // 8 итераций
```

**С AVX:**

```c
// 1 инструкция = 8 операций умножения + 8 сложений
__m256 va = _mm256_loadu_ps(a);
__m256 vb = _mm256_loadu_ps(b);
__m256 vc = _mm256_loadu_ps(c);
__m256 result = _mm256_fmadd_ps(va, vb, vc);  // FMA = multiply-add
_mm256_storeu_ps(result_array, result);
```

##### Какие специальные регистры процессора используются для векторных инструкций?

- **XMM0-XMM15** (128 бит) -- для SSE/SSE2;
- **YMM0-YMM15** (256 бит) -- для AVX (YMM = верхние 128 бит XMM);
- **ZMM0-ZMM31** (512 бит) -- для AVX-512.

**Иерархия:**

```
ZMM0  (512-бит)
├── YMM0  (256-бит, нижние)
│   ├── XMM0  (128-бит, нижние)
│   │   ├── EAX  (32-бит, нижние)
│   │   └── RAX  (64-бит, нижние)
```

**Пример использования:**

```c
#include <immintrin.h>

// Загрузить 8 float'ов в 256-битный регистр
__m256 v = _mm256_loadu_ps(data);

// Они хранятся в YMM регистре:
// v[0] v[1] v[2] v[3] v[4] v[5] v[6] v[7]
```

##### Покажите на примерах, как применить векторизацию для ускорения какого-либо кода.

**Пример 1: Скалярное произведение**

```c
// Обычный код (медленно)
float dot_product(float* a, float* b, int n) {
    float sum = 0;
    for (int i = 0; i < n; i++)
        sum += a[i] * b[i];
    return sum;
}

// С AVX (быстро)
#include <immintrin.h>

float dot_product_avx(float *a, float *b, int n) {
    __m256 sum_vec = _mm256_setzero_ps();  // [0, 0, 0, 0, 0, 0, 0, 0]

    for (int i = 0; i < n; i += 8) {
        __m256 va = _mm256_loadu_ps(&a[i]);
        __m256 vb = _mm256_loadu_ps(&b[i]);
        __m256 prod = _mm256_mul_ps(va, vb);
        sum_vec = _mm256_add_ps(sum_vec, prod);
    }

    // Свернуть вектор сумм в скаляр
    float *sums = (float *)&sum_vec;
    return sums[0] + sums[1] + sums[2] + sums[3] +
           sums[4] + sums[5] + sums[6] + sums[7];
}
```

**Пример 2: Поэлементное умножение матриц**

```c
// Обычный код
void matrix_multiply(float *c, float *a, float *b, int n) {
    for (int i = 0; i < n; i += 8) {
        for (int j = 0; j < n; j += 8) {
            for (int k = 0; k < n; k++) {
                // c[i:i+7][j:j+7] += a[i:i+7][k] * b[k][j:j+7]
                __m256 av = _mm256_set1_ps(a[i*n + k]);
                __m256 bv = _mm256_loadu_ps(&b[k*n + j]);
                __m256 cv = _mm256_loadu_ps(&c[i*n + j]);
                cv = _mm256_fmadd_ps(av, bv, cv);
                _mm256_storeu_ps(&c[i*n + j], cv);
            }
        }
    }
}
```

**Результаты:**

- Обычный код: ~5GB/s пропускная способность;
- С AVX: ~20-40GB/s (в зависимости от L3 cache);
- Ускорение: **4-8x** для memory-bound операций.

**Компилирование с поддержкой AVX:**

```bash
gcc -mavx2 -O3 prog.c -o prog
```

**Важно:** Не все процессоры поддерживают AVX/AVX2. Проверить:

```bash
grep avx /proc/cpuinfo
```
