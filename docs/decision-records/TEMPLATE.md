# NNNN — <short decision title>

<!--
Copy this file to `NNNN-kebab-title.md`, take the next free number, and fill
every section. A record with an empty section is not ready to be adjudicated.
See PHILOSOPHY.md → "How design decisions get adjudicated" for the process
this template encodes (personas as unranked lenses, the admissible-objection
test, the arbiter, and the proposed → accepted → superseded lifecycle).
-->

- **Status:** proposed <!-- proposed | accepted | superseded-by NNNN -->
- **Arbiter:** <name — the single person who issues the verdict>
- **Date proposed:** <YYYY-MM-DD>
- **Date accepted / superseded:** <YYYY-MM-DD, or —>

## Context

<What decision is on the table, and why now. State the user-facing behaviour
in question. Cite the relevant DESIGN/PHILOSOPHY sections and any live code
`file:line`. If the record proposes changing current behaviour, say plainly
what ships today so the record can't be read as describing the present.>

## Options

<Lay out the concrete options — at least the ones a persona lens would raise.
One short paragraph each. Do not pre-pick; that's the arbiter's job below.>

## Personas debate

<Each persona is an unranked adversarial lens. Write each one's position on
the options — what this user would expect, be confused by, or fear. Omit a
lens only if it genuinely has no stake, and say so.>

- **Office non-technical user:** <position>
- **Older careful user:** <position>
- **Power migrator:** <position>
- **Occasional user:** <position>

## Admissible objections

<Only objections that name a user/persona, a step in a real flow, and the
failure that user would hit. List each with who raised it and the concrete
problem it identifies.>

- <objection — user, step, failure>

### Rejected as naked preference

<Objections that are taste, not a checkable problem, recorded here with the
reason they carry no weight. Keeping them visible prevents them being
re-raised as if new.>

- <preference> — rejected: states no concrete user, step, or failure.

## Checkable threshold this record would establish

<The pass/fail line this decision commits to, phrased so an agent or reviewer
can independently declare pass or fail. A number, a concrete behaviour, or a
named budget/spec row. If the record is still open, state the threshold each
option *would* establish.>

## Arbiter verdict + rationale

<The call, and why — which admissible objections drove it, which options were
rejected and on what grounds. Empty while status is `proposed`.>

## Evidence required to reopen

<Once accepted, what superseding evidence would justify reopening: a concrete,
checkable problem not on the table when this was accepted, plus owner sign-off.
Name the kind of evidence, not just "someone disagrees.">
