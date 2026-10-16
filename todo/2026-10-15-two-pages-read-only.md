# Two Pages is read-only (no markup, text selection or Find) and hides the markup toolbar

Priority: P1. Tags: `text`, `annotations`.

- Done when, in Two Pages at dpr 1 and 2:
  - every markup tool enabled in Single is enabled, and each mark lands on the correct page of
    the spread (round-trip checked in Single);
  - selecting text on either page copies only that page's text;
  - Find highlights each match on its page and scrolls that spread into view;
  - switching among Single, Continuous and Two Pages never changes any toolbar's visibility or
    the document view's y-position;
  - the Read-only badge and the "switch to Single" tooltip are gone.
- Test: `test_known_bugs_window` (this entry; toolbar visibility). Not checked: markup on the
  spread, text selection, Find, and the Read-only badge and its tooltip.
- Src: `MainWindow.cpp:862-885`, `:4655-4693`; `TwoPageView` has no mouse or key
  handlers. Decided: keep Two Pages and fix it on the new page surface rather than remove it.
