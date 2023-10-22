/**
 * @file unagi_utility.c
 * @brief 共通に使うユーティリティ
 * @author 田浦
 * @date Oct. 8, 2018
 */

#include <stdio.h>
#include "unagi_utility.h"

/**
 @brief マクロ api_err を参照
 @sa api_err

 @details この関数を直接呼ぶことはない. マクロ api_err を参照
 */
void api_err_(const char * msg, /**< 表示したいメッセージ */
              const char * __file__, /**< ファイル名 */
              int __line__           /**< 行番号 */
              ) {
  fprintf(stderr, "%s:%d:error: [%s] (%m)\n", __file__, __line__, msg);
}

/**
 @brief マクロ internal_err を参照
 @sa internal_err

 @details この関数を直接呼ぶことはない. マクロ internal_err を参照
 */
void internal_err_(const char * msg, /**< 表示したいメッセージ */
                   const char * __file__, /**< ファイル名 */
                   int __line__           /**< 行番号 */
                   ) {
  fprintf(stderr, "%s:%d:error: internal error [%s]. abort\n",
          __file__, __line__, msg);
  exit(1);
}

/** 

    @brief mallocし, 失敗したらエラーを表示

    @details szバイトをmallocで割り当てる. 失敗したらエラーを表示(してNULLを返す. 終了はしない)

 */
void * malloc_or_err(size_t sz  /**< 割当量(バイト数) */) {
  void * a = malloc(sz);
  if (!a) api_err("malloc");
  return a;
}

/**
   @brief freeの代わりにこれを呼ぶ
  */
void my_free(void * a) {
  free(a);
}

