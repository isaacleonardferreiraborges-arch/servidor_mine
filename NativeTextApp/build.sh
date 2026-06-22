#!/usr/bin/env bash
# =============================================================================
# build.sh — Compila NativeTextApp 100% no terminal
#
# Uso:
#   chmod +x build.sh
#   ./build.sh              # build debug
#   ./build.sh release      # build release
#   ./build.sh install      # build + instala via adb
#   ./build.sh clean        # limpa build
# =============================================================================
set -euo pipefail

# ── Cores para output ──────────────────────────────────────────────────────────
RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'
CYAN='\033[0;36m'; BOLD='\033[1m'; RESET='\033[0m'

info()  { echo -e "${CYAN}[INFO]${RESET}  $*"; }
ok()    { echo -e "${GREEN}[OK]${RESET}    $*"; }
warn()  { echo -e "${YELLOW}[WARN]${RESET}  $*"; }
err()   { echo -e "${RED}[ERRO]${RESET}  $*" >&2; }
die()   { err "$*"; exit 1; }

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

BUILD_TYPE="${1:-debug}"
[[ "$BUILD_TYPE" == "clean" ]] && { info "Limpando..."; ./gradlew clean; ok "Limpo."; exit 0; }

# =============================================================================
# 1. Localizar Android SDK
# =============================================================================
find_sdk() {
    # Candidatos em ordem de preferência
    local candidates=(
        "$ANDROID_HOME"
        "$ANDROID_SDK_ROOT"
        "$HOME/Android/Sdk"                          # Linux padrão
        "$HOME/android-sdk"
        "/opt/android-sdk"
        "/usr/lib/android-sdk"
        "$HOME/Library/Android/sdk"                  # macOS
        "$LOCALAPPDATA/Android/Sdk"                  # Windows/Git Bash
    )
    for d in "${candidates[@]}"; do
        [[ -n "$d" && -d "$d/platforms" ]] && { echo "$d"; return 0; }
    done
    return 1
}

SDK_DIR=""
if SDK_DIR=$(find_sdk); then
    ok "Android SDK: $SDK_DIR"
else
    echo ""
    warn "Android SDK não encontrado automaticamente."
    echo -e "${BOLD}Informe o caminho do SDK (ex: /home/user/Android/Sdk):${RESET}"
    read -r SDK_DIR
    [[ -d "$SDK_DIR/platforms" ]] || die "Caminho inválido: $SDK_DIR"
fi

# =============================================================================
# 2. Localizar NDK
# =============================================================================
find_ndk() {
    local sdk="$1"
    # ndk/ ou ndk-bundle/
    local ndk_dir="$sdk/ndk"
    if [[ -d "$ndk_dir" ]]; then
        # Pega a versão mais recente disponível
        local latest
        latest=$(ls -1 "$ndk_dir" | grep -E '^[0-9]+\.' | sort -V | tail -1)
        [[ -n "$latest" ]] && { echo "$ndk_dir/$latest"; return 0; }
    fi
    [[ -d "$sdk/ndk-bundle" ]] && { echo "$sdk/ndk-bundle"; return 0; }
    return 1
}

NDK_DIR=""
if NDK_DIR=$(find_ndk "$SDK_DIR"); then
    ok "NDK: $NDK_DIR"
else
    warn "NDK não encontrado em $SDK_DIR/ndk/"
    echo ""
    echo -e "${BOLD}Opções:${RESET}"
    echo "  1) Instalar NDK agora via sdkmanager (recomendado)"
    echo "  2) Informar caminho manual"
    read -rp "Escolha [1/2]: " choice
    if [[ "$choice" == "1" ]]; then
        SDKMANAGER="$SDK_DIR/cmdline-tools/latest/bin/sdkmanager"
        [[ ! -f "$SDKMANAGER" ]] && SDKMANAGER="$SDK_DIR/tools/bin/sdkmanager"
        [[ ! -f "$SDKMANAGER" ]] && die "sdkmanager não encontrado. Instale as Command-line Tools primeiro:\n  https://developer.android.com/studio#command-tools"
        info "Instalando NDK 26.3..."
        "$SDKMANAGER" "ndk;26.3.11579264" "cmake;3.22.1"
        NDK_DIR="$SDK_DIR/ndk/26.3.11579264"
    else
        read -rp "Caminho do NDK: " NDK_DIR
        [[ -d "$NDK_DIR/toolchains" ]] || die "NDK inválido: $NDK_DIR"
    fi
fi

NDK_VERSION=$(basename "$NDK_DIR" | grep -oE '^[0-9]+' || echo "?")
ok "NDK versão: $NDK_VERSION"

# =============================================================================
# 3. Verificar Java
# =============================================================================
if ! command -v java &>/dev/null; then
    die "Java não encontrado. Instale JDK 17:\n  Ubuntu/Debian: sudo apt install openjdk-17-jdk\n  macOS:         brew install openjdk@17"
fi
JAVA_VER=$(java -version 2>&1 | head -1)
ok "Java: $JAVA_VER"

# =============================================================================
# 4. Escrever local.properties
# =============================================================================
cat > local.properties <<EOF
sdk.dir=${SDK_DIR}
ndk.dir=${NDK_DIR}
EOF
ok "local.properties configurado"

# =============================================================================
# 5. Verificar/instalar plataforma Android 34
# =============================================================================
if [[ ! -d "$SDK_DIR/platforms/android-34" ]]; then
    warn "platforms/android-34 não encontrado"
    SDKMANAGER="$SDK_DIR/cmdline-tools/latest/bin/sdkmanager"
    [[ ! -f "$SDKMANAGER" ]] && SDKMANAGER="$SDK_DIR/tools/bin/sdkmanager"
    if [[ -f "$SDKMANAGER" ]]; then
        info "Instalando platform android-34 e build-tools..."
        "$SDKMANAGER" "platforms;android-34" "build-tools;34.0.0"
    else
        warn "sdkmanager não disponível — instale manualmente: platforms;android-34"
    fi
fi

# =============================================================================
# 6. Garantir que o gradlew está executável
# =============================================================================
chmod +x gradlew

# =============================================================================
# 7. Build
# =============================================================================
echo ""
echo -e "${BOLD}═══════════════════════════════════════${RESET}"
info "Iniciando build: ${BOLD}${BUILD_TYPE}${RESET}"
info "A 1ª compilação baixa FreeType (~2 MB) e demora mais"
echo -e "${BOLD}═══════════════════════════════════════${RESET}"
echo ""

GRADLE_TASK=""
case "$BUILD_TYPE" in
    debug)   GRADLE_TASK="assembleDebug"   ;;
    release) GRADLE_TASK="assembleRelease" ;;
    install) GRADLE_TASK="assembleDebug"   ;;
    *)       die "Tipo inválido: $BUILD_TYPE  (use: debug | release | install | clean)" ;;
esac

# Opções de performance para Gradle
export GRADLE_OPTS="-Xmx2g -Dorg.gradle.daemon=false -Dorg.gradle.parallel=true"

./gradlew "$GRADLE_TASK" --stacktrace 2>&1 | tee build_output.log
GRADLE_EXIT=${PIPESTATUS[0]}

if [[ $GRADLE_EXIT -ne 0 ]]; then
    echo ""
    err "Build falhou. Últimas 30 linhas do log:"
    tail -30 build_output.log
    echo ""
    err "Log completo salvo em: build_output.log"
    exit 1
fi

# =============================================================================
# 8. Localizar APK gerado
# =============================================================================
APK_PATH=""
if [[ "$BUILD_TYPE" == "release" ]]; then
    APK_PATH="app/build/outputs/apk/release/app-release-unsigned.apk"
    [[ -f "app/build/outputs/apk/release/app-release.apk" ]] && \
        APK_PATH="app/build/outputs/apk/release/app-release.apk"
else
    APK_PATH="app/build/outputs/apk/debug/app-debug.apk"
fi

echo ""
echo -e "${GREEN}${BOLD}╔══════════════════════════════════════════╗${RESET}"
echo -e "${GREEN}${BOLD}║        BUILD CONCLUÍDO COM SUCESSO!      ║${RESET}"
echo -e "${GREEN}${BOLD}╚══════════════════════════════════════════╝${RESET}"
echo ""
ok "APK: ${BOLD}${APK_PATH}${RESET}"
[[ -f "$APK_PATH" ]] && ok "Tamanho: $(du -h "$APK_PATH" | cut -f1)"

# =============================================================================
# 9. Instalar via adb (se solicitado)
# =============================================================================
if [[ "$BUILD_TYPE" == "install" ]]; then
    echo ""
    info "Verificando dispositivo adb..."
    ADB="$SDK_DIR/platform-tools/adb"
    [[ ! -f "$ADB" ]] && ADB="adb"   # tenta no PATH

    if ! "$ADB" devices | grep -q "device$"; then
        warn "Nenhum dispositivo conectado via adb."
        warn "Conecte o device com USB Debugging ativado e rode:"
        echo "  $ADB install -r $APK_PATH"
    else
        info "Instalando no device..."
        "$ADB" install -r "$APK_PATH"
        ok "Instalado! Abrindo app..."
        "$ADB" shell am start -n com.example.nativetextapp/android.app.NativeActivity || true
    fi
fi

echo ""
info "Para instalar manualmente:"
echo "  adb install -r ${APK_PATH}"
echo ""
info "Para ver logs do app:"
echo "  adb logcat -s NativeTextApp"
echo ""
