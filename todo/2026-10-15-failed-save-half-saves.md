# A failed or cancelled PDF Save leaves the open document half-saved, and Cancel doesn't stop Save As

Priority: P0. Tags: `save`, `data-loss`.

- Done when: an injected write failure, and Cancel pressed mid-save while that button exists,
  each leave the open document with the same marks, page count and undo history as before. No
  file appears at a Save As target. The next good Save writes each mark once.
- Src: `MainWindow.cpp:3247-3268` returns on `wasCanceled`, but the `QtConcurrent::run` worker
  keeps going. `PdfAdapter.cpp:2596-2604` mutates the live editor; `:2626-2630` writes the
  Save As target directly. Inferred from the code, not reproduced, so no known-bug test yet:
  it needs a fault-injection seam in the writer first.
