#include "CardStore.h"

#include "settings/AppPaths.h"

#include <QFile>
#include <QFileInfo>

#include <toml++/toml.h>

#include <algorithm>
#include <sstream>

namespace trailer {

namespace {

std::string toStd(const QString &s) {
    return s.toStdString();
}
QString fromStd(std::string_view s) {
    return QString::fromUtf8(s.data(), static_cast<int>(s.size()));
}

// Pull a string field out of a toml table, returning an empty QString
// if the key is missing or the wrong type. Accepts any toml::node
// with a string value; anything else (including missing keys) is an
// empty string rather than a failure — the loader is lenient so old
// files keep working when we add fields.
QString tomlString(const toml::table &tbl, const char *key) {
    if (auto v = tbl[key].value<std::string>())
        return fromStd(*v);
    return {};
}

} // namespace

CardStore::CardStore() : CardStore(AppPaths::cardsFile()) {}

CardStore::CardStore(QString filePath) : m_filePath(std::move(filePath)) {}

void CardStore::load() {
    m_cards.clear();
    m_activeIndex = -1;

    QFile file(m_filePath);
    if (!file.exists() || !file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return;
    }
    const std::string content = file.readAll().toStdString();
    file.close();

    toml::table tbl;
    try {
        tbl = toml::parse(content);
    } catch (const toml::parse_error &) {
        return;
    }

    if (auto v = tbl["active"].value<int64_t>()) {
        m_activeIndex = static_cast<int>(*v);
    }

    if (auto *arr = tbl["cards"].as_array()) {
        for (auto &node : *arr) {
            auto *t = node.as_table();
            if (!t)
                continue;
            MyCard c;
            c.label = tomlString(*t, "label");
            c.givenName = tomlString(*t, "given_name");
            c.familyName = tomlString(*t, "family_name");
            c.fullName = tomlString(*t, "full_name");
            c.email = tomlString(*t, "email");
            c.phone = tomlString(*t, "phone");
            c.organization = tomlString(*t, "organization");
            c.jobTitle = tomlString(*t, "job_title");
            c.addressLine1 = tomlString(*t, "address_line1");
            c.addressLine2 = tomlString(*t, "address_line2");
            c.city = tomlString(*t, "city");
            c.state = tomlString(*t, "state");
            c.postalCode = tomlString(*t, "postal_code");
            c.country = tomlString(*t, "country");
            m_cards.push_back(std::move(c));
        }
    }

    // Clamp active index back into range.
    if (m_activeIndex >= static_cast<int>(m_cards.size())) {
        m_activeIndex = m_cards.empty() ? -1 : 0;
    }
    if (m_activeIndex < -1)
        m_activeIndex = -1;
}

void CardStore::save() const {
    toml::array arr;
    for (const auto &c : m_cards) {
        toml::table t;
        t.insert_or_assign("label", toStd(c.label));
        t.insert_or_assign("given_name", toStd(c.givenName));
        t.insert_or_assign("family_name", toStd(c.familyName));
        t.insert_or_assign("full_name", toStd(c.fullName));
        t.insert_or_assign("email", toStd(c.email));
        t.insert_or_assign("phone", toStd(c.phone));
        t.insert_or_assign("organization", toStd(c.organization));
        t.insert_or_assign("job_title", toStd(c.jobTitle));
        t.insert_or_assign("address_line1", toStd(c.addressLine1));
        t.insert_or_assign("address_line2", toStd(c.addressLine2));
        t.insert_or_assign("city", toStd(c.city));
        t.insert_or_assign("state", toStd(c.state));
        t.insert_or_assign("postal_code", toStd(c.postalCode));
        t.insert_or_assign("country", toStd(c.country));
        arr.push_back(std::move(t));
    }

    toml::table tbl;
    tbl.insert_or_assign("active", static_cast<int64_t>(m_activeIndex));
    tbl.insert_or_assign("cards", std::move(arr));

    AppPaths::ensureDirExists(QFileInfo(m_filePath).absolutePath());

    QFile file(m_filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        return;
    }
    std::ostringstream out;
    out << tbl;
    const std::string payload = out.str();
    file.write(payload.data(), static_cast<qint64>(payload.size()));
    file.close();
}

MyCard CardStore::activeCard() const {
    if (!hasActive())
        return {};
    return m_cards[static_cast<size_t>(m_activeIndex)];
}

void CardStore::addCard(MyCard card) {
    m_cards.push_back(std::move(card));
    if (m_activeIndex < 0) {
        m_activeIndex = static_cast<int>(m_cards.size()) - 1;
    }
}

void CardStore::replaceCard(int index, MyCard card) {
    if (index < 0 || index >= static_cast<int>(m_cards.size()))
        return;
    m_cards[static_cast<size_t>(index)] = std::move(card);
}

void CardStore::removeCard(int index) {
    if (index < 0 || index >= static_cast<int>(m_cards.size()))
        return;
    m_cards.erase(m_cards.begin() + index);
    if (m_cards.empty()) {
        m_activeIndex = -1;
    } else if (m_activeIndex >= static_cast<int>(m_cards.size())) {
        m_activeIndex = static_cast<int>(m_cards.size()) - 1;
    }
}

void CardStore::setActiveIndex(int index) {
    if (index < -1 || index >= static_cast<int>(m_cards.size())) {
        return;
    }
    m_activeIndex = index;
}

} // namespace trailer
