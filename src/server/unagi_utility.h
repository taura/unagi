/**
 * @file unagi_utility.h
 * @brief 共通に使うユーティリティ(ヘッダファイル)
 * @author 田浦
 * @date Oct. 8, 2018
 */

#pragma once

#include <stdlib.h>

void api_err_(const char * msg,
              const char * __file__,
              int __line__);

/**
 @brief システムコールやC標準関数のエラーメッセージをファイル番号:行番号とともに表示
 @param (msg) 表示したいメッセージ
 @sa api_err_

 @details システムコール(send, recv, ...)やCの標準関数(malloc, ...)がエラーになった際, 直後にこの関数を呼ぶと原因が表示される. 使用例:

    char * p = malloc(...);

    if(!p) api_err("malloc");
 */
#define api_err(msg) api_err_(msg, __FILE__, __LINE__)

void internal_err_(const char * msg, 
                   const char * __file__,
                   int __line__);

/**
 @brief 内部的なエラーを表示
 @param (msg) メッセージ
 @sa internal_err_

 @details 内部的なエラーが発生した際にエラーメッセージ, ファイル名, 行番号を表示し, 終了する

 */
#define internal_err(msg) internal_err_(msg, __FILE__, __LINE__)

void * malloc_or_err(size_t sz);
void my_free(void * a);
