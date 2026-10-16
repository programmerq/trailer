# Window and selection screenshots don't work on Windows or Linux

Priority: P1. Tags: `screenshot`, `platform`.

- Done when: on Windows 11 and on Linux (GNOME Wayland through the screenshot portal, and X11),
  From Window… and From Selection… capture into a new untitled document at the screen's native
  pixels. This is checked on a real desktop at dpr 1 and 2. The "isn't available on this
  platform yet" tooltips are gone.
- Src: `Application.cpp:1361-1368`.
