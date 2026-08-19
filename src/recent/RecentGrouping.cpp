#include "RecentGrouping.h"

#include <QCoreApplication>
#include <QMap>

namespace trailer {

namespace {

// Bucket keys, chosen so that the "N days ago" band's key IS N. That
// keeps the map's natural ascending order the same as the display order
// (Today -> Older) without a separate ordering table to keep in sync.
//
//   0     Today
//   1     Yesterday
//   2..6  "N days ago"
//   7     Last week   (7-13 days)
//   8     Last month  (14-30 days)
//   9     Older       (31+ days, or no usable timestamp)
//
// Range tried: a flat "This week" instead of per-day 2..6 keys read as
// less useful for the reported case (dozens of manuals opened across a
// working week, where "3 days ago" locates a file and "this week" does
// not). Symptom to change: if per-day sections make the menu feel
// choppy, collapse 2..6 to a single key here — the label switch and the
// menu builder both follow.
constexpr int kKeyToday = 0;
constexpr int kKeyYesterday = 1;
constexpr int kKeyLastWeek = 7;
constexpr int kKeyLastMonth = 8;
constexpr int kKeyOlder = 9;

// Upper bounds (inclusive) of the coarse bands, in whole local days.
constexpr qint64 kDaysAgoMax = 6;   // last day that gets its own "N days ago"
constexpr qint64 kLastWeekMax = 13; // last day that reads as "Last week"
constexpr qint64 kLastMonthMax = 30;

int bucketKey(const QDateTime &openedAt, const QDateTime &now) {
    if (!openedAt.isValid() || !now.isValid())
        return kKeyOlder;
    // Whole local calendar days between the two instants. daysTo on QDate
    // (not QDateTime) is what makes "23:00 yesterday" read as Yesterday
    // rather than as 2 hours ago.
    const qint64 days = openedAt.toLocalTime().date().daysTo(now.toLocalTime().date());
    if (days <= 0)
        return kKeyToday; // clamp: a clock skew into the future still reads Today
    if (days == 1)
        return kKeyYesterday;
    if (days <= kDaysAgoMax)
        return static_cast<int>(days);
    if (days <= kLastWeekMax)
        return kKeyLastWeek;
    if (days <= kLastMonthMax)
        return kKeyLastMonth;
    return kKeyOlder;
}

QString labelForKey(int key) {
    switch (key) {
    case kKeyToday:
        return QCoreApplication::translate("RecentGrouping", "Today");
    case kKeyYesterday:
        return QCoreApplication::translate("RecentGrouping", "Yesterday");
    case kKeyLastWeek:
        return QCoreApplication::translate("RecentGrouping", "Last week");
    case kKeyLastMonth:
        return QCoreApplication::translate("RecentGrouping", "Last month");
    case kKeyOlder:
        return QCoreApplication::translate("RecentGrouping", "Older");
    default:
        break;
    }
    // 2..6 — the key is the day count. %n drives the plural form, so
    // translations that inflect on it (and any future "1 day ago" use)
    // stay correct without a second string.
    return QCoreApplication::translate("RecentGrouping", "%n day(s) ago", nullptr, key);
}

} // namespace

QString recentGroupLabel(const QDateTime &openedAt, const QDateTime &now) {
    return labelForKey(bucketKey(openedAt, now));
}

QList<RecentGroup> groupRecentByAge(const QList<RecentEntry> &entries, const QDateTime &now) {
    // QMap keeps the buckets in ascending key order, which IS the display
    // order (see bucketKey). Bucketing rather than a single ordered walk
    // means a list that is not perfectly most-recent-first still produces
    // each label exactly once instead of repeating a section.
    QMap<int, QList<RecentEntry>> buckets;
    for (const RecentEntry &entry : entries)
        buckets[bucketKey(entry.openedAt, now)].append(entry);

    QList<RecentGroup> groups;
    groups.reserve(buckets.size());
    for (auto it = buckets.cbegin(); it != buckets.cend(); ++it)
        groups.append(RecentGroup{labelForKey(it.key()), it.value()});
    return groups;
}

} // namespace trailer
