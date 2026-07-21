#include "AnnotationJson.h"

namespace trailer {

namespace {

QJsonArray colorToJson(const QColor &c) {
    // [r,g,b,a] — the exact 8-bit ARGB channels so a recoloured markup
    // survives byte-for-byte (QColor::name() would drop the alpha).
    return QJsonArray{c.red(), c.green(), c.blue(), c.alpha()};
}

QColor colorFromJson(const QJsonValue &v, const QColor &fallback) {
    const QJsonArray a = v.toArray();
    if (a.size() != 4)
        return fallback;
    return QColor(a.at(0).toInt(), a.at(1).toInt(), a.at(2).toInt(), a.at(3).toInt());
}

QJsonObject rectToJson(const QRectF &r) {
    return QJsonObject{{QStringLiteral("x"), r.x()},
                       {QStringLiteral("y"), r.y()},
                       {QStringLiteral("w"), r.width()},
                       {QStringLiteral("h"), r.height()}};
}

QRectF rectFromJson(const QJsonObject &o) {
    return QRectF(o.value(QStringLiteral("x")).toDouble(), o.value(QStringLiteral("y")).toDouble(),
                  o.value(QStringLiteral("w")).toDouble(), o.value(QStringLiteral("h")).toDouble());
}

} // namespace

QJsonObject annotationToJson(const Annotation &a) {
    QJsonObject obj;
    obj[QStringLiteral("id")] = a.id;
    obj[QStringLiteral("page")] = a.page;
    obj[QStringLiteral("type")] = static_cast<int>(a.type);
    obj[QStringLiteral("bounds")] = rectToJson(a.bounds);

    QJsonArray points;
    for (const QPointF &p : a.points)
        points.append(QJsonArray{p.x(), p.y()});
    obj[QStringLiteral("points")] = points;

    QJsonArray pressures;
    for (float f : a.pressures)
        pressures.append(static_cast<double>(f));
    obj[QStringLiteral("pressures")] = pressures;

    QJsonArray quads;
    for (const QRectF &q : a.quads)
        quads.append(rectToJson(q));
    obj[QStringLiteral("quads")] = quads;

    obj[QStringLiteral("text")] = a.text;
    obj[QStringLiteral("imagePath")] = a.imagePath;

    QJsonObject style;
    style[QStringLiteral("stroke")] = colorToJson(a.style.stroke);
    style[QStringLiteral("fill")] = colorToJson(a.style.fill);
    style[QStringLiteral("strokeWidth")] = a.style.strokeWidth;
    style[QStringLiteral("fontPointSize")] = a.style.fontPointSize;
    style[QStringLiteral("dash")] = static_cast<int>(a.style.dash);
    style[QStringLiteral("fontFamily")] = a.style.fontFamily;
    style[QStringLiteral("fontWeight")] = a.style.fontWeight;
    style[QStringLiteral("zoomFactor")] = a.style.zoomFactor;
    obj[QStringLiteral("style")] = style;

    return obj;
}

Annotation annotationFromJson(const QJsonObject &obj) {
    Annotation a;
    a.id = obj.value(QStringLiteral("id")).toInt();
    a.page = obj.value(QStringLiteral("page")).toInt();
    a.type = static_cast<AnnotationType>(obj.value(QStringLiteral("type")).toInt());
    a.bounds = rectFromJson(obj.value(QStringLiteral("bounds")).toObject());

    for (const QJsonValue &pv : obj.value(QStringLiteral("points")).toArray()) {
        const QJsonArray pa = pv.toArray();
        if (pa.size() == 2)
            a.points.emplace_back(pa.at(0).toDouble(), pa.at(1).toDouble());
    }
    for (const QJsonValue &fv : obj.value(QStringLiteral("pressures")).toArray())
        a.pressures.push_back(static_cast<float>(fv.toDouble()));
    for (const QJsonValue &qv : obj.value(QStringLiteral("quads")).toArray())
        a.quads.push_back(rectFromJson(qv.toObject()));

    a.text = obj.value(QStringLiteral("text")).toString();
    a.imagePath = obj.value(QStringLiteral("imagePath")).toString();

    const QJsonObject style = obj.value(QStringLiteral("style")).toObject();
    const AnnotationStyle def; // channel defaults for any missing field
    a.style.stroke = colorFromJson(style.value(QStringLiteral("stroke")), def.stroke);
    a.style.fill = colorFromJson(style.value(QStringLiteral("fill")), def.fill);
    a.style.strokeWidth = style.value(QStringLiteral("strokeWidth")).toDouble(def.strokeWidth);
    a.style.fontPointSize = style.value(QStringLiteral("fontPointSize")).toInt(def.fontPointSize);
    a.style.dash = static_cast<DashStyle>(
        style.value(QStringLiteral("dash")).toInt(static_cast<int>(def.dash)));
    a.style.fontFamily = style.value(QStringLiteral("fontFamily")).toString();
    a.style.fontWeight = style.value(QStringLiteral("fontWeight")).toInt(def.fontWeight);
    a.style.zoomFactor = style.value(QStringLiteral("zoomFactor")).toDouble(def.zoomFactor);

    return a;
}

QJsonArray annotationsToJsonArray(const QList<Annotation> &annotations) {
    QJsonArray out;
    for (const Annotation &a : annotations)
        out.append(annotationToJson(a));
    return out;
}

QList<Annotation> annotationsFromJsonArray(const QJsonArray &array) {
    QList<Annotation> out;
    out.reserve(array.size());
    for (const QJsonValue &v : array)
        out.append(annotationFromJson(v.toObject()));
    return out;
}

} // namespace trailer
