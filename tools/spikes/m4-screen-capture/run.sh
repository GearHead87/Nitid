#!/usr/bin/env bash
# M4 (automatic part): is the xdg-desktop-portal ScreenCast interface available, and what does it offer?
# The real capture needs a KDE permission dialog, so it is checked by the user in OBS (or by spike S6).
set -uo pipefail
q(){ busctl --user get-property org.freedesktop.portal.Desktop /org/freedesktop/portal/desktop \
       org.freedesktop.portal.ScreenCast "$1" 2>&1; }
echo "ScreenCast version:        $(q version)"
echo "AvailableSourceTypes:      $(q AvailableSourceTypes)   (bitmask: 1 monitor, 2 window, 4 virtual)"
echo "AvailableCursorModes:      $(q AvailableCursorModes)   (bitmask: 1 hidden, 2 embedded, 4 metadata)"
echo "Portal packages:           $(pacman -Q xdg-desktop-portal xdg-desktop-portal-kde | tr '\n' ' ')"
echo "PipeWire:                  $(pw-cli info 0 2>/dev/null | sed -n 's/.*version: "\(.*\)"/\1/p')"
echo "Services:                  $(systemctl --user is-active pipewire wireplumber xdg-desktop-portal | tr '\n' ' ')"
