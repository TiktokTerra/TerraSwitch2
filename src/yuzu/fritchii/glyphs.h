// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QByteArray>
#include <QColor>
#include <QPixmap>

namespace FritchiiUi {

/// Draws one of the launcher's bundled SVG icons with QPainter: paths (M/L/H/V/C/S/A/Z,
/// absolute or relative), rounded rects, circles, "currentColor" and the stroke attributes.
/// The bundled static Qt ships no QSvgPlugin, so reading an SVG with QImageReader renders
/// nothing and the launcher's icon-only buttons turn invisible; this covers everything those
/// icons use, with no new dependency.
[[nodiscard]] QPixmap RenderSvgGlyph(const QByteArray& svg, const QColor& color, int px);

} // namespace FritchiiUi
