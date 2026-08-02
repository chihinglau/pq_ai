/**
 * @file iforest_infer.c
 * @brief 孤立森林异常检测推理实现
 */

#include "iforest_infer.h"
#include <math.h>
#include <string.h>

static float harmonic_number(int n)
{
    if (n <= 0) return 0.0f;
    float h = 0.0f;
    for (int i = 1; i <= n; i++) {
        h += 1.0f / (float)i;
    }
    return h;
}

static float c_func(int n)
{
    if (n <= 1) return 1.0f;
    return 2.0f * harmonic_number(n - 1) - 2.0f * (float)(n - 1) / (float)n;
}

static float traverse_tree(const if_node_t *nodes, int root, const float *x, int depth)
{
    int idx = root;
    while (idx >= 0) {
        const if_node_t *node = &nodes[idx];
        if (node->left < 0 && node->right < 0) {
            return (float)depth;
        }
        if (x[node->feat] < node->thr) {
            idx = node->left;
        } else {
            idx = node->right;
        }
        depth++;
        if (depth > IF_MAX_DEPTH + 10) break;
    }
    return (float)depth;
}

int iforest_load_model(iforest_model_t *model, const char *path)
{
    (void)path;
    if (model == NULL) return -1;

    memset(model, 0, sizeof(iforest_model_t));
    model->n_trees = 32;
    model->n_features = IF_N_FEATURES;

    /* 模拟：随机生成分裂树 */
    for (int t = 0; t < model->n_trees; t++) {
        int base = t * (1 << IF_MAX_DEPTH);
        for (int d = 0; d < (1 << (IF_MAX_DEPTH - 1)) - 1; d++) {
            int node_idx = base + d;
            model->nodes[node_idx].feat = (int)((d * 7 + t * 3) % IF_N_FEATURES);
            model->nodes[node_idx].thr = 0.3f + 0.4f * ((float)((d * 13 + t * 5) % 100) / 100.0f);
            model->nodes[node_idx].left = base + 2 * d + 1;
            model->nodes[node_idx].right = base + 2 * d + 2;
        }
        /* 叶子节点标记 */
        for (int d = (1 << (IF_MAX_DEPTH - 1)) - 1; d < (1 << IF_MAX_DEPTH) - 1; d++) {
            int node_idx = base + d;
            model->nodes[node_idx].left = -1;
            model->nodes[node_idx].right = -1;
        }
    }

    PQ_LOGI("IForest model loaded: trees=%d depth=%d", model->n_trees, IF_MAX_DEPTH);
    return 0;
}

float iforest_score(const iforest_model_t *model, const float *x)
{
    if (model == NULL || x == NULL || model->n_trees <= 0) {
        return 0.5f;
    }

    float total_depth = 0.0f;
    for (int t = 0; t < model->n_trees; t++) {
        int base = t * (1 << IF_MAX_DEPTH);
        total_depth += traverse_tree(model->nodes, base, x, 0);
    }

    float avg_depth = total_depth / (float)model->n_trees;
    float c_val = c_func(256); /* 样本数假设256 */
    float score = powf(2.0f, -avg_depth / c_val);
    return score;
}

int iforest_is_anomaly(const iforest_model_t *model, const float *x, float threshold)
{
    float s = iforest_score(model, x);
    return (s >= threshold) ? 1 : 0;
}
