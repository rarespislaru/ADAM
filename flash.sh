#!/usr/bin/env bash
# ==============================================================================
# ESP32 flashing helper (ESP32-WROOM / classic ESP32)
# ==============================================================================
# Usage:
#   ./flash.sh                      # Auto-detect port, build if needed, flash
#   ./flash.sh -p /dev/ttyUSB0      # Flash a specific port
#   ./flash.sh -m                   # Flash, then open the serial monitor
#   ./flash.sh -e                   # Erase flash before writing
#   ./flash.sh -a                   # Flash application binary only (fast)
#   ./flash.sh -b 115200            # Lower baud rate (noisy lines)
#   ./flash.sh -i                   # Show chip info / MAC / flash size
#
# Environment variables (all optional):
#   PROJECT_NAME     Project/binary name (default: read from build/, else "ADAM")
#   CHIP             Target chip              (default: esp32)
#   FLASH_SIZE       Flash size override      (default: from build, else "detect")
#   FLASH_MODE       Flash mode override      (default: from build, else "dio")
#   FLASH_FREQ       Flash frequency override (default: from build, else "40m")
#   IDF_PATH         ESP-IDF location (its export.sh will be sourced if needed)
#   ESP_IDF_EXPORT   Full path to an ESP-IDF export.sh
# ==============================================================================

set -Eeuo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "${SCRIPT_DIR}"

# --- ANSI colors (disabled when stdout is not a terminal) ---
if [[ -t 1 ]]; then
    C_RESET=$'\e[0m';  C_BOLD=$'\e[1m'
    C_RED=$'\e[0;31m'; C_GREEN=$'\e[0;32m'; C_YELLOW=$'\e[0;33m'
    C_BLUE=$'\e[0;34m'; C_CYAN=$'\e[0;36m'; C_GRAY=$'\e[0;90m'
else
    C_RESET=''; C_BOLD=''; C_RED=''; C_GREEN=''; C_YELLOW=''
    C_BLUE=''; C_CYAN=''; C_GRAY=''
fi

log_info() { printf '%s[INFO]%s %s\n'  "${C_CYAN}"   "${C_RESET}" "$*"; }
log_ok()   { printf '%s[OK]%s %s\n'    "${C_GREEN}"  "${C_RESET}" "$*"; }
log_warn() { printf '%s[WARN]%s %s\n'  "${C_YELLOW}" "${C_RESET}" "$*"; }
log_err()  { printf '%s[ERROR]%s %s\n' "${C_RED}"    "${C_RESET}" "$*" >&2; }
log_step() { printf '\n%s%s==>%s %s%s%s\n' "${C_BOLD}" "${C_BLUE}" "${C_RESET}" "${C_BOLD}" "$*" "${C_RESET}"; }

# --- Defaults ---
PORT=""
FLASH_BAUD="460800"
MONITOR_BAUD="115200"
DO_BUILD=0
DO_ERASE=0
ERASE_ONLY=0
APP_ONLY=0
DO_MONITOR=0
DO_INFO=0
CHIP="${CHIP:-esp32}"
PROJECT_NAME="${PROJECT_NAME:-}"
# Empty means "use whatever the build says, else a sane default"
FLASH_MODE="${FLASH_MODE:-}"
FLASH_FREQ="${FLASH_FREQ:-}"
FLASH_SIZE="${FLASH_SIZE:-}"

ESPTOOL=()
CMD_WRITE_FLASH="write_flash"
CMD_ERASE_FLASH="erase_flash"
CMD_CHIP_ID="chip_id"
CMD_FLASH_ID="flash_id"
BOOTLOADER_BIN=""
PARTITION_BIN=""
APP_BIN=""
APP_OFFSET="0x10000"

show_help() {
    cat << EOF
${C_BOLD}ESP32 Flashing Utility${C_RESET}

${C_BOLD}USAGE:${C_RESET}
    ./flash.sh [OPTIONS] [PORT]

${C_BOLD}OPTIONS:${C_RESET}
    -p, --port <PORT>        Serial port (e.g. /dev/ttyUSB0, /dev/ttyACM0)
    -b, --baud <BAUD>        Flashing baud rate (default: ${FLASH_BAUD})
    -m, --monitor            Open serial monitor after flashing
    --monitor-baud <BAUD>    Monitor baud rate (default: ${MONITOR_BAUD})
    -B, --build              Force a project build before flashing
    -e, --erase              Erase entire flash before flashing
    --erase-only             Erase flash and exit
    -a, --app-only           Flash the application binary only
    -i, --info               Show chip info, MAC address and flash details
    -h, --help               Show this help

${C_BOLD}EXAMPLES:${C_RESET}
    ./flash.sh                          # Auto-detect port and flash
    ./flash.sh /dev/ttyUSB0             # Flash on a specific port
    ./flash.sh -p /dev/ttyUSB0 -m       # Flash and open monitor
    ./flash.sh -e                       # Erase, then flash (fresh boards)
    ./flash.sh -b 115200                # Slow, safe flashing

${C_BOLD}TROUBLESHOOTING:${C_RESET}
    1. Manual bootloader mode (on "Timed out waiting for packet header"):
       hold BOOT (GPIO0), tap RESET/EN, release BOOT, re-run the script.

    2. Strapping pins:
       GPIO0  LOW for download mode, HIGH for normal boot
       GPIO2  LOW or floating at boot
       GPIO12 LOW for 3.3V flash voltage
       GPIO15 HIGH to silence ROM boot log
       EN     10k pull-up to 3.3V plus 100nF-1uF to GND

    3. Linux permissions (permission denied on /dev/ttyUSB*):
       sudo usermod -a -G dialout \$USER   (uucp on Arch), then log out/in
EOF
}

require_value() {
    if [[ -z "${2:-}" ]]; then
        log_err "Option $1 requires a value."
        exit 1
    fi
}

# --- Parse arguments ---
while [[ $# -gt 0 ]]; do
    case "$1" in
        -h|--help)       show_help; exit 0 ;;
        -p|--port)       require_value "$1" "${2:-}"; PORT="$2"; shift 2 ;;
        -b|--baud)       require_value "$1" "${2:-}"; FLASH_BAUD="$2"; shift 2 ;;
        --monitor-baud)  require_value "$1" "${2:-}"; MONITOR_BAUD="$2"; shift 2 ;;
        -m|--monitor)    DO_MONITOR=1; shift ;;
        -B|--build)      DO_BUILD=1; shift ;;
        -e|--erase)      DO_ERASE=1; shift ;;
        --erase-only)    ERASE_ONLY=1; shift ;;
        -a|--app-only)   APP_ONLY=1; shift ;;
        -i|--info)       DO_INFO=1; shift ;;
        /dev/*)          PORT="$1"; shift ;;
        *)
            log_err "Unknown option: $1"
            show_help
            exit 1
            ;;
    esac
done

# --- Environment ---
have() { command -v "$1" >/dev/null 2>&1; }

esptool_available() {
    have esptool || have esptool.py || python3 -m esptool version >/dev/null 2>&1
}

ensure_environment() {
    log_step "Checking environment..."

    if have idf.py && esptool_available; then
        log_ok "ESP-IDF environment already active."
        return 0
    fi

    local candidates=(
        "${ESP_IDF_EXPORT:-}"
        "${IDF_PATH:+${IDF_PATH}/export.sh}"
        "${SCRIPT_DIR}/esp-idf/export.sh"
        "${SCRIPT_DIR}/../esp-idf/export.sh"
        "${HOME}/esp/esp-idf/export.sh"
    )

    local cand
    for cand in "${candidates[@]}"; do
        if [[ -n "${cand}" && -f "${cand}" ]]; then
            log_info "Sourcing ESP-IDF environment from: ${cand}"
            # export.sh is not written for 'set -u'
            set +u
            # shellcheck disable=SC1090
            source "${cand}" >/dev/null 2>&1 || true
            set -u
            if have idf.py || esptool_available; then
                log_ok "ESP-IDF environment loaded."
                return 0
            fi
        fi
    done

    # Fallback: an ESP-IDF python virtualenv containing esptool
    local vbin
    for vbin in "${HOME}"/.espressif/python_env/*/bin; do
        if [[ -d "${vbin}" && ( -x "${vbin}/esptool" || -x "${vbin}/esptool.py" ) ]]; then
            export PATH="${vbin}:${PATH}"
            log_ok "Added ESP virtualenv to PATH: ${vbin}"
            return 0
        fi
    done

    log_warn "Could not load ESP-IDF. Will try a standalone esptool if installed."
}

# Fills the ESPTOOL array and picks command names (esptool v5 uses hyphens)
setup_esptool() {
    if have esptool; then
        ESPTOOL=(esptool)
    elif have esptool.py; then
        ESPTOOL=(esptool.py)
    elif python3 -m esptool version >/dev/null 2>&1; then
        ESPTOOL=(python3 -m esptool)
    else
        log_err "esptool not found. Install ESP-IDF, or run: pip install esptool"
        exit 1
    fi

    if "${ESPTOOL[@]}" --help 2>&1 | grep -q 'write-flash'; then
        CMD_WRITE_FLASH="write-flash"
        CMD_ERASE_FLASH="erase-flash"
        CMD_CHIP_ID="chip-id"
        CMD_FLASH_ID="flash-id"
    fi
}

# --- Serial port ---
detect_port() {
    if [[ -n "${PORT}" ]]; then
        if [[ ! -e "${PORT}" ]]; then
            log_err "Specified port '${PORT}' does not exist."
            exit 1
        fi
        return 0
    fi

    log_step "Detecting serial port..."

    local found=()
    local p
    shopt -s nullglob
    for p in /dev/ttyUSB* /dev/ttyACM* \
             /dev/cu.usbserial* /dev/cu.usbmodem* /dev/cu.SLAB_USBtoUART* /dev/cu.wchusbserial*; do
        [[ -e "${p}" ]] && found+=("${p}")
    done
    shopt -u nullglob

    if [[ ${#found[@]} -eq 0 ]]; then
        log_err "No USB serial ports found."
        cat << EOF

Troubleshooting:
  1. Is the board connected with a data cable (not charge-only)?
  2. Check kernel messages: dmesg | tail -n 20
  3. Check USB devices:     lsusb
  4. Or pass the port explicitly: ./flash.sh -p /dev/ttyUSB0
EOF
        exit 1
    elif [[ ${#found[@]} -eq 1 ]]; then
        PORT="${found[0]}"
        log_ok "Auto-selected port: ${C_BOLD}${PORT}${C_RESET}"
    else
        if [[ ! -t 0 ]]; then
            log_err "Multiple ports found (${found[*]}). Specify one with -p."
            exit 1
        fi
        log_info "Multiple serial ports detected:"
        local i
        for i in "${!found[@]}"; do
            printf '  [%s%d%s] %s\n' "${C_BOLD}" "$((i + 1))" "${C_RESET}" "${found[$i]}"
        done
        local choice
        read -r -p "Select port number [1-${#found[@]}] (default 1): " choice
        choice="${choice:-1}"
        if [[ "${choice}" =~ ^[0-9]+$ ]] && (( choice >= 1 && choice <= ${#found[@]} )); then
            PORT="${found[$((choice - 1))]}"
            log_ok "Selected port: ${C_BOLD}${PORT}${C_RESET}"
        else
            log_err "Invalid selection."
            exit 1
        fi
    fi
}

verify_permissions() {
    if [[ ! -w "${PORT}" ]]; then
        local user="${USER:-$(id -un)}"
        log_err "No write permission on ${PORT}."
        printf 'Add your user to the serial group, then log out and back in:\n'
        printf '  %ssudo usermod -a -G dialout %s%s   (uucp on Arch Linux)\n' "${C_CYAN}" "${user}" "${C_RESET}"
        exit 1
    fi
}

# --- Build ---
build_project() {
    log_step "Building project..."
    if have idf.py; then
        idf.py build
    else
        log_err "idf.py not found in environment; cannot build."
        exit 1
    fi
}

# --- Binary discovery ---
resolve_project_name() {
    [[ -n "${PROJECT_NAME}" ]] && return 0

    local desc="build/project_description.json"
    if [[ -f "${desc}" ]]; then
        PROJECT_NAME="$(sed -n 's/.*"project_name"[[:space:]]*:[[:space:]]*"\([^"]*\)".*/\1/p' "${desc}" | head -n1)"
    fi
    PROJECT_NAME="${PROJECT_NAME:-ADAM}"
}

locate_binaries() {
    resolve_project_name

    BOOTLOADER_BIN=""
    PARTITION_BIN=""
    APP_BIN=""
    [[ -f "build/bootloader/bootloader.bin" ]]               && BOOTLOADER_BIN="build/bootloader/bootloader.bin"
    [[ -f "build/partition_table/partition-table.bin" ]]     && PARTITION_BIN="build/partition_table/partition-table.bin"
    [[ -f "build/${PROJECT_NAME}.bin" ]]                     && APP_BIN="build/${PROJECT_NAME}.bin"

    # Use the real app offset from the build's flash_args when available
    if [[ -f build/flash_args ]]; then
        local off
        off="$(awk -v f="${PROJECT_NAME}.bin" '$2 == f { print $1 }' build/flash_args)"
        [[ -n "${off}" ]] && APP_OFFSET="${off}"
    fi
}

find_binaries() {
    locate_binaries

    if [[ "${ERASE_ONLY}" -eq 1 || "${DO_INFO}" -eq 1 ]]; then
        return 0
    fi

    local missing=0
    if [[ "${APP_ONLY}" -eq 1 ]]; then
        [[ -z "${APP_BIN}" ]] && missing=1
    else
        [[ -z "${BOOTLOADER_BIN}" || -z "${PARTITION_BIN}" || -z "${APP_BIN}" ]] && missing=1
    fi

    if [[ "${missing}" -eq 1 ]]; then
        log_warn "Binaries missing in build/. Building..."
        build_project
        locate_binaries
        if [[ -z "${APP_BIN}" ]]; then
            log_err "Build finished but build/${PROJECT_NAME}.bin was not found."
            log_err "Set PROJECT_NAME if your project is named differently."
            exit 1
        fi
    fi
}

# --- Failure help ---
print_boot_help() {
    log_err "Flashing failed or could not connect to the ESP32."
    cat << EOF

${C_BOLD}Recovery guide${C_RESET}
  1. Manual download mode (boards without auto-reset circuitry):
       hold BOOT (GPIO0), tap RESET/EN, release BOOT,
       then re-run: ./flash.sh -p ${PORT:-/dev/ttyUSB0}
  2. Lower the baud rate (noisy wiring / weak power):
       ./flash.sh -b 115200
  3. Check strapping pins: GPIO0, GPIO2, GPIO12, GPIO15, and EN pull-up.
  4. Make sure the 3.3V rail can supply ~500mA peaks.
  See ./flash.sh --help for details.

EOF
}

# --- Operations ---
read_chip_info() {
    log_step "Querying chip info on ${PORT}..."
    "${ESPTOOL[@]}" --port "${PORT}" --baud "${FLASH_BAUD}" --chip "${CHIP}" "${CMD_CHIP_ID}" || {
        print_boot_help
        exit 1
    }
    echo
    "${ESPTOOL[@]}" --port "${PORT}" --baud "${FLASH_BAUD}" --chip "${CHIP}" "${CMD_FLASH_ID}" || true
}

erase_flash() {
    log_step "Erasing entire flash on ${PORT}..."
    "${ESPTOOL[@]}" --port "${PORT}" --baud "${FLASH_BAUD}" --chip "${CHIP}" "${CMD_ERASE_FLASH}" || {
        print_boot_help
        exit 1
    }
    log_ok "Flash erased."
}

flash_target() {
    log_step "Flashing ${CHIP} on ${PORT} (baud: ${FLASH_BAUD})..."

    local base=(
        "${ESPTOOL[@]}"
        --chip "${CHIP}"
        --port "${PORT}"
        --baud "${FLASH_BAUD}"
        --before default_reset
        --after hard_reset
        "${CMD_WRITE_FLASH}"
    )
    local overrides=()
    [[ -n "${FLASH_MODE}" ]] && overrides+=(--flash_mode "${FLASH_MODE}")
    [[ -n "${FLASH_FREQ}" ]] && overrides+=(--flash_freq "${FLASH_FREQ}")
    [[ -n "${FLASH_SIZE}" ]] && overrides+=(--flash_size "${FLASH_SIZE}")

    local ok=0
    if [[ "${APP_ONLY}" -eq 1 ]]; then
        log_info "App binary: ${APP_BIN} -> ${APP_OFFSET}"
        # Without explicit overrides, keep the header settings esptool detects
        [[ ${#overrides[@]} -eq 0 ]] && overrides=(--flash_size detect)
        "${base[@]}" -z "${overrides[@]}" "${APP_OFFSET}" "${APP_BIN}" && ok=1
    elif [[ -f build/flash_args ]]; then
        # Preferred: exactly what ESP-IDF says to flash (offsets, mode, freq, size)
        log_info "Using build/flash_args from ESP-IDF"
        ( cd build && "${base[@]}" "${overrides[@]}" @flash_args ) && ok=1
    else
        log_info "Bootloader: ${BOOTLOADER_BIN} -> 0x1000"
        log_info "Partitions: ${PARTITION_BIN} -> 0x8000"
        log_info "App binary: ${APP_BIN} -> ${APP_OFFSET}"
        [[ ${#overrides[@]} -eq 0 ]] && overrides=(--flash_mode dio --flash_freq 40m --flash_size detect)
        "${base[@]}" -z "${overrides[@]}" \
            0x1000 "${BOOTLOADER_BIN}" \
            0x8000 "${PARTITION_BIN}" \
            "${APP_OFFSET}" "${APP_BIN}" && ok=1
    fi

    if [[ "${ok}" -eq 1 ]]; then
        echo
        log_ok "${C_BOLD}Flashing completed successfully!${C_RESET}"
    else
        echo
        print_boot_help
        exit 1
    fi
}

launch_monitor() {
    log_step "Launching serial monitor on ${PORT} @ ${MONITOR_BAUD} baud..."
    printf '%s(Press Ctrl+] to exit)%s\n\n' "${C_GRAY}" "${C_RESET}"

    if have idf.py; then
        IDF_MONITOR_BAUD="${MONITOR_BAUD}" MONITORBAUD="${MONITOR_BAUD}" \
            idf.py -p "${PORT}" monitor
    elif have pyserial-miniterm; then
        pyserial-miniterm "${PORT}" "${MONITOR_BAUD}"
    elif have miniterm.py; then
        miniterm.py "${PORT}" "${MONITOR_BAUD}"
    elif python3 -m serial.tools.miniterm --help >/dev/null 2>&1; then
        python3 -m serial.tools.miniterm "${PORT}" "${MONITOR_BAUD}"
    else
        log_warn "No monitor tool found. Try one of:"
        echo "  screen ${PORT} ${MONITOR_BAUD}"
        echo "  minicom -D ${PORT} -b ${MONITOR_BAUD}"
    fi
}

# --- Main ---
main() {
    printf '%s====================================================%s\n' "${C_BOLD}" "${C_RESET}"
    printf '%s              ESP32 Flasher Utility                 %s\n' "${C_BOLD}" "${C_RESET}"
    printf '%s====================================================%s\n' "${C_BOLD}" "${C_RESET}"

    ensure_environment
    setup_esptool
    detect_port
    verify_permissions

    [[ "${DO_BUILD}" -eq 1 ]] && build_project

    find_binaries

    if [[ "${DO_INFO}" -eq 1 ]]; then
        read_chip_info
        exit 0
    fi

    if [[ "${ERASE_ONLY}" -eq 1 ]]; then
        erase_flash
        exit 0
    fi

    [[ "${DO_ERASE}" -eq 1 ]] && erase_flash

    flash_target

    [[ "${DO_MONITOR}" -eq 1 ]] && launch_monitor
    return 0
}

main