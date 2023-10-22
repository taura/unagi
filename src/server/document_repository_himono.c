/**
 * @file document_repository.c
 * @brief ドキュメントを登録, 検索するレポジトリ
 * @author 田浦
 * @date Oct. 8, 2018
 */

#define _GNU_SOURCE         /* for memmem */
#include <assert.h>
#include <errno.h>
#include <sys/time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <pthread.h>
#include <ctype.h>
#include <unistd.h>

#include "unagi_utility.h"
#include "document_repository_himono.h"

/**
   @brief 2つの数のうち大きくない方
*/
long min_long(long x, long y) {
  return (x < y ? x : y);
}

/**
   @brief 2つの数のうち小さくない方
  */
long max_long(long x, long y) {
  return (x < y ? y : x);
}

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
    long n = da->n;;
    for (long i = 0; i < n; i++) {
      document_t doc = a[i];
      if (doc.label) my_free(doc.label);
      if (doc.data)  my_free(doc.data);
    }
    my_free(a);
    da->a = 0;
  }
}

/**
   @brief ドキュメント配列の初期サイズ(1つ目の要素挿入時)
 */
static const long document_array_init_sz = 16;

/**
   @brief ドキュメント配列の末尾にドキュメントを追加

   @details 必要ならば配列を拡張する. 1要素追加するたびに拡張(メモリ割り当て+コピー)するのを避けるため, 拡張するときは大きさを2倍にする.
 */
static long document_array_pushback(document_array_t * da, document_t d) {
  long n = da->n;             /* 現在の要素数 */
  long sz = da->sz;           /* 現在のサイズ */
  document_t * a = da->a;
  if (n >= sz) {                /* すでに満杯. 拡張が必要 */
    /* 新しいサイズ. document_array_init_sz または現在のサイズ x2 */
    long new_sz = sz;
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
   @brief ドキュメントの配列からidx番目の文字を含むドキュメントを返す
*/
document_t document_array_find_doc(document_array_t * da, long idx) {
  document_t * a = da->a;
  long n = da->n;
  assert(n > 0);
  /* 2分探索 */
  long p = 0;
  long q = n - 1;
  assert(idx < a[q].data_o + a[q].data_len);
  if (a[q].data_o <= idx) {
    return a[q];
  } else {
    assert(a[p].data_o <= idx);
    assert(idx < a[q].data_o);
    while (q - p > 1) {
      long c = (p + q) / 2;
      if (a[c].data_o <= idx) {
        p = c;
      } else {
        q = c;
      }
    }
    assert(0 <= p);
    assert(q < n);
    assert(q - p == 1);
    assert(a[p].data_o <= idx);
    assert(idx < a[q].data_o);
    assert(a[p].data_o + a[p].data_len == a[q].data_o);
    return a[p];
  }
}

/**
   @brief dataのchar_bufで idx 番目の文字から始まる
   文字列の長さ(バイト数)を返す.
   つまり, idx番目を含む文書の終わりの位置(end)を
   求め, end - idx を返す
 */
long document_array_data_len(document_array_t * da, long idx) {
  document_t d = document_array_find_doc(da, idx);
  assert(d.data_o <= idx);
  assert(idx < d.data_o + d.data_len);
  return d.data_o + d.data_len - idx;
}

/* char_buf関連  */

/**
   @brief char_bufの初期化

   @details
   char_buf は多数の文字列を一つの大きな配列で管理するためのデータ構造. 
   unagiサーバではドキュメントのラベルとテキスト本体を,
   それぞれひとつの大きなchar_bufに隙間なく格納する.
   ドキュメントが追加されるたびにそのラベルとテキスト本体が,
   それぞれのchar_bufの末尾に追加される.
   溢れたらメモリを割り当て直し, 常にすべての文字列がひとつの配列に
   隙間なく格納されるようにする.
*/
static void char_buf_init(char_buf_t * cb) {
  cb->sz = 0;         /* aの容量(格納可能バイト数) */
  cb->n = 0;          /* 実際に格納されているバイト数 */
  cb->a = 0;          /* 先頭のアドレス */
}

/**
   @brief char_bufの破壊
*/
static void char_buf_destroy(char_buf_t * cb) {
  if (cb->a) {
    my_free(cb->a);
    cb->a = 0;
    cb->sz = 0;
    cb->n = 0;
  }
}

/**
   @brief 空でない文字列割当て器の初期サイズ
*/
static const long char_buf_init_size = 1 << 14; /* 16KB */

/**
   @brief 新しい文字列の追加
*/
static long char_buf_pushback(char_buf_t * cb, char * s, long len) {
  long n = cb->n;
  /* 必要最低量 */
  long req_n = n + len;
  /* 現在のサイズを req_n に達するまで倍々にする */
  long new_sz = cb->sz;
  if (new_sz == 0) new_sz = char_buf_init_size;
  while (new_sz < req_n) {
    new_sz *= 2;
  }
  /* サイズの拡張が必要なら割り当て直し */
  if (cb->sz < new_sz) {
    cb->a = realloc(cb->a, new_sz);
    cb->sz = new_sz;
    /* 余った部分は0にしておく(本当は不要) */
    memset(cb->a + req_n, 0, new_sz - req_n);
  }
  /* 受け取った文字列を流し込む */
  memcpy(cb->a + n, s, len);
  cb->n = req_n;
  assert(cb->n <= cb->sz);
  return n;
}

/* suffix array 関連 */

/**
   @brief デバッグ用フラグ
   @details sa_dbg=1 または =2 で色々なチェックをしながら動く.
   めちゃくちゃ遅くなるので小さいデータでデバッグをする時用
 */
const int sa_dbg = 0;

/**
   @brief suffix array初期化
 */
static void suffix_array_init(suffix_array_t * sa) {
  sa->sz = 0;      /* ptrsの容量(格納可能要素数) */
  sa->n = 0;       /* ユニークな要素数 */
  sa->ptrs = 0;    /* 文字列開始位置の配列 */
  sa->f = 2;
}

/**
   @brief suffix array破壊
 */
static void suffix_array_destroy(suffix_array_t * sa) {
  my_free(sa->ptrs);
  sa->ptrs = 0;
  sa->n = 0;
  sa->sz = 0;
  sa->f = 0;
}

/**
   @brief suffix arrayが少なくとも req_sz 要素持つように保証
  */
static int suffix_array_ensure_sz(suffix_array_t * sa, long req_sz) {
  long sz = sa->sz;
  if (sz < req_sz) {
    /* 現状のszの方が小さければ倍々にしていく */
    long new_sz = sz;
    if (new_sz == 0) {
      new_sz = req_sz;
    }
    while (new_sz < req_sz) {
      new_sz *= 2;
    }
    assert(sz < new_sz);
    sa_idx_t * ptrs = sa->ptrs;
    sa_idx_t * new_ptrs = malloc_or_err(new_sz * sizeof(sa_idx_t));
    if (sa_dbg>=2) {
      printf("suffix_array_ensure_sz -> %ld elems\n", (long)new_sz);
    }
    if (ptrs) {
      assert(sz > 0);
      assert(new_sz % sz == 0);
      long f = new_sz / sz;
      for (long i = 0; i < sz; i++) {
        for (long j = 0; j < f; j++) {
          new_ptrs[i * f + j] = ptrs[i];
        }
      }
      my_free(ptrs);
    } else {
      assert(sz == 0);
      for (long i = 0; i < new_sz; i++) {
        new_ptrs[i] = 0;
      }
    }
    sa->ptrs = new_ptrs;
    sa->sz = new_sz;
    return 1;
  } else {
    return 0;
  }
}

/**
   @brief suffix arrayのptrs[i:j] を ptrs[i+s:j+s] に移す
   (s は>0, <0の両方がありうる)
*/
static void suffix_array_shift_ptrs(suffix_array_t * sa, long i, long j, long s) {
  sa_idx_t * ptrs = sa->ptrs;
  long     sz = sa->sz;
  if (sa_dbg>=2) {
    printf("suffix_array_shift_ptrs [%ld:%ld] -> [%ld:%ld]\n",
           (long)i, (long)j, (long)(i + s), (long)(j + s));
  }
  if (s > 0) {
    /* 右にずらす場合(右端の要素からずらす) */
    assert(0 <= i);
    assert(j + s <= sz);
    assert(j > 0);
    for (long k = j - 1; k >= i; k--) {
      assert(0 <= k);
      assert(k < sz);
      assert(0 <= k + s);
      assert(k + s < sz);
      ptrs[k + s] = sa->ptrs[k];
    }
  } else {
    /* 右にずらす場合(左端の要素からずらす) */
    assert(0 <= i + s);
    assert(j <= sz);
    for (long k = i; k < j; k++) {
      assert(0 <= k);           /* 0 <= i */
      assert(k < sz);           /* j <= sz */
      assert(0 <= k + s);       /* 0 <= i + s */
      assert(k + s < sz);       /* j + s <= sz */
      ptrs[k + s] = sa->ptrs[k];
    }
  }
}

/**
   @brief suffix arrayの全要素をxにする
*/
static void suffix_array_set_ptrs(suffix_array_t * sa, long x) {
  sa_idx_t * ptrs = sa->ptrs;
  long sz = sa->sz;
  assert(sa->n == 0);
  for (long i = 0; i < sz; i++) {
    ptrs[i] = x;
  }
  sa->n = 1;
}

/* 
   @brief suffix arrayのi-1 と i の間にxを挿入.
   @details 最終的に ptrs[i] = x とする. それにともない,
   前後の値をずらす必要があればずらす.
   例えばこのようになっていたとする. aaa, gg は
   それらの場所に同じ要素が入っていることを示す.
             i
    ...aaaabcdefggg...

    この場合最終的に以下のようにする.

             i
    ...aaaabcxdefgg...

 */
static long suffix_array_insert_ptr_before(suffix_array_t * sa, long i, long x) {
  assert((sa->n + 1) * sa->f <= sa->sz);
  sa_idx_t * ptrs = sa->ptrs;
  long sz = sa->sz;
  /* ... i-2 i-1 | i i+1 i+2 ... */
  assert(i == 0 || i == sz || ptrs[i - 1] != ptrs[i]);
  assert(i <= sz);
  for (long j = 1; j < max_long(i, sz - i); j++) {
    /* see if ptrs[i+j] is different */
    assert(1 <= i + j);
    if (i + j < sz) {            /* j < n - i */
      if (ptrs[i+j] == ptrs[i+j-1]) {
        assert(j > 0);
        /* we can shift a[i:i+j] to the right */
        suffix_array_shift_ptrs(sa, i, i + j, 1);
        if (sa_dbg>=2) {
          printf("[%ld] <- %ld\n", (long)i, (long)x);
        }
        ptrs[i] = x;
        sa->n++;
        return j;
      }
    }
    assert(i - j < sz);
    if (1 <= i - j) {       /* j <= i - 1  */
      if (ptrs[i-j-1] == ptrs[i-j]) {
        assert(j > 0);
        /* we can shift ptrs[i-j-1:i-1] to the right */
        suffix_array_shift_ptrs(sa, i - j, i, -1);
        if (sa_dbg>=2) {
          printf("[%ld] <- %ld\n", (long)(i - 1), (long)x);
        }
        ptrs[i - 1] = x;
        sa->n++;
        return -j;
      }
    }
  }
  internal_err("suffix_array_insert_ptr_before:"
               " could not find a room to shift pointers\n");
  return 0;
}

/* document repository関連 */

/**
   @brief 2つの文字列の辞書順比較
   @details aから始まるalenバイトとbから始まるblenバイトを辞書指揮比較.
   片方がもう片方のprefix担っている場合(例えば a = "hallow", b = "halloween")
   短い方 < 長い方とみなす.
 */

int textcmp(char * a, long alen, char * b, long blen) {
  assert(a);
  assert(b);
  if (a == b) {
    /* ポインタとして同じ場合をショートカット(長い文字列比較を避ける) */
    return alen - blen;
  }
  int r = memcmp(a, b, min_long(alen, blen));
  if (r) {
    return r;
  } else {
    return alen - blen;
  }
}

/**
   @brief
   以下を満たすindexを返す (< は, qlen文字まで比べる辞書順順序)
   &text[sa[index-1]] < query <= &text[sa[index]]
   &text[sa[index]] が query をprefixに含んでいなければ,
   queryはtext中に現れない.
 */

static long document_repo_search(document_repo_t * repo,
                                 char * query, long qlen) {
  assert(query);
  long sz = repo->sa->sz;
  if (sz == 0) {
    return -1;
  } else {
    sa_idx_t * ptrs = repo->sa->ptrs;
    char * chars = repo->data->a;
    document_array_t * da = repo->da;
    long a = 0, b = sz - 1;
    long alen = document_array_data_len(da, ptrs[a]);
    long blen = document_array_data_len(da, ptrs[b]);

    if (textcmp(query, qlen, &chars[ptrs[a]], alen) <= 0) return 0;
    if (textcmp(&chars[ptrs[b]], blen, query, qlen) <  0) return sz;
    /* &chars[sa[a]] < query <= &chars[sa[b]] */
    while (b - a > 1) {
      if (sa_dbg>=1) {
        assert(textcmp(&chars[ptrs[a]], alen, query, qlen) < 0);
        assert(textcmp(query, qlen, &chars[ptrs[b]], blen) <= 0);
      }
      long c = (a + b) / 2;
      long clen = document_array_data_len(da, ptrs[c]);
      if (textcmp(&chars[ptrs[c]], clen, query, qlen) < 0) {
        a = c;
        alen = clen;
      } else {
        if (sa_dbg>=1) {
          assert(textcmp(query, qlen, &chars[ptrs[c]], clen) <= 0);
        }
        b = c;
        blen = clen;
      }
    }
    assert(a == b - 1);
    assert(0 < b);
    assert(b < sz);
    if (sa_dbg>=1) {
      assert(textcmp(&chars[ptrs[a]], alen, query, qlen) < 0);
      assert(textcmp(query, qlen, &chars[ptrs[b]], blen) <= 0);
    }
    return b;
  }
}

/**
   @brief
   @return return the number of i s.t. chars[sa[i]] < chars[sa[i+1]]
  */

static long document_repo_check_ascending(document_repo_t * repo,
                                          long start, long end, int strict) {
  sa_idx_t * ptrs = repo->sa->ptrs;
  char *  chars = repo->data->a;
  long n_chars = repo->data->n;
  document_array_t * da = repo->da;
  long n_distinct_elems = 0;
  assert(end > 0);
  if (start < end) n_distinct_elems++;
  for (long i = start; i < end - 1; i++) {
    long p = ptrs[i];
    long q = ptrs[i + 1];
    long plen = document_array_data_len(da, p);
    long qlen = document_array_data_len(da, q);
    assert(p >= 0);
    assert(p < n_chars);
    assert(q >= 0);
    assert(q < n_chars);
    int cmp = textcmp(&chars[q], qlen, &chars[p], plen);
    assert(cmp >= strict);
    if (cmp > 0) n_distinct_elems++;
  }
  return n_distinct_elems;
}

static void document_repo_print(document_repo_t * repo) {
  document_array_t * da = repo->da;
  sa_idx_t *  ptrs = repo->sa->ptrs;
  long sz = repo->sa->sz;
  char *  chars = repo->data->a;
  long n_chars = repo->data->n;
  char * a = calloc(1, n_chars + 1);
  if (sa_dbg>=2) {
    printf("==== document_repo_print %ld elements ====\n", (long)sz);
  }
  memcpy(a, chars, n_chars);
  for (long i = 0; i < sz; i++) {
    long p = ptrs[i];
    if (i == 0 || p != ptrs[i - 1]) {
      long plen = document_array_data_len(da, p);
      assert(p >= 0);
      assert(p < n_chars);
      assert(p + plen <= n_chars);
      char saved_char = a[p + plen];
      a[p + plen] = 0;
      printf("%3ld : %s\n", (long)i, &a[p]);
      a[p + plen] = saved_char;
    }
  }
  my_free(a);
}

/**
   @brief
   文字列 base[idx:idx+len]をsaに追加
 */

static void document_repo_add_str(document_repo_t * repo, long idx, long len) {
  suffix_array_t * sa = repo->sa;
  suffix_array_ensure_sz(sa, (sa->n + 1) * sa->f);
  document_array_t * da = repo->da;
  char * chars = repo->data->a;
  char * s = &chars[idx];
  if (sa->n == 0) {
    suffix_array_set_ptrs(sa, idx);
  } else {
    long i = document_repo_search(repo, s, len);
    sa_idx_t * ptrs = sa->ptrs;
    long sz = sa->sz;
    assert(0 <= i);
    assert(i <= sz);
    /* &chars[sa[i-1]] < query <= &chars[sa[i]]  */
    if (sa_dbg>=1) {
      long n_chars = repo->data->n;
      if (i < sz) {
        long p = ptrs[i];
        long plen = document_array_data_len(da, p);
        assert(0 <= p);
        assert(p < n_chars);
        assert(textcmp(s, len, &chars[p], plen) <= 0);
      }
      if (i > 0) {
        long q = ptrs[i-1];
        long qlen = document_array_data_len(da, q);
        assert(0 <= q);
        assert(q < n_chars);
        assert(textcmp(&chars[q], qlen, s, len) < 0);
      }
    }
    suffix_array_insert_ptr_before(sa, i, idx);
    if (sa_dbg>=2) {
      document_repo_check_ascending(repo, 0, repo->sa->sz, 0);
    }
  }
}

/**
   @brief
   (base+begin_idx)   から始まり (base+end_idx-1) で終わる文字列をsaに追加
   (base+begin_idx+1) から始まり (base+end_idx-1) で終わる文字列をsaに追加
   (base+begin_idx+2) から始まり (base+end_idx-1) で終わる文字列をsaに追加
      ...
 */
static void document_repo_add_strs(document_repo_t * repo,
                                   long begin_idx, long len) {
  char * chars = repo->data->a;
  for (long i = 0; i < len; i++) {
    if (sa_dbg>=2) {
      document_repo_print(repo);
    }
    long o = begin_idx + i;
    if (i == 0 || (((unsigned char)chars[o]) >> 6) == 3  ||
        ((((unsigned char)chars[o]) >> 7) == 0 && isspace(chars[o-1]))) {
      document_repo_add_str(repo, o, len - i);
    }
  }
  if (sa_dbg>=2) {
    document_repo_print(repo);
  }
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
  char_buf_init(repo->labels);
  char_buf_init(repo->data);
  repo->use_sa = 1;
  suffix_array_init(repo->sa);
}

/**
   @brief ドキュメントレポジトリ(document_repo_t)を破壊する. メモリを開放する

   @sa document_repo_init
   @sa document_repo_add
   @sa document_repo_t
  */
void document_repo_destroy(document_repo_t * repo) {
  document_array_destroy(repo->da);
  char_buf_destroy(repo->labels);
  char_buf_destroy(repo->data);
  suffix_array_destroy(repo->sa);
}

/**
   @brief ドキュメントレポジトリ(document_repo_t)にドキュメントを追加する
   @return 成功したら, 非負の整数. 失敗(メモリ割り当て失敗)したら-1. 

   @sa document_repo_init
   @sa document_repo_destroy
   @sa document_repo_t
  */
long document_repo_add(document_repo_t * repo, document_t d) {
  d.label_o = char_buf_pushback(repo->labels, d.label, d.label_len);
  d.data_o  = char_buf_pushback(repo->data,   d.data,  d.data_len);
  my_free(d.label);
  my_free(d.data);
  d.label = 0;
  d.data = 0;
  long r = document_array_pushback(repo->da, d);
  if (repo->use_sa) {
    document_repo_add_strs(repo, d.data_o, d.data_len);
  }
  return r;
}

/**
   @brief 辞書順で次の文字列を作る
  */

char * make_next_string(char * s, long n) {
  char * t = malloc_or_err(n);
  memcpy(t, s, n);
  long i = 0;
  assert(n > 0);
  for (i = 0; i < n; i++) {
    t[n - i - 1]++;
    if (t[n - i - 1] != 0) break;
  }
  if (i == n) {
    my_free(t);
    return 0;                   /* all 0xff */
  } else {
    return t;
  }
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
                    long query_len        /**< queryの長さ(バイト数) */
                    ) {
  if (repo->use_sa) {
    char * next_query = make_next_string(query, query_len);
    long begin = document_repo_search(repo, query,      query_len);
    long   end = (next_query ?
                  document_repo_search(repo, next_query, query_len) :
                  repo->sa->sz);
    assert(begin <= end);
    long n = end - begin;
    sa_idx_t * occurrences = &repo->sa->ptrs[begin];
    my_free(next_query);
    query_result_t qr = {
      repo,
      query,
      query_len,
      occurrences,              /* occurrences */
      n,                        /* n_occs */
      0,                        /* next_occ */
      -1,
      0
    };
    return qr;
  } else {
    query_result_t qr = {
      repo,
      query,
      query_len,
      0,                        /* occurrences */
      -1,                       /* n_occs */
      -1,                       /* next_occ */
      0,                        /* next_doc */
      0                         /* next_pos */
    };
    return qr;
  }
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
  document_repo_t * repo = qr->repo;
  document_array_t * da = repo->da;

  if (qr->occurrences) {
    long n = qr->n_occs;
    long query_len = qr->query_len;
    sa_idx_t * occurrences = qr->occurrences;
    for (long i = qr->next_occ; i < n; i++) {
      long idx = occurrences[i];
      if (i == 0 || idx != occurrences[i - 1]) {
        document_t doc = document_array_find_doc(da, idx);
        if (idx + query_len <= doc.data_o + doc.data_len) {
          qr->next_occ = i + 1;
          occurrence_t o = { doc, idx - doc.data_o };
          return o;
        }
      }
    }
    qr->next_occ = n;
    occurrence_t o = { { 0, 0, 0, 0, 0, 0 }, -1 };
    return o;
  } else {
    char * query = qr->query;
    long n_docs = da->n;
    document_t * a = da->a;
    long start_i = qr->next_doc;
    /* qr->i 番目のドキュメントから検索 */
    for (long i = start_i; i < n_docs; i++) {
      char * data = a[i].data;
      char * data_end = data + a[i].data_len;
      long query_len = qr->query_len;
      /* ドキュメント先頭もしくは最後に見つかった場所 + 1から検索 */
      char * p = ((i == start_i && qr->next_pos) ? qr->next_pos : data);
      while (p) {
        /* pから始まる文字列中から, queryの出現を検索 */
        // char * q = strstr(p, query);
        assert(data_end - p >= 0);
        char * q = memmem(p, data_end - p, query, query_len);
        if (q) {
          /* 見つかったのでそれを返す */
          occurrence_t o = { a[i], q - data };
          qr->next_doc = i;
          qr->next_pos = q + 1;
          return o;
        } else {
          /* p = 0 -> while文を終了. 次のドキュメントへ進む */
          p = q;
        }
      };
    }
    /* 検索終了(これ以上の出現は無し) */
    qr->next_doc = n_docs;
    qr->next_pos = 0;
    occurrence_t o = { { 0, 0, 0, 0, 0, 0 }, -1 };
    return o;
  }
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
long document_repo_queryc(document_repo_t * repo, /**< 検索対象のドキュメントレポジトリ */
                          char * query,           /**< 検索文字列 */
                          long query_len        /**< queryの長さ(バイト数) */
                          ) {
  document_array_t * da = repo->da;
  if (repo->use_sa) {
    char * next_query = make_next_string(query, query_len);
    long begin = document_repo_search(repo, query, query_len);
    long   end = (next_query ?
                  document_repo_search(repo, next_query, query_len) :
                  repo->sa->sz);
    my_free(next_query);
    assert(begin <= end);
    long n = end - begin;
    sa_idx_t * occurrences = &repo->sa->ptrs[begin];
    long c = 0;
    for (long i = 0; i < n; i++) {
      long idx = occurrences[i];
      if (i == 0 || idx != occurrences[i - 1]) {
        document_t doc = document_array_find_doc(da, idx);
        if (idx + query_len <= doc.data_o + doc.data_len) {
          c++;
        }
      }
    }
    return c;
  } else {
    long n_docs = da->n;
    document_t * a = da->a;
    /* 以下では文字列の終わりは0と仮定しているので不要 */
    long c = 0;
    for (long i = 0; i < n_docs; i++) {
      char * data = a[i].data;
      char * data_end = data + a[i].data_len;
      char * p = data;
      while (p) {
        /* pから始まる文字列中から, queryの出現を検索 */
        //char * q = strstr(p, query);
        assert(data_end - p >= 0);
        char * q = memmem(p, data_end - p, query, query_len);
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
  dump_result_t dr = { repo, 0 };
  return dr;
}

/**
   @brief 全ドキュメント数を取得する.
 */
long document_repo_n_docs(document_repo_t * repo) {
  long n = repo->da->n;
  return n;
}

/**
   @brief document_repo_dump が返した dump_result_t から,
   次のドキュメントを返す.
  */
document_t dump_result_next(dump_result_t * dr) {
  document_repo_t * repo = dr->repo;
  document_array_t * da = repo->da;
  long n = da->n;
  long i = dr->i;
  if (i < n) {
    dr->i = i + 1;
    document_t doc = da->a[i];
    return doc;
  } else {
    document_t doc = { 0, -1, -1, 0, -1, -1 };
    return doc;
  }
}

long cur_time_us() {
  struct timeval tp[1];
  gettimeofday(tp, 0);
  return tp->tv_sec * 1000000 + tp->tv_usec;
}

int document_repo_save(document_repo_t * repo, const char * dir) {
  long t0 = cur_time_us();

  
  fprintf(stderr, "%s:%d:document_repo_save(%p,%s): 実装してください\n",
          __FILE__, __LINE__, repo, dir);
  exit(1);

  
  long t1 = cur_time_us();
  long dt = t1 - t0;
  fprintf(stderr, "server saved data to %s in %.6f sec\n",
          dir, dt * 1.0e-6);
  return 1;                     /* OK */
}

int document_repo_load(document_repo_t * repo, const char * dir) {
  long t0 = cur_time_us();

  
  fprintf(stderr, "%s:%d:document_repo_load(%p,%s): 実装してください\n",
          __FILE__, __LINE__, repo, dir);
  exit(1);

  
  long t1 = cur_time_us();
  long dt = t1 - t0;
  fprintf(stderr, "server loaded data from %s in %.6f sec\n",
          dir, dt * 1.0e-6);
  return 1;                     /* OK */
}
