

mac OSにてgdbのコマンド入力時に unknown load command 0x32 と表示されデバッグできません
============================================

 * 自分でやれていないのでわかりませんが, このページが同じ問題を報告していて https://gdb-prs.sourceware.narkive.com/5Lijl6u4/bug-gdb-23742-new-unknown-load-command-0x32 解決策は書かれていません.
 * 現時点で逃げ道として
  * clang でコンパイルしてみる
  * clang でコンパイルして lldb でデバッグしてみる
をやってみて下さい.

  
