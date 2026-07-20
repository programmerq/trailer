#pragma once

#include "Annotation.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QList>

namespace trailer {

// Lossless Annotation <-> JSON mapping used by the kept-windows (⌥⌘Q)
// draft store to persist a PDF's UNSAVED annotations across a relaunch as
// individually editable objects (not flattened into page content). Every
// field of Annotation round-trips: id, page, type, bounds, points,
// pressures, quads, text, imagePath, and the full AnnotationStyle
// (stroke/fill colours, stroke width, font, dash, weight, zoom). Colours
// are stored as [r,g,b,a] arrays so the exact ARGB survives.
//
// See docs/decision-records/2026-07-16-quit-and-keep-windows.md — the PDF
// annotation-persistence refinement.
QJsonObject annotationToJson(const Annotation &a);
Annotation annotationFromJson(const QJsonObject &obj);

QJsonArray annotationsToJsonArray(const QList<Annotation> &annotations);
QList<Annotation> annotationsFromJsonArray(const QJsonArray &array);

} // namespace trailer
