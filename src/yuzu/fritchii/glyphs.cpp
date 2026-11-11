// SPDX-License-Identifier: GPL-3.0-or-later

#include "yuzu/fritchii/glyphs.h"

#include <cmath>

#include <QApplication>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QStringList>
#include <QXmlStreamReader>

namespace FritchiiUi {
namespace {

constexpr qreal kPi = 3.14159265358979323846;

struct GlyphPoint {
    qreal x = 0.0;
    qreal y = 0.0;
};

/// The paint attributes an element inherits from <svg> and may override. Only what the
/// launcher's icons use; anything else keeps these defaults.
struct GlyphStyle {
    QString fill = QStringLiteral("none");
    QString stroke = QStringLiteral("currentColor");
    qreal width = 1.8;
    Qt::PenCapStyle cap = Qt::RoundCap;
    Qt::PenJoinStyle join = Qt::RoundJoin;

    [[nodiscard]] GlyphStyle Override(const QXmlStreamAttributes& attrs) const {
        GlyphStyle out = *this;
        if (attrs.hasAttribute(QStringLiteral("fill"))) {
            out.fill = attrs.value(QStringLiteral("fill")).toString();
        }
        if (attrs.hasAttribute(QStringLiteral("stroke"))) {
            out.stroke = attrs.value(QStringLiteral("stroke")).toString();
        }
        if (attrs.hasAttribute(QStringLiteral("stroke-width"))) {
            out.width = attrs.value(QStringLiteral("stroke-width")).toDouble();
        }
        const QStringView cap_kind = attrs.value(QStringLiteral("stroke-linecap"));
        if (cap_kind == QLatin1String("butt")) {
            out.cap = Qt::FlatCap;
        } else if (cap_kind == QLatin1String("square")) {
            out.cap = Qt::SquareCap;
        }
        const QStringView join_kind = attrs.value(QStringLiteral("stroke-linejoin"));
        if (join_kind == QLatin1String("miter")) {
            out.join = Qt::MiterJoin;
        } else if (join_kind == QLatin1String("bevel")) {
            out.join = Qt::BevelJoin;
        }
        return out;
    }
};

/// "currentColor", "#rrggbb" or "none" (transparent, the caller checks for the word).
[[nodiscard]] QColor ParseGlyphColor(const QString& name, const QColor& current) {
    if (name == QLatin1String("currentColor")) {
        return current;
    }
    const QColor parsed(name);
    return parsed.isValid() ? parsed : QColor(Qt::transparent);
}

/// An attribute as a number; 0 when missing or unparsable (all of these are optional).
[[nodiscard]] qreal AttrNumber(const QXmlStreamAttributes& attrs, QStringView name) {
    bool ok = false;
    const qreal value = attrs.value(name).toString().toDouble(&ok);
    return ok ? value : 0.0;
}

/// Walks a path's "d" string. SVG lets numbers, flags and commands run together with any
/// mix of separators ("M5 7h14", "a8 8 0 0 1-11.6 7.1", ".6.6 0 0 0-.9.5").
class GlyphPathData {
public:
    explicit GlyphPathData(const QString& data) : m_data(data) {}

    /// The next command letter, or a null QChar at the end or on a number.
    [[nodiscard]] QChar TakeCommand() {
        SkipSeparators();
        if (m_pos >= m_data.size() || !m_data[m_pos].isLetter()) {
            return {};
        }
        return m_data[m_pos++];
    }

    /// Whether the next token is a number (a repeat of the current command follows).
    [[nodiscard]] bool HasNumber() const {
        qsizetype pos = m_pos;
        while (pos < m_data.size() && m_data[pos].isSpace()) {
            ++pos;
        }
        if (pos >= m_data.size()) {
            return false;
        }
        const QChar ch = m_data[pos];
        return ch.isDigit() || ch == QLatin1Char('.') || ch == QLatin1Char('+') ||
               ch == QLatin1Char('-');
    }

    [[nodiscard]] qreal TakeNumber() {
        SkipSeparators();
        const qsizetype start = m_pos;
        if (m_pos < m_data.size() &&
            (m_data[m_pos] == QLatin1Char('+') || m_data[m_pos] == QLatin1Char('-'))) {
            ++m_pos;
        }
        while (m_pos < m_data.size() &&
               (m_data[m_pos].isDigit() || m_data[m_pos] == QLatin1Char('.'))) {
            ++m_pos;
        }
        return m_data.mid(start, m_pos - start).toDouble();
    }

    /// Arc flags are one digit, often run together with the next number ("0 1-11.6").
    [[nodiscard]] bool TakeFlag() {
        SkipSeparators();
        if (m_pos >= m_data.size() || !m_data[m_pos].isDigit()) {
            return false;
        }
        return m_data[m_pos++].digitValue() != 0;
    }

private:
    void SkipSeparators() {
        while (m_pos < m_data.size() && (m_data[m_pos].isSpace() || m_data[m_pos] == QLatin1Char(',') ||
                                         m_data[m_pos] == QLatin1Char(';'))) {
            ++m_pos;
        }
    }

    QString m_data;
    qsizetype m_pos = 0;
};

/// One (x, y) pair, absolute or relative to the current point.
[[nodiscard]] GlyphPoint ReadGlyphPair(GlyphPathData& cursor, bool relative, GlyphPoint origin) {
    GlyphPoint target{cursor.TakeNumber(), cursor.TakeNumber()};
    if (relative) {
        target.x += origin.x;
        target.y += origin.y;
    }
    return target;
}

/// One SVG elliptical arc (endpoint form) as cubic segments: the W3C implementation notes'
/// endpoint-to-center conversion (F.6.5), then one segment per 90 degrees at most.
void GlyphArcTo(QPainterPath& path, GlyphPoint current, qreal rx, qreal ry, qreal rotation_deg,
                bool large_arc, bool sweep, GlyphPoint target) {
    if (current.x == target.x && current.y == target.y) {
        return;
    }
    rx = std::abs(rx);
    ry = std::abs(ry);
    if (rx == 0.0 || ry == 0.0) {
        path.lineTo(target.x, target.y);
        return;
    }

    const qreal phi = rotation_deg * kPi / 180.0;
    const qreal cos_phi = std::cos(phi);
    const qreal sin_phi = std::sin(phi);
    const qreal half_dx = (current.x - target.x) / 2.0;
    const qreal half_dy = (current.y - target.y) / 2.0;
    const qreal x1p = cos_phi * half_dx + sin_phi * half_dy;
    const qreal y1p = -sin_phi * half_dx + cos_phi * half_dy;

    // Grow the radii when they cannot reach the endpoints.
    const qreal lambda = x1p * x1p / (rx * rx) + y1p * y1p / (ry * ry);
    if (lambda > 1.0) {
        const qreal grow = std::sqrt(lambda);
        rx *= grow;
        ry *= grow;
    }

    const qreal sign = large_arc != sweep ? 1.0 : -1.0;
    const qreal radicand =
        (rx * rx * ry * ry - rx * rx * y1p * y1p - ry * ry * x1p * x1p) /
        (rx * rx * y1p * y1p + ry * ry * x1p * x1p);
    const qreal root = radicand > 0.0 ? std::sqrt(radicand) : 0.0;
    const qreal cxp = sign * root * (rx * y1p / ry);
    const qreal cyp = sign * root * (-ry * x1p / rx);
    const qreal cx = cos_phi * cxp - sin_phi * cyp + (current.x + target.x) / 2.0;
    const qreal cy = sin_phi * cxp + cos_phi * cyp + (current.y + target.y) / 2.0;

    const qreal ux = (x1p - cxp) / rx;
    const qreal uy = (y1p - cyp) / ry;
    const qreal vx = (-x1p - cxp) / rx;
    const qreal vy = (-y1p - cyp) / ry;

    // Start angle and sweep, both in SVG's sweep direction (y grows down).
    const qreal theta = std::atan2(uy, ux);
    qreal delta = std::atan2(ux * vy - uy * vx, ux * vx + uy * vy);
    if (!sweep && delta > 0.0) {
        delta -= 2.0 * kPi;
    } else if (sweep && delta < 0.0) {
        delta += 2.0 * kPi;
    }

    // A point on the ellipse, rotated back into SVG coordinates.
    const auto point_at = [&](qreal angle) {
        const qreal ex = rx * std::cos(angle);
        const qreal ey = ry * std::sin(angle);
        return GlyphPoint{cx + cos_phi * ex - sin_phi * ey, cy + sin_phi * ex + cos_phi * ey};
    };

    const int segments = std::max(1, static_cast<int>(std::ceil(std::abs(delta) / (kPi / 2.0))));
    const qreal step = delta / segments;
    for (int i = 0; i < segments; ++i) {
        const qreal t1 = theta + i * step;
        const qreal t2 = t1 + step;
        const qreal reach = 4.0 / 3.0 * std::tan(step / 4.0);
        const GlyphPoint from = point_at(t1);
        const GlyphPoint to = point_at(t2);
        // The tangent at t is (-rx sin t, ry cos t), same rotation as the point.
        const qreal tangent1x = cos_phi * (-rx * std::sin(t1)) - sin_phi * (ry * std::cos(t1));
        const qreal tangent1y = sin_phi * (-rx * std::sin(t1)) + cos_phi * (ry * std::cos(t1));
        const qreal tangent2x = cos_phi * (-rx * std::sin(t2)) - sin_phi * (ry * std::cos(t2));
        const qreal tangent2y = sin_phi * (-rx * std::sin(t2)) + cos_phi * (ry * std::cos(t2));
        path.cubicTo(from.x + reach * tangent1x, from.y + reach * tangent1y, to.x - reach * tangent2x,
                     to.y - reach * tangent2y, to.x, to.y);
    }
}

/// SVG path data (M/L/H/V/C/S/A/Z, absolute or relative) as a QPainterPath. Command
/// arguments repeat while numbers keep coming; after a moveto they are implicit linetos.
[[nodiscard]] QPainterPath BuildGlyphPath(const QString& data) {
    QPainterPath path;
    GlyphPathData cursor(data);
    GlyphPoint current{0.0, 0.0};
    GlyphPoint subpath_start{0.0, 0.0};
    GlyphPoint last_control{0.0, 0.0};
    bool has_control = false;

    QChar cmd = cursor.TakeCommand();
    while (!cmd.isNull()) {
        const bool relative = cmd.isLower();
        switch (cmd.toUpper().unicode()) {
        case 'M': {
            const GlyphPoint target = ReadGlyphPair(cursor, relative, current);
            path.moveTo(target.x, target.y);
            current = target;
            subpath_start = target;
            has_control = false;
            cmd = relative ? QLatin1Char('l') : QLatin1Char('L');
            break;
        }
        case 'L': {
            const GlyphPoint target = ReadGlyphPair(cursor, relative, current);
            path.lineTo(target.x, target.y);
            current = target;
            has_control = false;
            break;
        }
        case 'H': {
            const qreal x = cursor.TakeNumber() + (relative ? current.x : 0.0);
            path.lineTo(x, current.y);
            current.x = x;
            has_control = false;
            break;
        }
        case 'V': {
            const qreal y = cursor.TakeNumber() + (relative ? current.y : 0.0);
            path.lineTo(current.x, y);
            current.y = y;
            has_control = false;
            break;
        }
        case 'C': {
            const GlyphPoint c1 = ReadGlyphPair(cursor, relative, current);
            const GlyphPoint c2 = ReadGlyphPair(cursor, relative, current);
            const GlyphPoint target = ReadGlyphPair(cursor, relative, current);
            path.cubicTo(c1.x, c1.y, c2.x, c2.y, target.x, target.y);
            current = target;
            last_control = c2;
            has_control = true;
            break;
        }
        case 'S': {
            // The first control point mirrors the previous curve's; without one, the start.
            const GlyphPoint c1 = has_control
                                      ? GlyphPoint{2 * current.x - last_control.x,
                                                   2 * current.y - last_control.y}
                                      : current;
            const GlyphPoint c2 = ReadGlyphPair(cursor, relative, current);
            const GlyphPoint target = ReadGlyphPair(cursor, relative, current);
            path.cubicTo(c1.x, c1.y, c2.x, c2.y, target.x, target.y);
            current = target;
            last_control = c2;
            has_control = true;
            break;
        }
        case 'A': {
            const qreal arc_rx = cursor.TakeNumber();
            const qreal arc_ry = cursor.TakeNumber();
            const qreal arc_rotation = cursor.TakeNumber();
            const bool large_arc = cursor.TakeFlag();
            const bool sweep = cursor.TakeFlag();
            const GlyphPoint target = ReadGlyphPair(cursor, relative, current);
            GlyphArcTo(path, current, arc_rx, arc_ry, arc_rotation, large_arc, sweep, target);
            current = target;
            has_control = false;
            break;
        }
        case 'Z': {
            path.closeSubpath();
            current = subpath_start;
            has_control = false;
            break;
        }
        default:
            return path;
        }
        if (!cursor.HasNumber()) {
            cmd = cursor.TakeCommand();
        }
    }
    return path;
}

} // namespace

QPixmap RenderSvgGlyph(const QByteArray& svg, const QColor& color, int px) {
    const qreal ratio = qApp->devicePixelRatio();
    QPixmap pixmap(QSize(px, px) * ratio);
    pixmap.setDevicePixelRatio(ratio);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);

    QXmlStreamReader xml(svg);
    QRectF view(0.0, 0.0, px, px);
    GlyphStyle root_style;
    bool svg_seen = false;
    while (!xml.atEnd()) {
        xml.readNext();
        if (xml.hasError()) {
            return pixmap;
        }
        if (xml.tokenType() != QXmlStreamReader::StartElement) {
            continue;
        }
        const QXmlStreamAttributes attrs = xml.attributes();

        if (xml.name() == QLatin1String("svg")) {
            svg_seen = true;
            root_style = GlyphStyle{}.Override(attrs);
            const QStringList box = attrs.value(QStringLiteral("viewBox")).toString()
                                        .simplified()
                                        .split(QLatin1Char(' '));
            if (box.size() == 4 && box[2].toDouble() > 0.0 && box[3].toDouble() > 0.0) {
                view = QRectF(box[0].toDouble(), box[1].toDouble(), box[2].toDouble(),
                              box[3].toDouble());
            } else {
                const qreal w = AttrNumber(attrs, QStringLiteral("width"));
                const qreal h = AttrNumber(attrs, QStringLiteral("height"));
                if (w > 0.0 && h > 0.0) {
                    view = QRectF(0.0, 0.0, w, h);
                }
            }
            painter.translate(-view.left(), -view.top());
            painter.scale(px / view.width(), px / view.height());
            continue;
        }
        if (!svg_seen) {
            continue;
        }

        const GlyphStyle style = root_style.Override(attrs);
        QPainterPath shape;
        if (xml.name() == QLatin1String("path")) {
            shape = BuildGlyphPath(attrs.value(QStringLiteral("d")).toString());
        } else if (xml.name() == QLatin1String("rect")) {
            const qreal x = AttrNumber(attrs, QStringLiteral("x"));
            const qreal y = AttrNumber(attrs, QStringLiteral("y"));
            const qreal w = AttrNumber(attrs, QStringLiteral("width"));
            const qreal h = AttrNumber(attrs, QStringLiteral("height"));
            const qreal rx = AttrNumber(attrs, QStringLiteral("rx"));
            const qreal ry = AttrNumber(attrs, QStringLiteral("ry"));
            if (rx > 0.0 || ry > 0.0) {
                shape.addRoundedRect(QRectF(x, y, w, h), rx, ry);
            } else {
                shape.addRect(QRectF(x, y, w, h));
            }
        } else if (xml.name() == QLatin1String("circle")) {
            const qreal cx = AttrNumber(attrs, QStringLiteral("cx"));
            const qreal cy = AttrNumber(attrs, QStringLiteral("cy"));
            const qreal r = AttrNumber(attrs, QStringLiteral("r"));
            shape.addEllipse(QRectF(cx - r, cy - r, 2 * r, 2 * r));
        } else if (xml.name() == QLatin1String("line")) {
            shape.moveTo(AttrNumber(attrs, QStringLiteral("x1")),
                         AttrNumber(attrs, QStringLiteral("y1")));
            shape.lineTo(AttrNumber(attrs, QStringLiteral("x2")),
                         AttrNumber(attrs, QStringLiteral("y2")));
        } else {
            continue;
        }
        shape.setFillRule(Qt::WindingFill);

        painter.setBrush(style.fill == QLatin1String("none")
                             ? Qt::NoBrush
                             : QBrush(ParseGlyphColor(style.fill, color)));
        if (style.stroke != QLatin1String("none") && style.width > 0.0) {
            QPen pen(ParseGlyphColor(style.stroke, color), style.width);
            pen.setCapStyle(style.cap);
            pen.setJoinStyle(style.join);
            painter.setPen(pen);
        } else {
            painter.setPen(Qt::NoPen);
        }
        painter.drawPath(shape);
    }
    return pixmap;
}

} // namespace FritchiiUi
