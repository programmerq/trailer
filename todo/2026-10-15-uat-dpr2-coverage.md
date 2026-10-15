# Most UAT binaries never run at dpr 2

Priority: P2. Tags: `infra`.

- Done when: every UAT binary runs at dpr 1 and 2 through `trailer_register_uat_dpr_matrix`,
  and they are green at release.
- Src: only 5 of 35 use it.
