# Problem reports can't tell invisible OCR text from visible text

Priority: P3. Tags: `ocr`.

- Done when: given an ocrmypdf-style page (a scan with render-mode-3 text) and a born-digital
  page, the feedback report states two different, correct text states without comparing
  pixels.
- Src: `FeedbackReport.cpp:260-270`.
