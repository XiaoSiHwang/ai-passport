#!/usr/bin/env bash
set -euo pipefail

mode="${1:---all}"
repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"

usage() {
    echo "Usage: $0 [--all|--static|--firmware]" >&2
}

run_static_checks() {
    local actionlint_bin
    local test_dir
    local gc_flag="-Wl,--gc-sections"
    if [[ "$(uname -s)" == "Darwin" ]]; then gc_flag="-Wl,-dead_strip"; fi

    python3 tools/check_repo.py

    actionlint_bin="${ACTIONLINT_BIN:-}"
    if [[ -z "${actionlint_bin}" ]]; then
        actionlint_bin="$(command -v actionlint || true)"
    fi
    if [[ -z "${actionlint_bin}" || ! -x "${actionlint_bin}" ]]; then
        actionlint_bin="$(./tools/install-actionlint.sh)"
    fi
    "${actionlint_bin}" -color .github/workflows/*.yml

    test_dir="$(mktemp -d /tmp/ai-passport-host-tests.XXXXXX)"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain \
        tests/test_ui_pixel_math.c main/ui_pixel_math.c \
        -o "${test_dir}/test_ui_pixel_math"
    "${test_dir}/test_ui_pixel_math"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain \
        tests/test_demo_navigation.c main/demo_navigation.c \
        -o "${test_dir}/test_demo_navigation"
    "${test_dir}/test_demo_navigation"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain \
        tests/test_workout_model.c main/workout_model.c \
        -o "${test_dir}/test_workout_model"
    "${test_dir}/test_workout_model"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain \
        tests/test_workout_power.c main/workout_power.c -o "${test_dir}/test_workout_power"
    "${test_dir}/test_workout_power"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain \
        tests/test_passport_calendar.c main/passport_calendar.c main/workout_model.c \
        -o "${test_dir}/test_passport_calendar"
    "${test_dir}/test_passport_calendar"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain \
        tests/test_codex_monitor.c main/codex_monitor.c main/workout_model.c \
        -o "${test_dir}/test_codex_monitor"
    "${test_dir}/test_codex_monitor"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -ffunction-sections -fdata-sections \
        -Itests/codex_stubs -Itests/network_stubs -Itests/workout_stubs -Imain \
        tests/test_codex_app.c main/codex_monitor.c main/workout_model.c "${gc_flag}" \
        main/passport_calendar.c main/workout_power.c \
        -o "${test_dir}/test_codex_app"
    "${test_dir}/test_codex_app"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain \
        tests/test_workout_profiles.c main/workout_profiles.c main/workout_model.c \
        -o "${test_dir}/test_workout_profiles"
    "${test_dir}/test_workout_profiles"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Itests/network_stubs -Itests/workout_stubs -Imain \
        tests/test_workout_network.c main/workout_profiles.c main/workout_model.c main/ai_quota.c main/ai_tokens.c main/codex_monitor.c main/workout_power.c \
        -o "${test_dir}/test_workout_network"
    "${test_dir}/test_workout_network"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain \
        tests/test_ai_quota.c main/ai_quota.c main/workout_model.c \
        -o "${test_dir}/test_ai_quota"
    "${test_dir}/test_ai_quota"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain \
        tests/test_ai_tokens.c main/ai_tokens.c main/workout_model.c \
        -o "${test_dir}/test_ai_tokens"
    "${test_dir}/test_ai_tokens"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Itests/workout_stubs -Imain \
        tests/test_workout_store.c main/workout_store.c main/workout_model.c main/workout_profiles.c main/ai_quota.c main/ai_tokens.c \
        -o "${test_dir}/test_workout_store"
    "${test_dir}/test_workout_store"
    PYTHONDONTWRITEBYTECODE=1 python3 tests/test_workout_fonts.py
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Icomponents/bsp/src \
        tests/test_bsp_display_rounding.c components/bsp/src/bsp_display_rounding.c \
        -o "${test_dir}/test_bsp_display_rounding"
    "${test_dir}/test_bsp_display_rounding"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Icomponents/bsp/src \
        tests/test_bsp_es8311_sleep_check.c components/bsp/src/bsp_es8311_sleep_check.c \
        -o "${test_dir}/test_bsp_es8311_sleep_check"
    "${test_dir}/test_bsp_es8311_sleep_check"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror \
        -Itests/bsp_stubs -Icomponents/bsp/include \
        tests/test_bsp_button.c -o "${test_dir}/test_bsp_button"
    "${test_dir}/test_bsp_button"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror \
        -Itests/bsp_stubs -Icomponents/bsp/include \
        tests/test_bsp_lvgl_init.c components/bsp/src/bsp_display_rounding.c \
        -o "${test_dir}/test_bsp_lvgl_init"
    "${test_dir}/test_bsp_lvgl_init"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror \
        -Itests/bsp_stubs -Icomponents/bsp/include \
        tests/test_bsp_display_sleep.c -o "${test_dir}/test_bsp_display_sleep"
    "${test_dir}/test_bsp_display_sleep"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror \
        -Itests/audio_stubs -Icomponents/bsp/include -Icomponents/bsp/src \
        tests/test_bsp_audio_recovery.c components/bsp/src/bsp_es8311_sleep_check.c \
        -o "${test_dir}/test_bsp_audio_recovery"
    "${test_dir}/test_bsp_audio_recovery"
    for demo in audio low_power ble wifi; do
        "${CC:-cc}" -std=c11 -Wall -Wextra -Werror \
            -ffunction-sections -fdata-sections -Itests/demo_stubs -Imain \
            "tests/test_demo_${demo}_runtime.c" "${gc_flag}" \
            -o "${test_dir}/test_demo_${demo}_runtime"
        "${test_dir}/test_demo_${demo}_runtime"
    done
    PYTHONDONTWRITEBYTECODE=1 python3 tests/test_deep_sleep_contract.py
    PYTHONDONTWRITEBYTECODE=1 python3 tests/test_check_repo.py
    PYTHONDONTWRITEBYTECODE=1 python3 tests/test_verify_firmware.py
    PYTHONDONTWRITEBYTECODE=1 python3 tests/test_archive_firmware.py
    PYTHONDONTWRITEBYTECODE=1 python3 tests/test_install_passport_skills.py
    rm -rf "${test_dir}"
    echo "Host tests: PASS"
}

run_firmware_checks() (
    local validation_build_dir

    if ! command -v idf.py >/dev/null 2>&1; then
        echo "ERROR: idf.py is not available; activate ESP-IDF 5.5.3 first." >&2
        return 1
    fi

    validation_build_dir="$(mktemp -d /tmp/ai-passport-firmware.XXXXXX)"
    trap 'case "${validation_build_dir}" in /tmp/ai-passport-firmware.*) rm -rf -- "${validation_build_dir}" ;; esac' EXIT

    # Exercise the decoder with the same cJSON source used by ESP-IDF, without vendoring it.
    "${CC:-cc}" -std=c11 -Wno-deprecated-declarations \
        -c "${IDF_PATH}/components/json/cJSON/cJSON.c" -o "${validation_build_dir}/cjson_host.o"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain \
        -I"${IDF_PATH}/components/json/cJSON" tests/test_workout_json.c \
        main/workout_json.c main/workout_model.c main/ai_quota.c main/ai_tokens.c main/workout_profiles.c "${validation_build_dir}/cjson_host.o" \
        -lm -o "${validation_build_dir}/test_workout_json"
    "${validation_build_dir}/test_workout_json"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain \
        -I"${IDF_PATH}/components/json/cJSON" tests/test_ai_quota_json.c \
        main/workout_json.c main/workout_model.c main/ai_quota.c main/ai_tokens.c main/workout_profiles.c "${validation_build_dir}/cjson_host.o" \
        -lm -o "${validation_build_dir}/test_ai_quota_json"
    "${validation_build_dir}/test_ai_quota_json"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain \
        -I"${IDF_PATH}/components/json/cJSON" tests/test_ai_tokens_json.c \
        main/workout_json.c main/workout_model.c main/ai_quota.c main/ai_tokens.c main/workout_profiles.c "${validation_build_dir}/cjson_host.o" \
        -lm -o "${validation_build_dir}/test_ai_tokens_json"
    "${validation_build_dir}/test_ai_tokens_json"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain \
        -I"${IDF_PATH}/components/json/cJSON" tests/test_codex_monitor_json.c \
        main/workout_json.c main/workout_model.c main/workout_profiles.c main/ai_quota.c main/ai_tokens.c \
        "${validation_build_dir}/cjson_host.o" -lm -o "${validation_build_dir}/test_codex_monitor_json"
    "${validation_build_dir}/test_codex_monitor_json"

    SDKCONFIG_DEFAULTS="${repo_root}/sdkconfig.defaults" \
        idf.py -B "${validation_build_dir}" \
        -D "SDKCONFIG=${validation_build_dir}/sdkconfig" build
    cmake -S tests/workout_ui -B "${validation_build_dir}/host-ui"
    cmake --build "${validation_build_dir}/host-ui" --parallel 8
    (cd "${validation_build_dir}" && ./host-ui/workout_ui_host)
    idf.py -B "${validation_build_dir}" merge-bin \
        -o "${validation_build_dir}/FoloToy-AI-Passport-full.bin"
    python3 tools/verify_firmware.py "${validation_build_dir}"
    PYTHONDONTWRITEBYTECODE=1 python3 tools/archive_firmware.py create \
        "${validation_build_dir}" --archive-root "${repo_root}/build/firmware"
    mkdir -p "${repo_root}/build"
    install -m 0644 \
        "${validation_build_dir}/FoloToy-AI-Passport-full.bin" \
        "${repo_root}/build/FoloToy-AI-Passport-full.bin"
    echo "Firmware build: PASS"
)

cd "${repo_root}"
case "${mode}" in
    --all)
        run_static_checks
        run_firmware_checks
        ;;
    --static)
        run_static_checks
        ;;
    --firmware)
        run_firmware_checks
        ;;
    *)
        usage
        exit 2
        ;;
esac
