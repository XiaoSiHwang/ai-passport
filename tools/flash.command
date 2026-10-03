#!/usr/bin/env bash
set -eo pipefail

repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
build_dir="${repo_root}/build/flash"
serial_port=""
build_only=false

case "${1:-}" in
    --help|-h)
        printf '%s\n' '用法：./tools/flash.command [串口 | --build-only]' \
            '默认：自动识别 ESP USB 设备，构建、校验并分段刷写。' \
            '--build-only：只构建和校验，不连接或刷写设备。'
        exit 0
        ;;
    --build-only) build_only=true ;;
    --*) printf '未知选项：%s\n' "$1" >&2; exit 1 ;;
    *) serial_port="${1:-}" ;;
esac
if [[ $# -gt 1 ]]; then
    printf '%s\n' '请只指定一个串口，或使用 --build-only。' >&2
    exit 1
fi

finish() {
    local result=$?
    trap - EXIT
    if [[ ${result} -ne 0 ]]; then
        printf '\n%s\n' '操作失败，请查看上方错误信息。' >&2
    fi
    if [[ -t 0 && -t 1 ]]; then
        printf '\n%s' '按回车键结束…'
        read -r _ || true
    fi
    exit "${result}"
}
trap finish EXIT

activate_idf() {
    local idf_root="${AI_PASSPORT_IDF_ROOT:-${IDF_PATH:-}}"
    if [[ -z "${idf_root}" ]]; then
        idf_root="${HOME}/.cache/ai-passport/esp-idf-v5.5.3"
        export IDF_TOOLS_PATH="${IDF_TOOLS_PATH:-${HOME}/.cache/ai-passport/idf-tools}"
    fi
    if [[ ! -f "${idf_root}/export.sh" ]]; then
        printf '%s\n' '未找到 ESP-IDF，请通过 AI_PASSPORT_IDF_ROOT 指定 ESP-IDF 5.5.3 目录。' >&2
        return 1
    fi
    printf '%s\n' '正在激活 ESP-IDF…'
    source "${idf_root}/export.sh"
    if [[ "$(idf.py --version)" != 'ESP-IDF v5.5.3' ]]; then
        printf '%s\n' '本项目要求 ESP-IDF 5.5.3。' >&2
        return 1
    fi
}

select_port() {
    python - "${serial_port}" <<'PY'
import sys
from serial.tools import list_ports

ports = list(list_ports.comports())
requested = sys.argv[1]
if requested:
    matches = [p.device for p in ports if p.device == requested]
else:
    matches = [p.device for p in ports if p.vid == 0x303A and p.pid == 0x1001
               and (sys.platform != "darwin" or p.device.startswith("/dev/cu."))]
if len(matches) != 1:
    if not matches:
        print("未检测到目标设备，请开机并用 USB 数据线连接电脑。", file=sys.stderr)
    else:
        print("检测到多个 ESP 设备，请把目标串口作为脚本参数：", file=sys.stderr)
        print("\n".join(matches), file=sys.stderr)
    sys.exit(1)
print(matches[0])
PY
}

check_device_layout() {
    local table_offset
    table_offset="$(python - "${build_dir}" <<'PY'
import sys
from pathlib import Path
from tools.verify_firmware import parse_flash_args

images = parse_flash_args((Path(sys.argv[1]) / "flash_args").read_text())
print(hex(images["partition_table/partition-table.bin"]))
PY
)"
    printf '%s\n' '正在核对设备分区，保留现有配置…'
    python -m esptool --chip esp32c3 --port "${serial_port}" --baud 460800 \
        --connect-attempts 3 read_flash "${table_offset}" 0x1000 "${build_dir}/device-partition.bin"
    python - "${build_dir}" <<'PY'
import sys
from pathlib import Path

root = Path(sys.argv[1])
expected = (root / "partition_table/partition-table.bin").read_bytes()
actual = (root / "device-partition.bin").read_bytes()
if actual[:len(expected)] != expected:
    sys.exit("设备分区与当前固件不同，已停止刷写；请先核对分区和需要保留的数据。")
print("分区一致，可以保留现有配置。")
PY
}

cd "${repo_root}"
activate_idf
set -u
if [[ "${build_only}" == false ]]; then
    serial_port="$(select_port)"
    printf '目标设备：%s\n' "${serial_port}"
fi
printf '%s\n' '正在构建当前代码…'
export SDKCONFIG_DEFAULTS="${repo_root}/sdkconfig.defaults"
idf.py -B "${build_dir}" -D "SDKCONFIG=${build_dir}/sdkconfig" build
idf.py -B "${build_dir}" merge-bin -o "${build_dir}/FoloToy-AI-Passport-full.bin"
python tools/verify_firmware.py "${build_dir}"
python tools/archive_firmware.py create "${build_dir}" --archive-root "${repo_root}/build/firmware"
if [[ "${build_only}" == true ]]; then
    printf '%s\n' '构建和固件校验完成，未刷写设备。'
    exit 0
fi
check_device_layout
printf '%s\n' '正在分段刷写…'
idf.py -B "${build_dir}" -p "${serial_port}" -b 460800 flash
printf '%s\n' '刷写完成，设备已重启。'
