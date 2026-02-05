#!/bin/bash
#
# CPify Flatpak Build Script
# Builds the Flatpak and creates a properly named bundle in the project root
#

set -e  # Exit on error

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Get script directory (project root)
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

# Configuration
APP_ID="com.github.dixonsolutions.CPify"
MANIFEST="${APP_ID}.yml"
BUILD_DIR="build-flatpak"
REPO_DIR="flatpak-repo"

# Get version from meson.build
VERSION=$(grep -oP "version:\s*'\K[^']+" meson.build | head -1)
if [ -z "$VERSION" ]; then
    echo -e "${RED}Error: Could not extract version from meson.build${NC}"
    exit 1
fi

# Detect architecture
ARCH=$(uname -m)
case "$ARCH" in
    x86_64)
        ARCH_NAME="x86_64"
        ;;
    aarch64)
        ARCH_NAME="aarch64"
        ;;
    armv7l)
        ARCH_NAME="arm"
        ;;
    i686|i386)
        ARCH_NAME="i386"
        ;;
    *)
        ARCH_NAME="$ARCH"
        ;;
esac

# Detect OS
OS_NAME="linux"

# Output filename following naming convention: cpify-{os}-{arch}.flatpak
OUTPUT_FILENAME="cpify-${OS_NAME}-${ARCH_NAME}.flatpak"

echo -e "${BLUE}╔═══════════════════════════════════════════════════════════╗${NC}"
echo -e "${BLUE}║            CPify Flatpak Build Script                     ║${NC}"
echo -e "${BLUE}╚═══════════════════════════════════════════════════════════╝${NC}"
echo ""
echo -e "${YELLOW}Version:${NC}      ${VERSION}"
echo -e "${YELLOW}Architecture:${NC} ${ARCH_NAME}"
echo -e "${YELLOW}OS:${NC}           ${OS_NAME}"
echo -e "${YELLOW}Output:${NC}       ${OUTPUT_FILENAME}"
echo ""

# Check for flatpak-builder
if ! command -v flatpak-builder &> /dev/null; then
    echo -e "${RED}Error: flatpak-builder not found${NC}"
    echo "Install with: sudo dnf install flatpak-builder"
    exit 1
fi

# Check for manifest
if [ ! -f "$MANIFEST" ]; then
    echo -e "${RED}Error: Manifest file not found: ${MANIFEST}${NC}"
    exit 1
fi

# Check if running inside a container (distrobox/toolbox)
if [ -f /run/.containerenv ] || [ -f /.dockerenv ]; then
    echo -e "${YELLOW}Warning: Running inside a container.${NC}"
    echo -e "${YELLOW}Flatpak builds work best on the host system.${NC}"
    echo ""
    read -p "Continue anyway? (y/N) " -n 1 -r
    echo
    if [[ ! $REPLY =~ ^[Yy]$ ]]; then
        echo "Aborted."
        exit 1
    fi
fi

# Step 1: Build the Flatpak
echo -e "${GREEN}[1/3] Building Flatpak...${NC}"
flatpak-builder --force-clean "$BUILD_DIR" "$MANIFEST"

# Step 2: Export to repository
echo -e "${GREEN}[2/3] Exporting to repository...${NC}"
flatpak-builder --repo="$REPO_DIR" --force-clean "$BUILD_DIR" "$MANIFEST"

# Step 3: Create bundle
echo -e "${GREEN}[3/3] Creating bundle: ${OUTPUT_FILENAME}${NC}"
flatpak build-bundle "$REPO_DIR" "$OUTPUT_FILENAME" "$APP_ID"

# Verify bundle was created
if [ -f "$OUTPUT_FILENAME" ]; then
    SIZE=$(du -h "$OUTPUT_FILENAME" | cut -f1)
    echo ""
    echo -e "${GREEN}╔═══════════════════════════════════════════════════════════╗${NC}"
    echo -e "${GREEN}║                    Build Complete!                        ║${NC}"
    echo -e "${GREEN}╚═══════════════════════════════════════════════════════════╝${NC}"
    echo ""
    echo -e "${YELLOW}Output file:${NC} ${OUTPUT_FILENAME}"
    echo -e "${YELLOW}Size:${NC}        ${SIZE}"
    echo -e "${YELLOW}Version:${NC}     ${VERSION}"
    echo ""
    echo -e "${BLUE}To install locally:${NC}"
    echo "  flatpak install --user ${OUTPUT_FILENAME}"
    echo ""
    echo -e "${BLUE}To upload to GitHub Release:${NC}"
    echo "  Tag name:      V${VERSION}"
    echo "  Release title: CPify V${VERSION} Release"
    echo "  Asset file:    ${OUTPUT_FILENAME}"
    echo ""
else
    echo -e "${RED}Error: Bundle was not created${NC}"
    exit 1
fi
