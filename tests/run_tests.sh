#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR"

JCCSC="${JCCSC:-./jccsc}"
TMP_DIR="tests/tmp"
mkdir -p "$TMP_DIR"

if [[ ! -x "$JCCSC" ]]; then
  echo "[info] jccsc no existe, compilando..."
  make
fi

pass_count=0
fail_count=0

run_pass() {
  local case_name="$1"
  local expected="$2"
  local src="tests/cases/pass/${case_name}.cs"
  local log="$TMP_DIR/${case_name}.log"
  local out_bc="$TMP_DIR/${case_name}.jccsc"

  if ! "$JCCSC" "$src" -o "$out_bc" --run >"$log" 2>&1; then
    echo "[FAIL] $case_name (deberia compilar/ejecutar)"
    cat "$log"
    fail_count=$((fail_count + 1))
    return
  fi

  local normalized
  normalized="$(grep -v '^Compilacion OK\.' "$log" | sed '/^$/d')"
  if [[ "$normalized" == "$expected" ]]; then
    echo "[PASS] $case_name"
    pass_count=$((pass_count + 1))
  else
    echo "[FAIL] $case_name (salida inesperada)"
    echo "--- esperado ---"
    printf '%s\n' "$expected"
    echo "--- actual ---"
    printf '%s\n' "$normalized"
    fail_count=$((fail_count + 1))
  fi
}

run_fail() {
  local case_name="$1"
  local expected_substr="$2"
  local src="tests/cases/fail/${case_name}.cs"
  local log="$TMP_DIR/${case_name}.log"
  local out_bc="$TMP_DIR/${case_name}.jccsc"

  if "$JCCSC" "$src" -o "$out_bc" --run >"$log" 2>&1; then
    echo "[FAIL] $case_name (deberia fallar)"
    cat "$log"
    fail_count=$((fail_count + 1))
    return
  fi

  if grep -Fq "$expected_substr" "$log"; then
    echo "[PASS] $case_name"
    pass_count=$((pass_count + 1))
  else
    echo "[FAIL] $case_name (diagnostico inesperado)"
    echo "Buscado: $expected_substr"
    echo "Log real:"
    cat "$log"
    fail_count=$((fail_count + 1))
  fi
}

run_native_target() {
  local target="$1"
  local src="tests/cases/pass/string_print.cs"
  local out_asm="$TMP_DIR/native_${target}.s"
  local log="$TMP_DIR/native_${target}.log"
  if ! "$JCCSC" "$src" -o "$out_asm" --backend native --target "$target" --emit-asm >"$log" 2>&1; then
    echo "[FAIL] native_target_$target (deberia generar asm)"
    cat "$log"
    fail_count=$((fail_count + 1))
    return
  fi
  if grep -Fq "_start" "$out_asm"; then
    echo "[PASS] native_target_$target"
    pass_count=$((pass_count + 1))
  else
    echo "[FAIL] native_target_$target (asm sin _start)"
    cat "$out_asm"
    fail_count=$((fail_count + 1))
  fi
}

run_native_fail_run_flag() {
  local src="tests/cases/pass/string_print.cs"
  local out_asm="$TMP_DIR/native_run_fail.s"
  local log="$TMP_DIR/native_run_fail.log"
  if "$JCCSC" "$src" -o "$out_asm" --backend native --target x86_64 --run >"$log" 2>&1; then
    echo "[FAIL] native_run_flag (deberia fallar)"
    fail_count=$((fail_count + 1))
    return
  fi
  if grep -Fq -- "--run no esta soportado en backend native" "$log"; then
    echo "[PASS] native_run_flag"
    pass_count=$((pass_count + 1))
  else
    echo "[FAIL] native_run_flag (diagnostico inesperado)"
    cat "$log"
    fail_count=$((fail_count + 1))
  fi
}

run_native_host_executable() {
  local src="tests/cases/pass/arith_precedence.cs"
  local out_exe="$TMP_DIR/native_host_exec"
  local log="$TMP_DIR/native_host_exec.log"
  if ! "$JCCSC" "$src" -o "$out_exe" --backend native >"$log" 2>&1; then
    echo "[FAIL] native_host_executable (deberia generar ejecutable)"
    cat "$log"
    fail_count=$((fail_count + 1))
    return
  fi
  if [[ ! -x "$out_exe" ]]; then
    echo "[FAIL] native_host_executable (archivo no ejecutable)"
    fail_count=$((fail_count + 1))
    return
  fi
  if "$out_exe" >/dev/null 2>&1; then
    echo "[PASS] native_host_executable"
    pass_count=$((pass_count + 1))
  else
    echo "[FAIL] native_host_executable (ejecucion fallida)"
    fail_count=$((fail_count + 1))
  fi
}

# PASS CASES (14)
run_pass "arith_precedence" "7"
run_pass "if_else" $'111\n222'
run_pass "while_sum" "10"
run_pass "for_sum" "15"
run_pass "bool_logic" $'true\ntrue'
run_pass "break_continue" $'1\n3\ntrue'
run_pass "nested_blocks" $'2\n1'
run_pass "method_calls" "42"
run_pass "recursion_factorial" "120"
run_pass "string_print" "hola"
run_pass "string_concat" $'JuanCarlos\nN=7\nB=true\nX=null'
run_pass "comparisons" $'true\ntrue\ntrue\ntrue\ntrue\ntrue'
run_pass "unary_ops" $'-5\ntrue'
run_pass "for_without_condition" $'0\n1\n2'
run_pass "bool_param_return" "true"
run_pass "oop_instance" "2"
run_pass "namespace_using" "321"
run_pass "using_custom_package" "JuanCarlos"
run_pass "do_while" $'0\n1\n2'
run_pass "switch_basic" "22"
run_pass "arrays_basic" "12"
run_pass "foreach_length" $'4\n18'
run_pass "null_references" $'true\ntrue\ntrue\ntrue\n2'
run_pass "increment_compound" "1"
run_pass "method_overload" $'5\n9'
run_pass "method_overload_types" $'123\n1'
run_pass "method_overload_class_types" $'10\n20'
run_pass "struct_basic" $'1\n2'
run_pass "interface_basic" "JuanCarlos"
run_pass "try_catch_throw" "capturado: boom"
run_pass "try_finally" $'A\nF'
run_pass "try_catch_finally" $'C:x\nF'
run_pass "rethrow_nested_catch" $'mid\nouter:inner'
run_pass "multi_catch" $'N=7\nF'
run_pass "catch_all" "catched-all"
run_pass "short_circuit" $'1\n222\n2'
run_pass "null_coalescing" $'Carlos\nJuan'
run_pass "ternary_basic" $'10\n20\nJuan'
run_pass "default_return_values" $'false\nnull\ntrue'
run_pass "generics_syntax_basic" "5"
run_pass "async_await_basic" "7"
run_pass "list_int_basic" $'2\n30\n7\n0'

# NATIVE BACKEND TARGET CASES (4)
run_native_target "x86_64"
run_native_target "x86_32"
run_native_target "arm64"
run_native_target "arm32"
run_native_fail_run_flag
run_native_host_executable

# FAIL CASES (10)
run_fail "undefined_symbol" "simbolo no definido"
run_fail "type_mismatch_init" "tipo incompatible en inicializacion"
run_fail "return_type_mismatch" "tipo de retorno incompatible"
run_fail "break_outside_loop" "solo puede usarse dentro de un bucle"
run_fail "continue_outside_loop" "solo puede usarse dentro de un bucle"
run_fail "if_string_condition" "la condicion de if debe ser bool/int"
run_fail "while_string_condition" "la condicion de while debe ser bool/int"
run_fail "unterminated_string" "literal string sin cerrar"
run_fail "missing_semicolon" "se esperaba ';'"
run_fail "invalid_token" "caracter no reconocido"
run_fail "invalid_plus_operands" "operador '+' requiere enteros o al menos un string"
run_fail "foreach_non_array" "foreach requiere iterable int[]"
run_fail "null_to_int" "tipo incompatible en inicializacion"
run_fail "invalid_increment_target" "operador ++/-- requiere una variable asignable"
run_fail "method_overload_no_match" "no existe sobrecarga compatible"
run_fail "method_overload_type_no_match" "no existe sobrecarga compatible"
run_fail "method_overload_class_mismatch" "no existe sobrecarga compatible"
run_fail "throw_uncaught" "excepcion no capturada"
run_fail "rethrow_without_active_exception" "throw sin excepcion activa"
run_fail "multi_catch_unmatched" "excepcion no capturada"
run_fail "ternary_non_bool_condition" "la condicion del operador ternario debe ser bool/int"

echo
echo "Resumen: PASS=$pass_count FAIL=$fail_count"
if [[ $fail_count -ne 0 ]]; then
  exit 1
fi
