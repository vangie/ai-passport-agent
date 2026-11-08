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

    python3 tools/check_repo.py
    python3 tools/check_public.py
    PYTHONDONTWRITEBYTECODE=1 python3 tests/test_public_audit.py
    PYTHONDONTWRITEBYTECODE=1 python3 tests/test_release.py

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
        tests/test_key_guard.c -o "${test_dir}/test_key_guard"
    "${test_dir}/test_key_guard"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Icomponents/bsp/src \
        tests/test_bsp_display_rounding.c components/bsp/src/bsp_display_rounding.c \
        -o "${test_dir}/test_bsp_display_rounding"
    "${test_dir}/test_bsp_display_rounding"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Icomponents/bsp/src \
        tests/test_bsp_es8311_sleep_check.c components/bsp/src/bsp_es8311_sleep_check.c \
        -o "${test_dir}/test_bsp_es8311_sleep_check"
    "${test_dir}/test_bsp_es8311_sleep_check"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror \
        -Itests/battery_stubs -Itests/bsp_stubs -Icomponents/bsp/include \
        tests/test_bsp_battery.c -o "${test_dir}/test_bsp_battery"
    "${test_dir}/test_bsp_battery"
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
        -Itests/audio_stubs -Icomponents/bsp/include -Icomponents/bsp/src \
        tests/test_bsp_audio_recovery.c components/bsp/src/bsp_es8311_sleep_check.c \
        -o "${test_dir}/test_bsp_audio_recovery"
    "${test_dir}/test_bsp_audio_recovery"
    local gc_sections=-Wl,--gc-sections
    if [[ "$(uname -s)" == Darwin ]]; then
        gc_sections=-Wl,-dead_strip
    fi
    for demo in audio low_power ble wifi; do
        "${CC:-cc}" -std=c11 -Wall -Wextra -Werror \
            -ffunction-sections -fdata-sections -Itests/demo_stubs -Imain \
            "tests/test_demo_${demo}_runtime.c" "${gc_sections}" \
            -o "${test_dir}/test_demo_${demo}_runtime"
        "${test_dir}/test_demo_${demo}_runtime"
    done
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Iapp \
        tests/test_badge.c app/badge_store.c app/badge_ui.c app/badge_usb.c \
        app/badge_pair.c app/badge_proxy.c app/openclaw_frame.c \
        -o "${test_dir}/test_badge"
    "${test_dir}/test_badge"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Itests/nvs_stubs -Itests/gateway_stubs -Iapp \
        tests/test_badge_nvs.c app/badge_store.c app/badge_proxy.c -o "${test_dir}/test_badge_nvs"
    "${test_dir}/test_badge_nvs"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -ffunction-sections -fdata-sections \
        -Itests/demo_stubs -Imain -Iapp tests/test_badge_audio_runtime.c \
        "${gc_sections}" -o "${test_dir}/test_badge_audio_runtime"
    "${test_dir}/test_badge_audio_runtime"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Itests/clip_stubs \
        tests/test_badge_clip.c -o "${test_dir}/test_badge_clip"
    "${test_dir}/test_badge_clip"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Iapp \
        tests/test_badge_json.c app/badge_json.c -o "${test_dir}/test_badge_json"
    "${test_dir}/test_badge_json"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Iapp \
        tests/test_badge_reply.c app/badge_reply.c -o "${test_dir}/test_badge_reply"
    "${test_dir}/test_badge_reply"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Iapp tests/test_badge_ima.c -o "${test_dir}/test_badge_ima"
    "${test_dir}/test_badge_ima"
    node tests/test_passport_ima.mjs "${test_dir}/test_badge_ima"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Iapp \
        tests/test_badge_radio_policy.c app/badge_radio.c -o "${test_dir}/test_badge_radio_policy"
    "${test_dir}/test_badge_radio_policy"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -ffunction-sections -fdata-sections \
        -Itests/transport_stubs -Icomponents/passport_transport \
        tests/test_passport_transport.c "${gc_sections}" -o "${test_dir}/test_passport_transport"
    "${test_dir}/test_passport_transport"
    mbedtls_flags="$(pkg-config --cflags --libs mbedtls) -lmbedcrypto"
    local json_dir="${CJSON_SOURCE_DIR:-${IDF_PATH:-${HOME}/esp/esp-idf}/components/json/cJSON}"
    if [[ ! -f "${json_dir}/cJSON.c" ]]; then
        echo "ERROR: gateway runtime tests need ESP-IDF's cJSON source; activate ESP-IDF first." >&2
        return 1
    fi
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Itests/gateway_stubs -Imain -Iapp \
        -Icomponents/passport_crypto -I"${json_dir}" tests/test_badge_gateway.c \
        app/badge_reply.c app/badge_json.c components/passport_crypto/monocypher.c \
        components/passport_crypto/monocypher-ed25519.c "${json_dir}/cJSON.c" \
        ${mbedtls_flags} -o "${test_dir}/test_badge_gateway"
    "${test_dir}/test_badge_gateway"
    node tests/test_passport_relay.mjs
    node tests/test_passport_plugin.mjs
    python3 tests/test_passport_relay_install.py
    python3 tests/test_passport_plugin_migration.py
    PYTHONDONTWRITEBYTECODE=1 python3 tests/test_passport_plugin_package.py
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -pthread -Itests/tls_stubs -Imain -Iapp \
        tests/test_badge_tls_cancel.c main/badge_tls.c app/badge_proxy.c app/badge_ss.c \
        ${mbedtls_flags} -o "${test_dir}/test_badge_tls_cancel"
    "${test_dir}/test_badge_tls_cancel"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Iapp tests/test_badge_ss.c app/badge_ss.c \
        ${mbedtls_flags} -o "${test_dir}/test_badge_ss"
    "${test_dir}/test_badge_ss"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Icomponents/passport_crypto \
        tests/test_passport_crypto.c components/passport_crypto/monocypher.c \
        components/passport_crypto/monocypher-ed25519.c -o "${test_dir}/test_passport_crypto"
    "${test_dir}/test_passport_crypto"
    if [[ "$(uname -s)" == Darwin ]] && command -v swiftc >/dev/null 2>&1; then
        swiftc tests/test_mac_usb.swift mac/PassportMac/ConfigConnection.swift \
            mac/PassportMac/SerialPort.swift mac/PassportMac/FirmwareUpdate.swift shared/ProxySettings.swift \
            -o "${test_dir}/test_mac_usb"
        "${test_dir}/test_mac_usb"
        swiftc tests/test_pair_prompt.swift shared/PairPrompt.swift shared/ConfigurationLink.swift \
            -o "${test_dir}/test_pair_prompt"
        "${test_dir}/test_pair_prompt"
    fi
    PYTHONDONTWRITEBYTECODE=1 python3 tests/test_battery_profile.py
    PYTHONDONTWRITEBYTECODE=1 python3 tests/test_deep_sleep_contract.py
    PYTHONDONTWRITEBYTECODE=1 python3 tests/test_key_wake_contract.py
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

    SDKCONFIG_DEFAULTS="${repo_root}/sdkconfig.defaults" \
        idf.py -B "${validation_build_dir}" \
        -D "SDKCONFIG=${validation_build_dir}/sdkconfig" build
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
