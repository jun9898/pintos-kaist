#include <stdio.h>
#include <stdint.h>
#include "fixed_point.h"
// 둘다 소수의 덧셈
int fixed_add(int x, int y) {
    return x + y;
}
// 둘다 소수의 뺄셈
int fixed_sub(int x, int y) {
    return x - y;
}
// 소수 + 정수
int fixed_add_int(int x, int n) {
    return x + n * F;
}
// 소수 - 정수
int fixed_sub_int(int x, int n) {
    return x - n * F;
}
// 소수끼리 곱하기
int fixed_mul(int x, int y) {
    return ((int64_t) x) * y / F;
}
// 소수끼리 나누기
int fixed_div(int x, int y) {
    return ((int64_t) x) * F / y;
}
// 정수를 소수로
int int_to_fixed(int n) {
    return n * F;
}
// 소수를 정수로 반올림처리
int fixed_to_int_round(int x) {
    if (x >= 0) {
        return (x + F / 2) / F;
    } else {
        return (x - F / 2) / F;
    }
}
// 소수를 정수 바꾸되, 내림
int fixed_to_int_trunc(int x) {
    return x / F;
}
// 17.14로 표현된 고정소수점을 double float 형태로.
double fixed_to_double(int x) {
    return ((double)x) / F;
}
// double float 형태를 -> 17.14로 표현된 고정소수점 형태로.
int double_to_fixed(double x) {
    return (int)(x * F);
}