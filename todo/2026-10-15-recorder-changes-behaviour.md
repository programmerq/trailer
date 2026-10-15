# The compiled-out recorder still changes behaviour, and PR CI tests a different configuration from the one that ships

Priority: P2. Tags: `infra`.

- Done when: the no-op recorder facade doesn't evaluate its arguments (a macro or lambda); the
  Linux PR job builds the shipped configuration (recorder off); and either product code holds
  no recorder call sites (it attaches from outside, through an event filter and action
  signals) or a test proves the compiled-out build has no side effects.
- Src: The no-op `recordEvent` receives a fully built QJsonObject (`UxRecord.h:53`), so
  `addDocument` forces the deferred open via `pageCount()` (`MainWindow.cpp:5251-5254`). The
  Linux PR job configures `-DTRAILER_ENABLE_UX_RECORDER=ON` (`ci.yml:185`, `:322`). 26 recorder
  references sit in 9 product files. In recorder builds, "Hand Off to Preview" saves to the
  user's file on macOS (`UxTrailerHooks.cpp:287-293`): a dev tool must not write documents.
