#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Flatpak launcher: the sandbox has its own XDG folders; the manifest exposes the program's own folders of the real ones, so the
# Flatpak, the AppImage and a native build share one config (profiles, calibrations) and one log folder.
export XDG_CONFIG_HOME="$HOME/.config"
export XDG_STATE_HOME="$HOME/.local/state"
exec /app/libexec/msfs-linux-companion "$@"
