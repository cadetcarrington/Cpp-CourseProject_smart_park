# 验证 CLI 预约命令完整生命周期：预约 -> 到场确认 -> 再次预约 -> 取消 -> 列表。
if(NOT DEFINED SMARTPARK_CLI)
    message(FATAL_ERROR "SMARTPARK_CLI must be defined")
endif()
if(NOT DEFINED DB_PATH)
    message(FATAL_ERROR "DB_PATH must be defined")
endif()

file(REMOVE "${DB_PATH}" "${DB_PATH}-wal" "${DB_PATH}-shm")

function(assert_cli_ok what)
    execute_process(
        COMMAND ${SMARTPARK_CLI} ${ARGN}
        OUTPUT_VARIABLE cli_stdout
        ERROR_VARIABLE cli_stderr
        RESULT_VARIABLE cli_result)
    if(NOT cli_result EQUAL 0)
        message(FATAL_ERROR
            "${what} failed (exit ${cli_result})\nstdout:\n${cli_stdout}\nstderr:\n${cli_stderr}")
    endif()
endfunction()

assert_cli_ok("book" --db "${DB_PATH}" --book 晋T00001 --in 90)
assert_cli_ok("checkin" --db "${DB_PATH}" --checkin 晋T00001 --in 90)
assert_cli_ok("book second plate" --db "${DB_PATH}" --book 晋T00002 --in 120)
assert_cli_ok("cancel" --db "${DB_PATH}" --cancel 晋T00002)
assert_cli_ok("list bookings" --db "${DB_PATH}" --bookings)

# 到场确认后再次确认应失败（预约已转为停车）。
execute_process(
    COMMAND ${SMARTPARK_CLI} --db "${DB_PATH}" --checkin 晋T00001 --in 95
    OUTPUT_VARIABLE dup_stdout
    ERROR_VARIABLE dup_stderr
    RESULT_VARIABLE dup_result)
if(NOT dup_result EQUAL 1)
    message(FATAL_ERROR
        "duplicate checkin should fail with exit 1, got ${dup_result}\nstdout:\n${dup_stdout}\nstderr:\n${dup_stderr}")
endif()

message(STATUS "smartpark_cli_booking: all booking commands passed")
