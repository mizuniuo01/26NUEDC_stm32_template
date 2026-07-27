#ifndef AUTO_BALL_CAR_USER_LIBRARIES_SCALAR_VALUE_H
#define AUTO_BALL_CAR_USER_LIBRARIES_SCALAR_VALUE_H /* 头文件保护 */

#include <stdbool.h>
#include <stdint.h>

/* 可在参数和实时数据边界传递的标量类型 */
typedef enum {
    SCALAR_VALUE_FLOAT = 0, /* 单精度浮点数 */
    SCALAR_VALUE_INT32,     /* 32 位有符号整数 */
    SCALAR_VALUE_UINT32,    /* 32 位无符号整数 */
    SCALAR_VALUE_BOOL,      /* 布尔状态 */
} scalar_value_type_t;

/* 带明确类型标签的标量值 */
typedef struct {
    scalar_value_type_t type; /* data 中当前有效的成员类型 */
    union {
        float float_value;       /* 单精度浮点值 */
        int32_t int32_value;     /* 32 位有符号值 */
        uint32_t uint32_value;   /* 32 位无符号值 */
        bool bool_value;         /* 布尔值 */
    } data;
} scalar_value_t;

#endif /* AUTO_BALL_CAR_USER_LIBRARIES_SCALAR_VALUE_H */
