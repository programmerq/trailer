#pragma once

#include "MyCard.h"

#include <QString>
#include <vector>

namespace trailer {

// Persists the user's "My Card(s)" to a TOML file under
// AppPaths::dataDir(). The settings file stays focused on UI
// preferences; personal data lives in its own file so it's easy to
// back up, sync, or redact separately.
//
// File layout (cards.toml):
//
//   active = 0                  # zero-based index into [cards]
//   [[cards]]
//   label = "Personal"
//   given_name = "Alice"
//   family_name = "Example"
//   full_name = ""              # optional override
//   email = "alice@example.com"
//   phone = "+1 555-0100"
//   organization = "Acme"
//   job_title = ""
//   address_line1 = "1 Example St"
//   address_line2 = ""
//   city = "Portland"
//   state = "OR"
//   postal_code = "97201"
//   country = "USA"
class CardStore {
  public:
    CardStore();                          // uses AppPaths::cardsFile()
    explicit CardStore(QString filePath); // for tests

    // Load from disk. Empty file / missing file is not an error; the
    // store starts with zero cards and setActiveIndex() = -1.
    void load();
    // Persist to disk. Creates the parent directory if needed.
    void save() const;

    const std::vector<MyCard> &cards() const { return m_cards; }
    int activeIndex() const { return m_activeIndex; }

    // Returns the currently active card (or a default-constructed
    // MyCard if there is none / index is out of range).
    MyCard activeCard() const;
    bool hasActive() const {
        return m_activeIndex >= 0 && m_activeIndex < static_cast<int>(m_cards.size());
    }

    void addCard(MyCard card);
    // Replace the card at `index`. No-op if out of range.
    void replaceCard(int index, MyCard card);
    // Remove the card at `index`; if the active index was pointing to
    // or past it, it is clamped back into range (or -1 if the list
    // becomes empty).
    void removeCard(int index);
    void setActiveIndex(int index);

    QString filePath() const { return m_filePath; }

  private:
    QString m_filePath;
    std::vector<MyCard> m_cards;
    int m_activeIndex = -1;
};

} // namespace trailer
