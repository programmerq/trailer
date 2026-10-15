# Nothing stops new network code from landing

Priority: P3. Tags: `infra`.

- Done when: a CI check fails if a network class (QNetworkAccessManager, QTcpSocket,
  QUdpSocket, QNetworkRequest, …) appears outside `src/ml/ModelDownloader.*`, `src/update/`
  and their tests.
- Src: AGENTS.md, Hard constraints.
