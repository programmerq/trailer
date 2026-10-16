# Updates install only on macOS

Priority: P2. Tags: `packaging`.

- Done when: on Windows (MSI) and Linux (DEB, RPM, tarball), Check for Updates downloads the
  next version, verifies its ed25519 signature, and either starts the platform installer or
  states the one manual step.
- Src: `UpdateManager.cpp:211` (`#ifdef Q_OS_MACOS`).
