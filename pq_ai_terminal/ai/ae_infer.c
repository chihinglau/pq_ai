/**
 * @file ae_infer.c
 * @brief 自编码器异常检测推理实现 (CPU模拟)
 */

#include "ae_infer.h"
#include <math.h>
#include <string.h>

static int g_ae_in_dim = 27;
static int g_ae_bot_dim = 8;
static float g_w1[AE_MAX_DIM * AE_MAX_DIM]; /* encoder: in -> bottleneck */
static float g_b1[AE_MAX_DIM];
static float g_w2[AE_MAX_DIM * AE_MAX_DIM]; /* decoder: bottleneck -> in */
static float g_b2[AE_MAX_DIM];
static int g_ae_init = 0;

/* 简单伪随机数 */
static uint32_t g_rand_seed = 12345;
static float rand_float(void)
{
    g_rand_seed = g_rand_seed * 1103515245u + 12345u;
    return (float)(g_rand_seed & 0x7FFF) / 32768.0f;
}

int ae_init(int input_dim, int bottleneck_dim)
{
    int i, j;
    if (input_dim > AE_MAX_DIM || bottleneck_dim > AE_MAX_DIM) {
        return -1;
    }
    g_ae_in_dim = input_dim;
    g_ae_bot_dim = bottleneck_dim;

    /* Xavier-like init */
    float scale1 = sqrtf(2.0f / (float)(input_dim + bottleneck_dim));
    for (i = 0; i < bottleneck_dim; i++) {
        for (j = 0; j < input_dim; j++) {
            g_w1[i * input_dim + j] = (rand_float() - 0.5f) * 2.0f * scale1;
        }
        g_b1[i] = 0.0f;
    }

    float scale2 = sqrtf(2.0f / (float)(bottleneck_dim + input_dim));
    for (i = 0; i < input_dim; i++) {
        for (j = 0; j < bottleneck_dim; j++) {
            g_w2[i * bottleneck_dim + j] = (rand_float() - 0.5f) * 2.0f * scale2;
        }
        g_b2[i] = 0.0f;
    }

    g_ae_init = 1;
    PQ_LOGI("AE init: input=%d bottleneck=%d", input_dim, bottleneck_dim);
    return 0;
}

int ae_encode(const float *x, float *z)
{
    int i, j;
    if (!g_ae_init || x == NULL || z == NULL) return -1;
    for (i = 0; i < g_ae_bot_dim; i++) {
        float sum = g_b1[i];
        for (j = 0; j < g_ae_in_dim; j++) {
            sum += g_w1[i * g_ae_in_dim + j] * x[j];
        }
        z[i] = tanhf(sum); /* 激活函数 */
    }
    return 0;
}

int ae_decode(const float *z, float *x_hat)
{
    int i, j;
    if (!g_ae_init || z == NULL || x_hat == NULL) return -1;
    for (i = 0; i < g_ae_in_dim; i++) {
        float sum = g_b2[i];
        for (j = 0; j < g_ae_bot_dim; j++) {
            sum += g_w2[i * g_ae_bot_dim + j] * z[j];
        }
        x_hat[i] = sum; /* 线性输出 */
    }
    return 0;
}

float ae_reconstruction_error(const float *x, const float *x_hat, int n)
{
    float mse = 0.0f;
    for (int i = 0; i < n; i++) {
        float d = x[i] - x_hat[i];
        mse += d * d;
    }
    return mse / (float)n;
}

float ae_anomaly_score(const float *x)
{
    float z[AE_MAX_DIM];
    float x_hat[AE_MAX_DIM];
    if (ae_encode(x, z) != 0) return 1.0f;
    if (ae_decode(z, x_hat) != 0) return 1.0f;
    return ae_reconstruction_error(x, x_hat, g_ae_in_dim);
}
