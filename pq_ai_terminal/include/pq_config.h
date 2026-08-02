/**
 * @file pq_config.h
 * @brief 系统配置参数管理
 */

#ifndef PQ_CONFIG_H
#define PQ_CONFIG_H

#include "pq_common.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CFG_MAX_KEY_LEN   64
#define CFG_MAX_VAL_LEN   128
#define CFG_MAX_ENTRIES   64

typedef struct {
    char key[CFG_MAX_KEY_LEN];
    char val[CFG_MAX_VAL_LEN];
} cfg_entry_t;

typedef struct {
    cfg_entry_t entries[CFG_MAX_ENTRIES];
    int count;
} pq_config_t;

int  config_load(pq_config_t *cfg, const char *path);
int  config_get_int(const pq_config_t *cfg, const char *key, int default_val);
float config_get_float(const pq_config_t *cfg, const char *key, float default_val);
const char* config_get_string(const pq_config_t *cfg, const char *key, const char *default_val);

#ifdef __cplusplus
}
#endif

#endif /* PQ_CONFIG_H */
