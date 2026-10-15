# Trailer can't be built without ONNX Runtime

Priority: P2. Tags: `packaging`.

- Done when: configuring with ML off and no ONNX Runtime present builds the app and passes the
  unit tests, and the ML commands are absent from the UI.
- Src: `CMakeLists.txt:239`; `cmake/OnnxRuntime.cmake:57` and `:78-115`.
