/**
 * @file json_builder.h
 * @brief 轻量级JSON构造器
 */

#ifndef JSON_BUILDER_H
#define JSON_BUILDER_H

#include "pq_common.h"

#ifdef __cplusplus
extern "C" {
#endif

int json_begin(char *buf, int cap);
int json_add_string(char *buf, int cap, const char *key, const char *val, int is_last);
int json_add_float(char *buf, int cap, const char *key, float val, int is_last);
int json_add_int(char *buf, int cap, const char *key, int val, int is_last);
int json_add_object_begin(char *buf, int cap, const char *key);
int json_add_object_end(char *buf, int cap, int is_last);
int json_add_array_begin(char *buf, int cap, const char *key);
int json_add_array_end(char *buf, int cap, int is_last);
int json_end(char *buf, int cap);

#ifdef __cplusplus
}
#endif

#endif /* JSON_BUILDER_H */
