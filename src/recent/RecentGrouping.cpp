#include "RecentGrouping.h"

#include <QCoreApplication>
#include <QMap>

namespace trailer {

namespace {

// Bucket keys in display order. The ladder is Apple's (Finder / Photos):
// other platforms group differently (Explorer: "Last week / Earlier this
// month"; Chrome: per-date), so this is one convention followed, not a
// cross-platform unanimity:
//
//   0  Today
//   1  Yesterday
//   2  Previous 7 Days   (2-7 whole local days ago)
//   3  Previous 30 Days  (8-30)
//   4  Older             (31+, or no usable timestamp)
//
// Range tried: per-day "N days ago" sections for days 2-6 (first draft,
// owner's literal wording) produced up to five single-item sections on a
// normal working week — noise, and no platform precedent. The owner
// delegated the call to established convention (2026-08-28); Apple's
// ladder is the one Trailer follows.
// Symptom to change: if "Previous 7 Days" routinely holds so many entries
// that locating a file means reading the whole section, split it.
constexpr int kKeyToday = 0;
constexpr int kKeyYesterday = 1;
constexpr int kKeyPrev7 = 2;
constexpr int kKeyPrev30 = 3;
constexpr int kKeyOlder = 4;

// Upper bounds (inclusive) of the coarse bands, in whole local days.
constexpr qint64 kPrev7Max = 7;
constexpr qint64 kPrev30Max = 30;

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
    if (days <= kPrev7Max)
        return kKeyPrev7;
    if (days <= kPrev30Max)
        return kKeyPrev30;
    return kKeyOlder;
}

QString labelForKey(int key) {
    switch (key) {
    case kKeyToday:
        return QCoreApplication::translate("RecentGrouping", "Today");
    case kKeyYesterday:
        return QCoreApplication::translate("RecentGrouping", "Yesterday");
    case kKeyPrev7:
        return QCoreApplication::translate("RecentGrouping", "Previous 7 Days");
    case kKeyPrev30:
        return QCoreApplication::translate("RecentGrouping", "Previous 30 Days");
    case kKeyOlder:
    default:
        return QCoreApplication::translate("RecentGrouping", "Older");
    }
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
