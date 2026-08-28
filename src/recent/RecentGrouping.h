#pragma once

#include "RecentFiles.h"

#include <QDateTime>
#include <QList>
#include <QString>

namespace trailer {

// One recency bucket of the Open Recent menu — a section label plus the
// entries that fall in it, in the order they were handed in (the
// RecentFiles model keeps that most-recent-first).
struct RecentGroup {
    QString label;
    QList<RecentEntry> entries;
};

// Buckets `entries` by age of RecentEntry::openedAt relative to `now`,
// so File > Open Recent can render dated sections instead of one long
// undifferentiated list (2026-08-19 owner feedback).
//
// The ladder, in emitted order, is the platform-conventional coarse one
// (Finder / Explorer / Chrome history):
//
//     Today · Yesterday · Previous 7 Days (2-7) · Previous 30 Days (8-30)
//         · Older (31+)
//
// Age is measured in whole LOCAL calendar days, not elapsed hours:
// openedAt is stored UTC (RecentFiles::add), and a file opened at 23:00
// yesterday must read "Yesterday" at 01:00 today rather than "Today"
// two hours later. Groups with no entries are omitted entirely, so the
// caller never renders an empty section header. An entry with an
// invalid openedAt (a recent.json written before the field existed)
// sorts into "Older" — the one bucket that makes no claim the timestamp
// can contradict.
//
// The day boundaries are a display grouping, not a retention policy:
// nothing here drops or expires an entry.
QList<RecentGroup> groupRecentByAge(const QList<RecentEntry> &entries, const QDateTime &now);

// The section label a single entry's age maps to. Exposed for tests and
// for any surface that needs one entry's bucket without grouping a list.
QString recentGroupLabel(const QDateTime &openedAt, const QDateTime &now);

} // namespace trailer
