# MAKGrib — a modified version of XyGrib

MAKGrib is a fork of **XyGrib** (<https://github.com/opengribs/XyGrib>),
which is itself the successor of **zyGrib** by Jacques Zaninetti.

This is a **modified version**, not the original program. It is released
under the same licence as the original, the **GNU General Public License,
version 3 or later**. The full licence text is in [LICENSE](LICENSE).

## Copyright

- Copyright © 2026 MAKGrib contributors — the changes listed below
- Copyright © 2012–2019 OpenGribs contributors — XyGrib
- Copyright © 2008–2012 Jacques Zaninetti — zyGrib

The original copyright notices have been kept in every file that carries
them. MAKGrib is not endorsed by, affiliated with, or supported by
OpenGribs or the XyGrib authors; please do not report MAKGrib problems to
them.

## What was changed, and when

All changes were made in **September 2026**.

- **Renamed** to MAKGrib, version 1.0.0, with a new icon
  (`data/img/makgrib.svg`). The name and the mark are ours; the XyGrib
  name and logo are not used, which is what the GPL asks of a fork that
  is no longer the original program.
- **Virtual boat**: a route drawn on the map or entered by waypoints,
  with departure time and speed, and a table of the weather the boat
  meets at each forecast step (`VirtualBoat`, `DialogVirtualBoat`).
- **Several forecasts at once**: more than one GRIB open together,
  switched from a toolbar selector, plus a "download every model for this
  area" action (`MultiModelLoader`).
- **Direct download from NOAA NOMADS** (`NomadsLoader`) when the OpenGribs
  server refuses or is down, as it has been since 5 September 2026 — see
  <https://github.com/opengribs/XyGrib/issues/326>. Covers GFS and
  GFS-Wave, with the area and depth asked for.
- **Reader fix**: GRIB2 fixed-surface type 241 ("ordered sequence of
  data") is now understood, so the swell fields NOAA publishes are read
  instead of being dropped (`src/Grib2Record.cpp`).
- **Expired forecasts** are no longer shown as though they were current.
- **Tooltips** on every menu and toolbar action, and a Russian
  translation completed to 100 %.

### 1.1.0

- **Windows: downloads from NOAA now work.** Qt built for mingw links
  against an OpenSSL whose paths are all baked as Linux ones
  (`/usr/x86_64-w64-mingw32/sys-root/mingw/etc/pki/tls/cert.pem`,
  `.../lib/ossl-modules`), so on Windows it finds no root certificates at
  all and every https connection fails. The NOAA fetch on Windows now
  goes through **WinHTTP**, which uses the operating system's own TLS and
  certificate store (`src/WinHttpFetch.cpp`). macOS and Linux are
  unchanged and keep using Qt.
- **No request can hang the window any more**: the two requests to the
  OpenGribs server have timeouts, an unreachable server falls back to
  NOAA instead of waiting forever, and the encryption subsystem is
  brought up on a background thread at start-up.

## Third-party components

Unchanged from XyGrib and distributed under their own licences: g2clib
(NOAA, public domain), OpenJPEG (BSD-2-Clause), libnova (LGPL-2), PROJ
(MIT), libpng, zlib, bzip2, Qt 5 (LGPL-3), and the GSHHG shoreline data
(LGPL-3).

## Source code

The complete corresponding source for any binary we distribute is this
repository. Anyone who receives a MAKGrib binary is entitled to the
source under the GPL and may ask us for it.
