#!/usr/bin/env bash
# Demonic C (DMC) Unix Installer
# Supports Linux and macOS (x86_64 and arm64)
#
# Usage:
#   curl -fsSL https://raw.githubusercontent.com/ak495867/Demonic-C/main/install.sh | bash
#   or
#   wget -qO- https://raw.githubusercontent.com/ak495867/Demonic-C/main/install.sh | bash

set -euo pipefail

REPO="ak495867/Demonic-C"
INSTALL_DIR="${DMC_INSTALL_DIR:-$HOME/.dmc}"
VERSION="${1:-latest}"
LOCAL_ARCHIVE="${LOCAL_ARCHIVE:-}"

RED='\033[0;31m'
GREEN='\033[0;32m'
CYAN='\033[0;36m'
YELLOW='\033[1;33m'
GRAY='\033[0;90m'
NC='\033[0m' # No Color

print_banner() {
    printf "${RED}"
    cat << 'EOF'
   ___                         _       _____ 
  / _ \___ __ _ ___  _ _  (_)__   /  __ \
 / // / -_)  ' \/ _ \/ ' \/ / _ \ / / / /
/____/\__/_/_/_/\___/_/_/_/_/\___//_/ /_/ 
Demonic C Systems Programming Language Installer
EOF
    printf "${NC}\n"
}

print_banner

# Detect OS
OS_RAW="$(uname -s)"
case "${OS_RAW}" in
    Linux*)     OS="linux" ;;
    Darwin*)    OS="macos" ;;
    *)
        printf "${RED}Error: Unsupported operating system: ${OS_RAW}${NC}\n"
        exit 1
        ;;
esac

# Detect Architecture
ARCH_RAW="$(uname -m)"
case "${ARCH_RAW}" in
    x86_64|amd64)   ARCH="x86_64" ;;
    aarch64|arm64)  ARCH="arm64" ;;
    *)
        printf "${RED}Error: Unsupported architecture: ${ARCH_RAW}${NC}\n"
        exit 1
        ;;
esac

ASSET_PREFIX="dmc-${OS}-${ARCH}"
ARCHIVE_EXT="tar.gz"

printf "${CYAN}Target platform detected: ${OS}-${ARCH}${NC}\n"

TEMP_DIR="$(mktemp -d 2>/dev/null || mktemp -d -t 'dmc-install')"
cleanup() {
    rm -rf "${TEMP_DIR}"
}
trap cleanup EXIT

ARCHIVE_PATH="${TEMP_DIR}/${ASSET_PREFIX}.${ARCHIVE_EXT}"

if [ -n "${LOCAL_ARCHIVE}" ]; then
    printf "${CYAN}Installing from local archive: ${LOCAL_ARCHIVE}${NC}\n"
    cp "${LOCAL_ARCHIVE}" "${ARCHIVE_PATH}"
else
    # Determine download tool
    DOWNLOADER=""
    if command -v curl >/dev/null 2>&1; then
        DOWNLOADER="curl"
    elif command -v wget >/dev/null 2>&1; then
        DOWNLOADER="wget"
    else
        printf "${RED}Error: curl or wget is required to download Demonic C.${NC}\n"
        exit 1
    fi

    DOWNLOAD_URL=""
    if [ "${VERSION}" = "latest" ]; then
        printf "${CYAN}Querying GitHub for latest release...${NC}\n"
        API_URL="https://api.github.com/repos/${REPO}/releases/latest"
        
        API_RESP=""
        if [ "${DOWNLOADER}" = "curl" ]; then
            API_RESP="$(curl -fsSL -H "User-Agent: dmc-installer" "${API_URL}" 2>/dev/null || true)"
        else
            API_RESP="$(wget -qO- --user-agent="dmc-installer" "${API_URL}" 2>/dev/null || true)"
        fi

        if [ -n "${API_RESP}" ]; then
            DOWNLOAD_URL="$(printf '%s' "${API_RESP}" | grep -o "https://[^\"]*${ASSET_PREFIX}\\.${ARCHIVE_EXT}" | head -n 1 || true)"
        fi

        if [ -z "${DOWNLOAD_URL}" ]; then
            printf "${YELLOW}API lookup unavailable, falling back to direct release download...${NC}\n"
            DOWNLOAD_URL="https://github.com/${REPO}/releases/latest/download/${ASSET_PREFIX}.${ARCHIVE_EXT}"
        fi
    else
        DOWNLOAD_URL="https://github.com/${REPO}/releases/download/${VERSION}/${ASSET_PREFIX}.${ARCHIVE_EXT}"
    fi

    printf "${CYAN}Downloading ${DOWNLOAD_URL}...${NC}\n"
    if [ "${DOWNLOADER}" = "curl" ]; then
        curl -fsSL --retry 3 --retry-delay 2 "${DOWNLOAD_URL}" -o "${ARCHIVE_PATH}"
    else
        wget -q --tries=3 --waitretry=2 "${DOWNLOAD_URL}" -O "${ARCHIVE_PATH}"
    fi
fi

# Ensure install directory exists
mkdir -p "${INSTALL_DIR}"
printf "${CYAN}Extracting to ${INSTALL_DIR}...${NC}\n"
tar -xzf "${ARCHIVE_PATH}" -C "${INSTALL_DIR}"

# Flatten if archive contained a top-level dmc folder
if [ -d "${INSTALL_DIR}/dmc" ] && [ -d "${INSTALL_DIR}/dmc/bin" ]; then
    cp -r "${INSTALL_DIR}/dmc/"* "${INSTALL_DIR}/"
    rm -rf "${INSTALL_DIR}/dmc"
fi

BIN_DIR="${INSTALL_DIR}/bin"
mkdir -p "${BIN_DIR}"

# Ensure permissions
if [ -f "${BIN_DIR}/dmc" ]; then
    chmod +x "${BIN_DIR}/dmc"
fi
if [ -f "${BIN_DIR}/dmc-native" ]; then
    chmod +x "${BIN_DIR}/dmc-native"
fi
if [ -f "${INSTALL_DIR}/dmc" ]; then
    chmod +x "${INSTALL_DIR}/dmc"
fi

# Add to Shell Profiles
add_to_profile() {
    PROFILE_FILE="$1"
    LINE_TO_ADD="$2"
    if [ -f "${PROFILE_FILE}" ]; then
        if ! grep -q "${INSTALL_DIR}" "${PROFILE_FILE}"; then
            printf "\n# Demonic C Language PATH\n%s\n" "${LINE_TO_ADD}" >> "${PROFILE_FILE}"
            printf "${GREEN}Added DMC to ${PROFILE_FILE}${NC}\n"
        fi
    fi
}

PATH_ENTRY="export PATH=\"${BIN_DIR}:${INSTALL_DIR}:\$PATH\""
SHELL_NAME="$(basename "${SHELL:-bash}")"

case "${SHELL_NAME}" in
    zsh)
        add_to_profile "${HOME}/.zshrc" "${PATH_ENTRY}"
        ;;
    bash)
        add_to_profile "${HOME}/.bashrc" "${PATH_ENTRY}"
        add_to_profile "${HOME}/.bash_profile" "${PATH_ENTRY}"
        ;;
    *)
        add_to_profile "${HOME}/.profile" "${PATH_ENTRY}"
        ;;
esac

# Fallback generic profile update
if [ -f "${HOME}/.profile" ]; then
    add_to_profile "${HOME}/.profile" "${PATH_ENTRY}"
fi

export PATH="${BIN_DIR}:${INSTALL_DIR}:${PATH}"

printf "\n${GREEN}Demonic C has been installed successfully!${NC}\n"

DMC_EXEC="${BIN_DIR}/dmc"
if [ ! -x "${DMC_EXEC}" ]; then
    DMC_EXEC="${INSTALL_DIR}/dmc"
fi

if [ -x "${DMC_EXEC}" ]; then
    INSTALLED_VER="$("${DMC_EXEC}" --version 2>&1 || true)"
    printf "${CYAN}Installed: ${INSTALLED_VER}${NC}\n"
fi

cat << EOF

Quickstart:
  dmc --version          Check version
  dmc new my-project     Create a new project
  dmc build main.dmc     Compile directly to executable
  dmc test test-files    Run test suite

If 'dmc' is not yet found in your current shell session, run:
  export PATH="${BIN_DIR}:${INSTALL_DIR}:\$PATH"

Enjoy programming in Demonic C!
EOF
