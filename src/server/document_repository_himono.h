/**
 * @file document_repository_1812.h
 * @brief ドキュメントを登録, 検索するレポジトリ(ヘッダファイル)
 * @author 田浦
 * @date Dec. 10, 2018
 */

#include <stdint.h>
typedef uint32_t sa_idx_t;

/** 
 @brief 1つのドキュメントを表す構造体(putされる単位)
 @sa document_array_t

 @details ドキュメントは任意のラベル(典型的にはタイトル, ファイル名など)文字列と, そのドキュメントの中身で表される. 使用例:

 char * label = "野球";
 char * data = "野球はアメリカの国民的スポーツ",
 document_t doc = { label, strlen(label), data, strlen(data) };
*/
typedef struct {
  char * label;                 /**< ラベル  */
  long label_o;                 /**< ラベル(オフセット)  */
  long label_len;               /**< ラベルの長さ(バイト数)  */
  char * data;                  /**< データ(ドキュメントのテキスト)  */
  long data_o;                  /**< データ(オフセット)  */
  long data_len;                /**< データの長さ(バイト数) */
} document_t;

/**
   @brief ドキュメントの可変長配列

   @sa document_array_init
   @sa document_array_destroy
   @sa document_array_pushback

   @details 使用例

   document_array_t da[1];

   document_array_init(da);

   document_t doc = { ... };

   document_array_pushback(da, doc);
  */
typedef struct {
  long sz;                    /**< 配列aのサイズ */
  long n;                     /**< 現在埋まっている要素数(n <= sz)  */
  document_t * a;               /**< ドキュメントの配列 */
} document_array_t;


/**
   @brief 文字列バッファ
*/

typedef struct {
  char * a;
  long n;
  long sz;
} char_buf_t;

/**
   @brief suffix array
  */
typedef struct {
  long sz;                      /**< ptrsの容量 */
  long n;                       /**< ptrs中で実際に埋まっている要素数  */
  sa_idx_t * ptrs;
  long f;                       /**< n * f >= szになったら拡大  */
} suffix_array_t;

/** 
    @brief ドキュメントのレポジトリ

    @sa document_repo_init
    @sa document_repo_destroy
    @sa document_repo_add

    @details 現状はドキュメントの配列そのものだが,
    検索インデクスの追加など今後変更される
*/

typedef struct {
  document_array_t da[1];       /**< putされたドキュメントの配列 */
  char_buf_t labels[1];
  char_buf_t data[1];
  int use_sa;
  suffix_array_t sa[1];
} document_repo_t;

/**
   @brief 文書中の検索文字列の出現(occurrence)を表すデータ

   @details ある検索文字列で検索(get)を行うと, その検索文字列が出現した
   箇所が(全て)返される. occurrence_t はそのうちの一つの出現を表す
   データ構造. どの文書に出現したかと, その中のどの位置(オフセット)
   に出現したかを示す.
  */
typedef struct {
  document_t doc;            /**< 検索文字列が出現したドキュメント */
  long offset;             /**< doc中で検索文字列が出現した位置  */
} occurrence_t;

/**
   @brief 検索結果(出現位置の集合, ストリーム)を表すデータ構造

   @sa document_repo_query
   @sa query_result_next

   @details 一般に検索結果は, 検索文字列の出現全てであり, 
   大量になりうるため, 検索結果は, 出現を一つずつ返すことができる
   データ構造とする. 具体的には, query_result_next という関数で,
   次の出現を返すようなデータ構造とする. 従って以下のように使う.

   query_result_t qr = document_repo_query(repo, query, query_len);

   while (1) {

     occurrence_t o = query_result_next(&qr);

     if (!o.doc.label) break;

     ... o.doc.data[o.offset] から query が出現する ...

   }

   この構造体の中身は以降, 検索のアルゴリズムを変える際に大きく変わる.
   現状は, 全ドキュメントを順にスキャンしていくだけのアルゴリズム

 */
typedef struct {
  document_repo_t * repo;  /**< ドキュメントレポジトリ */
  char * query;            /**< 検索文字列 */
  long query_len;          /**< queryの長さ(バイト数) */
  /* suffix arrayで求めた出現箇所 */
  sa_idx_t * occurrences;       /**< 出現場所の配列 */
  long n_occs;                  /**< occurrencesの大きさ */
  long next_occ;                /**< occurrences中で次に返す要素  */
  /* 全スキャンで求めた出現箇所 */
  long next_doc;    /**< 次に検索するドキュメントの番号(配列の添字) */
  char * next_pos;  /**< 次に検索を開始する位置  */
} query_result_t;

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

typedef struct {
  document_repo_t * repo;       /**< ドキュメントレポジトリ */
  long i;                       /**< カーソル(次に返すドキュメント) */
} dump_result_t;

void document_repo_init(document_repo_t * repo);
void document_repo_destroy(document_repo_t * repo);
long document_repo_add(document_repo_t * repo, document_t d);

query_result_t
document_repo_query(document_repo_t * repo, char * query, long query_len);

occurrence_t query_result_next(query_result_t * qr);

long document_repo_queryc(document_repo_t * repo, char * query, long query_len);

dump_result_t document_repo_dump(document_repo_t * repo);
long document_repo_n_docs(document_repo_t * repo);
document_t dump_result_next(dump_result_t * dr);

int document_repo_save(document_repo_t * repo, const char * dir);
int document_repo_load(document_repo_t * repo, const char * dir);
