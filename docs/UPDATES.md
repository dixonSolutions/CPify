# CPify Auto-Update System

CPify includes an automatic update mechanism that checks for new releases via the GitHub API. This document describes the naming conventions required for releases to work correctly with the auto-updater.

## Overview

On startup, CPify checks the GitHub releases API for the latest release. If a newer version is found, a dialog is displayed to the user with:
- Release title and version information
- Release notes/description
- Option to download and install the update

## Version Numbering

CPify uses **Semantic Versioning (SemVer)**:

```
MAJOR.MINOR.PATCH
```

Examples:
- `0.0.1` - Initial release
- `0.1.0` - New features added
- `1.0.0` - First stable release

## Tag Naming Convention

**Format:** `V{MAJOR}.{MINOR}.{PATCH}`

| Example | Description |
|---------|-------------|
| `V0.0.1` | First release |
| `V0.1.0` | Minor version bump |
| `V1.0.0` | Major version bump |

**Rules:**
- Must start with uppercase `V`
- Use dots (`.`) as separators
- No spaces or other characters
- Numbers only after the `V`

## Release Title Naming Convention

**Format:** `CPify V{VERSION} Release`

| Example | Tag |
|---------|-----|
| `CPify V0.0.1 Release` | `V0.0.1` |
| `CPify V0.1.0 Release` | `V0.1.0` |
| `CPify V1.0.0 Release` | `V1.0.0` |

**Rules:**
- Start with `CPify`
- Include the version with `V` prefix
- End with `Release`
- Use spaces as separators

## Binary Asset Naming Convention

**Format:** `cpify-{os}-{architecture}[.extension]`

### Supported Operating Systems

| OS Identifier | Platform |
|---------------|----------|
| `linux` | Linux (all distributions) |
| `windows` | Windows 10/11 |
| `macos` | macOS (Intel & Apple Silicon) |

### Supported Architectures

| Architecture | Description |
|--------------|-------------|
| `x86_64` | 64-bit Intel/AMD (most common) |
| `aarch64` | 64-bit ARM (Apple M1/M2, ARM servers) |
| `i386` | 32-bit Intel/AMD (legacy) |
| `arm` | 32-bit ARM (Raspberry Pi, etc.) |

### Binary Naming Examples

| Filename | OS | Architecture |
|----------|-----|--------------|
| `cpify-linux-x86_64` | Linux | 64-bit Intel/AMD |
| `cpify-linux-aarch64` | Linux | 64-bit ARM |
| `cpify-windows-x86_64.exe` | Windows | 64-bit Intel/AMD |
| `cpify-macos-x86_64` | macOS | Intel |
| `cpify-macos-aarch64` | macOS | Apple Silicon |

**Rules:**
- Use lowercase for OS and architecture
- Use hyphens (`-`) as separators
- Include `.exe` extension for Windows binaries
- No extension needed for Linux/macOS binaries
- The updater performs case-insensitive matching

## Creating a Release

### Step-by-Step Process

1. **Update the version** in `meson.build`:
   ```meson
   project(
     'cpify',
     'c',
     version: '0.1.0',  # <- Update this
     ...
   )
   ```

2. **Build binaries** for each target platform

3. **Create a GitHub Release**:
   - Go to your repository → Releases → "Draft a new release"
   - **Tag:** `V0.1.0` (create new tag)
   - **Title:** `CPify V0.1.0 Release`
   - **Description:** Include changelog and release notes
   - **Assets:** Upload all binary files with correct naming

### Example Release Structure

```
Tag: V0.1.0
Title: CPify V0.1.0 Release
Description:
  ## What's New
  - Added gallery view layout
  - Improved video playback
  - Bug fixes

  ## Downloads
  Download the appropriate binary for your system below.

Assets:
  - cpify-linux-x86_64
  - cpify-linux-aarch64
  - cpify-windows-x86_64.exe
  - cpify-macos-x86_64
  - cpify-macos-aarch64
```

## How the Updater Works

1. **On startup** (after 3 second delay): Fetches latest release from GitHub API
2. **Version comparison**: Compares current app version with release tag
3. **If update available**: Shows dialog with release info
4. **User clicks Install**: Downloads appropriate binary to cache directory
5. **Completion**: User prompted to restart app

### API Endpoint

```
GET https://api.github.com/repos/dixonSolutions/CPify/releases/latest
```

### Binary Matching Logic

The updater searches release assets in this order:

1. **Exact match**: OS + Architecture (e.g., `cpify-linux-x86_64`)
2. **Fallback**: OS only (e.g., any file containing `linux`)

If no matching binary is found, the Install button is disabled with a tooltip explaining that users should build from source.

## Download Location

Downloaded binaries are stored in:

```
~/.cache/cpify/updates/
```

## Troubleshooting

### Update check fails silently
- Check internet connection
- GitHub API rate limits may apply (60 requests/hour for unauthenticated)

### No binary available for my system
- Build from source following `DEV_SETUP.md`
- Request a binary for your platform via GitHub Issues

### Version not detected as newer
- Ensure tag follows `V{VERSION}` format
- Verify version in `meson.build` matches expectations
