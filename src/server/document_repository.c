/**
 * @file document_repository.c
 * @brief ドキュメントを登録, 検索するレポジトリ
 * @author 田浦
 * @date Oct. 8, 2018
 */

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "unagi_utility.h"
#include "document_repository.h"

/**
   @brief ドキュメント配列(document_array_t)の初期化(空の配列にする)
 */
static void document_array_init(document_array_t * da) {
  da->sz = 0;
  da->n = 0;
  da->a = 0;
}

/**
   @brief ドキュメント配列(document_array_t)を破壊. メモリを開放
 */
static void document_array_destroy(document_array_t * da) {
  document_t * a = da->a;
  if (a) {
    size_t n = da->n;;
    for (size_t i = 0; i < n; i++) {
      document_t doc = a[i];
      if (doc.label) my_free(doc.label);
      if (doc.data) my_free(doc.data);
    }
    my_free(a);
    da->a = 0;
  }
}

/**
   @brief ドキュメント配列の初期サイズ(1つ目の要素挿入時)
 */
static const size_t document_array_init_sz = 16;

/**
   @brief ドキュメント配列の末尾にドキュメントを追加

   @details 必要ならば配列を拡張する. 1要素追加するたびに拡張(メモリ割り当て+コピー)するのを避けるため, 拡張するときは大きさを2倍にする.
 */
static ssize_t document_array_pushback(document_array_t * da, document_t d) {
  size_t n = da->n;             /* 現在の要素数 */
  size_t sz = da->sz;           /* 現在のサイズ */
  document_t * a = da->a;
  if (n >= sz) {                /* すでに満杯. 拡張が必要 */
    /* 新しいサイズ. document_array_init_sz または現在のサイズ x2 */
    size_t new_sz = sz;
    if (new_sz == 0) {
      new_sz = document_array_init_sz;
    } else {
      new_sz *= 2;
    }
    assert(n < new_sz);
    /* メモリ割り当て + コピー */
    document_t * new_a = malloc_or_err(sizeof(document_t) * new_sz);
    if (!new_a) return -1;
    memcpy(new_a, a, sizeof(document_t) * sz);
    my_free(a);
    da->a = a = new_a;
    da->sz = new_sz;
  }
  /* ドキュメントを配列に追加 */
  a[n] = d;
  da->n = n + 1;
  return n;
}

/**
   @brief ドキュメントレポジトリ(document_repo_t)の初期化(空にする)

   @sa document_repo_destroy
   @sa document_repo_add
   @sa document_repo_t
  */
void document_repo_init(document_repo_t * repo) {
  /* empty doc repository */
  document_array_init(repo->da);
}

/**
   @brief ドキュメントレポジトリ(document_repo_t)を破壊する. メモリを開放する

   @sa document_repo_init
   @sa document_repo_add
   @sa document_repo_t
  */
void document_repo_destroy(document_repo_t * repo) {
  document_array_destroy(repo->da);
}

/**
   @brief ドキュメントレポジトリ(document_repo_t)にドキュメントを追加する
   @return 成功したら, 非負の整数. 失敗(メモリ割り当て失敗)したら-1. 

   @sa document_repo_init
   @sa document_repo_destroy
   @sa document_repo_t
  */
ssize_t document_repo_add(document_repo_t * repo, document_t d) {
  return document_array_pushback(repo->da, d);
}

/**
   @brief ドキュメントレポジトリから指定文字列(query)を検索.
   @return 検索結果(query_result_t)
   @sa query_result_t
   @sa query_result_next
   @sa document_repo_add

   @details ドキュメントレポジトリ内の全ドキュメント(これまでに
   document_repo_add されたすべてのドキュメント)から検索文字列 query
   の出現を検索する. query_result_next は, そこから全ての出現位置を取
   得することができるようなデータである. 詳しくは, query_result_t のド
   キュメントを参照
  */
query_result_t
document_repo_query(document_repo_t * repo, /**< 検索対象のドキュメントレポジトリ */
                    char * query,           /**< 検索文字列 */
                    size_t query_len        /**< queryの長さ(バイト数) */
                    ) {
  query_result_t qr = { repo->da, query, query_len, 0, 0 };
  return qr;
}

/**
   @brief document_repo_query で得られた検索結果から, 次の出現位置を得る
   @return 検索文字列の次の出現位置(occurrence_t)

   @sa document_repo_query
   @sa query_result_t

   @details document_repo_query が返した「検索結果(query_result_t 型の
   データ)」から, 検索文字列の「次の」出現位置を得る. 検索結果に対して
   この関数を次々と呼び出すことで, すべての出現位置を得ることができる.
   document_repo_query 現在の検索アルゴリズムは非常に単純(非効率)なも
   ので, (putで)蓄えられたドキュメントを順に, スキャンしていくだけのも
   の. ひとつのドキュメントから文字列を検索するにはCのstrstr 関数を
   呼ぶ(man strstr して調べよ).

 */

occurrence_t query_result_next(query_result_t * qr /**< document_repo_queryが返した検索結果 */) {
  document_array_t * da = qr->da;
  char * query = qr->query;
  size_t n_docs = da->n;
  document_t * a = da->a;
  size_t start_i = qr->i;
  /* qr->i 番目のドキュメントから検索 */
  for (size_t i = start_i; i < n_docs; i++) {
    char * data = a[i].data;
    /* ドキュメント先頭もしくは最後に見つかった場所 + 1から検索 */
    char * p = ((i == start_i && qr->p) ? qr->p : data);
    while (p) {
      /* pから始まる文字列中から, queryの出現を検索 */
      char * q = strstr(p, query);
      if (q) {
        /* 見つかったのでそれを返す */
        occurrence_t o = { a[i], q - data };
        qr->i = i;
        qr->p = q + 1;
        return o;
      } else {
        /* p = 0 -> while文を終了. 次のドキュメントへ進む */
        p = q;
      }
    };
  }
  /* 検索終了(これ以上の出現は無し) */
  qr->i = n_docs;
  qr->p = 0;
  occurrence_t o = { { 0, 0, 0, 0 }, 0 };
  return o;
}

/**
   @brief ドキュメントレポジトリから指定文字列(query)を検索しその出現回数
   (のみ)を返す
   @return 出現回数
   @sa query_result_t
   @sa query_result_next
   @sa document_repo_query
   @sa document_repo_add

   @details ドキュメントレポジトリ内の全ドキュメント(これまでに
   document_repo_add されたすべてのドキュメント)から検索文字列 query
   の出現を検索する. query_result_next は, そこから全ての出現位置を取
   得することができるようなデータである. 詳しくは, query_result_t のド
   キュメントを参照
  */
size_t
document_repo_queryc(document_repo_t * repo, /**< 検索対象のドキュメントレポジトリ */
                     char * query,           /**< 検索文字列 */
                     size_t query_len        /**< queryの長さ(バイト数) */
                     ) {
  document_array_t * da = repo->da;
  size_t n_docs = da->n;
  document_t * a = da->a;
  /* 以下では文字列の終わりは0と仮定しているので不要 */
  (void)query_len;
  size_t c = 0;
  for (size_t i = 0; i < n_docs; i++) {
    char * p = a[i].data;
    while (p) {
      /* pから始まる文字列中から, queryの出現を検索 */
      char * q = strstr(p, query);
      if (q) {
        c++;
        p = q + 1;
      } else {
        p = q;
      }
    };
  }
  return c;
}

/**
   @brief 全ドキュメントのダンプを表すデータ構造

   @sa document_repo_dump
   @sa dump_result_next

   @details 一般に全ドキュメントは大量になりうるため, 
   ドキュメントダンプの結果は, ドキュメントを一つずつ返すことができる
   データ構造とする. 具体的には, dump_result_next という関数で,
   次のドキュメントを返すようなデータ構造とする. 従って以下のように使う.

   dump_result_t qr = document_repo_dump(repo);

   while (1) {

     document_t doc = dump_result_next(&qr);

     if (!doc.label) break;

     ... doc.data ...

   }

 */

/**
   @brief 全ドキュメントのダンプを取得する

   @details dump_result_t 型の構造体を返す. その構造体は,
   dump_result_next(..) 関数を次々と呼ぶことで, 次々に
   ドキュメントを返す.
 */
dump_result_t document_repo_dump(document_repo_t * repo) {
  dump_result_t dr = { repo->da, 0 };
  return dr;
}

/**
   @brief 全ドキュメント数を取得する.
 */
size_t document_repo_n_docs(document_repo_t * repo) {
  return repo->da->n;
}

/**
   @brief document_repo_dump が返した dump_result_t から,
   次のドキュメントを返す.
  */
document_t dump_result_next(dump_result_t * dr) {
  document_array_t * da = dr->da;
  size_t n = da->n;
  size_t i = dr->i;
  if (i < n) {
    dr->i = i + 1;
    return da->a[i];
  } else {
    document_t doc = { 0, 0, 0, 0 };
    return doc;
  }
}
