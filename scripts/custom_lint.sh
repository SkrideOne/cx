#!/usr/bin/env bash
set -e
status=0
files=$(git ls-files '*.c' '*.h')
for f in $files; do
  head12=$(head -n 12 "$f")
  for tag in Purpose Pipeline Actions; do
    if ! grep -q "$tag" <<<"$head12"; then
      echo "header_triplet violation in $f: missing $tag" >&2
      status=1
    fi
  done
  if grep -E "return [0-9-]+;" "$f" | grep -vE 'XDP_|BPF_|RET_|EXIT|ENO'; then
      echo "ret_literal violation in $f" >&2
      status=1
  fi
  if grep -q alloca "$f"; then
      echo "vla_alloca use in $f" >&2
      status=1
  fi
  if grep -E '\[[a-zA-Z_]+[a-zA-Z0-9_]*\]' "$f" | grep -vq '\[[0-9]'; then
      echo "vla_alloca variable length array in $f" >&2
      status=1
  fi
  if grep -E '\bfloat\b|\bdouble\b|\blong double\b' "$f"; then
      echo "float_types violation in $f" >&2
      status=1
  fi
  if grep -E '\bsigned[[:space:]]+char\b' "$f"; then
      echo "signed_char violation in $f" >&2
      status=1
  fi
done
exit $status
