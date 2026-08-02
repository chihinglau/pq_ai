/**
 * @file cnn1d_infer.c
 * @brief 1D-CNN事件分类推理实现 (CPU模拟)
 */

#include "cnn1d_infer.h"
#include <math.h>
#include <string.h>

static int g_cnn_classes = 7;
static int g_cnn_pts = 256;

/* 模拟卷积权重和偏置 */
#define CNN_KERNEL_SIZE 5
#define CNN_FILTERS 8

static float g_conv_w[CNN_FILTERS * CNN_KERNEL_SIZE];
static float g_conv_b[CNN_FILTERS];
static float g_fc_w[CNN_MAX_CLASSES * CNN_FILTERS];
static float g_fc_b[CNN_MAX_CLASSES];
static int g_cnn_init = 0;

static uint32_t g_rand_seed = 54321;
static float rand_float_cnn(void)
{
    g_rand_seed = g_rand_seed * 1103515245u + 12345u;
    return (float)(g_rand_seed & 0x7FFF) / 32768.0f;
}

static float relu(float x) { return x > 0.0f ? x : 0.0f; }

static void softmax(float *x, int n)
{
    float maxv = x[0];
    int i;
    for (i = 1; i < n; i++) if (x[i] > maxv) maxv = x[i];
    float sum = 0.0f;
    for (i = 0; i < n; i++) {
        x[i] = expf(x[i] - maxv);
        sum += x[i];
    }
    for (i = 0; i < n; i++) x[i] /= sum;
}

int cnn1d_init(int n_classes, int pts_per_cycle)
{
    int i, j;
    g_cnn_classes = n_classes;
    g_cnn_pts = pts_per_cycle;

    for (i = 0; i < CNN_FILTERS; i++) {
        for (j = 0; j < CNN_KERNEL_SIZE; j++) {
            g_conv_w[i * CNN_KERNEL_SIZE + j] = (rand_float_cnn() - 0.5f) * 0.2f;
        }
        g_conv_b[i] = 0.0f;
    }
    for (i = 0; i < n_classes; i++) {
        for (j = 0; j < CNN_FILTERS; j++) {
            g_fc_w[i * CNN_FILTERS + j] = (rand_float_cnn() - 0.5f) * 0.1f;
        }
        g_fc_b[i] = 0.0f;
    }
    g_cnn_init = 1;
    PQ_LOGI("CNN1D init: classes=%d pts=%d", n_classes, pts_per_cycle);
    return 0;
}

int cnn1d_classify(const float *wave, int n_channels, int pts_per_cycle,
                   int n_cycles, float *probs)
{
    int i, j, f;
    float conv_out[CNN_FILTERS];
    (void)n_cycles;
    (void)n_channels;

    if (!g_cnn_init || wave == NULL || probs == NULL) return -1;

    /* 全局平均池化后的卷积特征模拟 */
    for (f = 0; f < CNN_FILTERS; f++) {
        float sum = 0.0f;
        /* 对通道0(UA)做一维卷积并全局平均 */
        for (i = CNN_KERNEL_SIZE / 2; i < pts_per_cycle - CNN_KERNEL_SIZE / 2; i++) {
            float conv = 0.0f;
            for (j = 0; j < CNN_KERNEL_SIZE; j++) {
                conv += wave[i + j - CNN_KERNEL_SIZE / 2] * g_conv_w[f * CNN_KERNEL_SIZE + j];
            }
            sum += relu(conv + g_conv_b[f]);
        }
        conv_out[f] = sum / (float)(pts_per_cycle - CNN_KERNEL_SIZE + 1);
    }

    /* 全连接 + softmax */
    for (i = 0; i < g_cnn_classes; i++) {
        float sum = g_fc_b[i];
        for (j = 0; j < CNN_FILTERS; j++) {
            sum += g_fc_w[i * CNN_FILTERS + j] * conv_out[j];
        }
        probs[i] = sum;
    }
    softmax(probs, g_cnn_classes);
    return 0;
}

int cnn1d_get_class(const float *probs, int n_classes, float *confidence)
{
    int best = 0;
    for (int i = 1; i < n_classes; i++) {
        if (probs[i] > probs[best]) best = i;
    }
    if (confidence) *confidence = probs[best];
    return best;
}
