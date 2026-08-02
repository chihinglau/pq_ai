/**
 * @file pq_config.c
 * @brief 轻量级INI配置文件解析器
 */

#include "pq_config.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

int config_load(pq_config_t *cfg, const char *path)
{
    FILE *fp;
    char line[256];
    int line_no = 0;

    if (cfg == NULL || path == NULL) return -1;
    cfg->count = 0;

    fp = fopen(path, "r");
    if (fp == NULL) {
        PQ_LOGW("Config file not found: %s, using defaults", path);
        return -1;
    }

    while (fgets(line, sizeof(line), fp) != NULL && cfg->count < CFG_MAX_ENTRIES) {
        line_no++;
        char *p = line;
        /* 跳过前导空白 */
        while (*p == ' ' || *p == '\t') p++;
        /* 跳过空行和注释 */
        if (*p == '\0' || *p == '\n' || *p == ';' || *p == '#') continue;

        char *eq = strchr(p, '=');
        if (eq == NULL) continue;

        *eq = '\0';
        char *key = p;
        char *val = eq + 1;

        /* 去除key尾部空白 */
        char *kend = key + strlen(key) - 1;
        while (kend > key && (*kend == ' ' || *kend == '\t')) {
            *kend = '\0';
            kend--;
        }

        /* 去除val前导空白和尾部换行 */
        while (*val == ' ' || *val == '\t') val++;
        char *vend = val + strlen(val) - 1;
        while (vend > val && (*vend == '\n' || *vend == '\r')) {
            *vend = '\0';
            vend--;
        }

        if (strlen(key) >= CFG_MAX_KEY_LEN || strlen(val) >= CFG_MAX_VAL_LEN) {
            PQ_LOGW("Config line %d too long, skipped", line_no);
            continue;
        }

        strncpy(cfg->entries[cfg->count].key, key, CFG_MAX_KEY_LEN - 1);
        strncpy(cfg->entries[cfg->count].val, val, CFG_MAX_VAL_LEN - 1);
        cfg->count++;
    }

    fclose(fp);
    PQ_LOGI("Config loaded: %d entries from %s", cfg->count, path);
    return 0;
}

static int config_find(const pq_config_t *cfg, const char *key)
{
    int i;
    if (cfg == NULL || key == NULL) return -1;
    for (i = 0; i < cfg->count; i++) {
        if (strcmp(cfg->entries[i].key, key) == 0) {
            return i;
        }
    }
    return -1;
}

int config_get_int(const pq_config_t *cfg, const char *key, int default_val)
{
    int idx = config_find(cfg, key);
    if (idx < 0) return default_val;
    return atoi(cfg->entries[idx].val);
}

float config_get_float(const pq_config_t *cfg, const char *key, float default_val)
{
    int idx = config_find(cfg, key);
    if (idx < 0) return default_val;
    return (float)atof(cfg->entries[idx].val);
}

const char* config_get_string(const pq_config_t *cfg, const char *key, const char *default_val)
{
    int idx = config_find(cfg, key);
    if (idx < 0) return default_val;
    return cfg->entries[idx].val;
}
