---
name: decision-brief
description: Triage candidate questions against recorded objectives, self-decide everything an objective already derives, and escalate only genuine decisions to the owner as one-word-answerable table rows.
---

# Decision brief

Use this before sending the owner (programmerq) any set of questions. The owner
works async; a question that has an obvious best answer stalls the session for
hours for no information gain. This skill forces you to answer everything the
project's recorded objectives already decide, and to escalate only the genuine
forks — each in a form answerable with a single word.

Its hand-off-time companion is
[`surface-the-ask`](../surface-the-ask/SKILL.md): this skill governs questions
raised **mid-work**; `surface-the-ask` signals, at PR/report time, **what blocks
the merge**.

## Rationale (owner feedback)

Per the `proceed-on-clear-defaults` owner feedback (2026-07-10): "These aren't
questions that I'd expect have ambiguity and shouldn't come back." When options
have a clear best default — especially one you yourself recommend — take it,
state it in one line, and keep working. Reserve real questions for genuinely
destructive/irreversible forks or true 50/50 judgment calls. That is why Step 1
triage below is deliberately aggressive.

## Recorded objectives (the sources you triage against)

- `PHILOSOPHY.md` — hard constraints, adjudication model, arbiter/owner roles.
- `DESIGN.md` — the product spec and personas.
- `AGENTS.md` — the hard constraints and hard gates G1-G9.
- The interview / decision records in `docs/decision-records/` (accepted ones
  bind; proposed ones do not yet).

## Procedure

### Step 1 — TRIAGE (self-decide by default)
For each candidate question, check it against the recorded objectives above.
**If any objective derives the answer, SELF-DECIDE it.** Do not escalate it.
Present it instead as an **"FYI — decided, owner may veto"** row: state the
decision, the objective that derives it, and note the owner can veto. This is
information, not a question.

### Step 2 — ESCALATE (only genuine decisions)
Escalate only questions that **no recorded objective decides** — genuine forks
or true judgment calls. Format **each as one table row**, with columns:

| Question | Options | Concrete implications of each | Recommendation + default-if-no-reply |

Constraints:
- The question must be **answerable with ONE word** (typically the option name,
  or yes/no). If it can't be, split it until each part can.
- Always give a recommendation **and** the default you will take if the owner
  does not reply — so silence never blocks you.

### Step 3 — JUSTIFY THE ESCALATION
Every escalated row **must state which recorded objective fails to decide it** —
i.e. why it can't be self-derived. Add a "Why not self-derived (objective
checked)" column or a per-row note. If you can't name an objective that *should*
have decided it and didn't, re-check Step 1: it is probably self-decidable.

## Worked example

Escalation set for a hypothetical "add a Preferences → Appearance pane" change:

| Question | Options | Concrete implications | Recommendation + default-if-no-reply | Why not self-derived |
|---|---|---|---|---|
| *(FYI — decided, owner may veto)* Persist theme choice across relaunch? | yes / no | — | **Decided: yes.** G7 requires every Preferences control to persist across relaunch; never-worry-save reinforces it. Owner may veto. | Self-derived — AGENTS.md G7 + never-worry-save decide it; not a question. |
| Default theme on first run? | System / Light / Dark | *System* follows OS and matches platform-native feel; *Light*/*Dark* pin an appearance regardless of OS setting. | **Recommend: System.** Default-if-no-reply: **System.** | No recorded objective names a first-run theme default; DESIGN/PHILOSOPHY are silent on which of the three ships as default — genuine judgment call. |

Row 1 is self-decided (Step 1) and shown only as an FYI. Row 2 is a real
escalation (Step 2) with its non-derivability justified (Step 3).
