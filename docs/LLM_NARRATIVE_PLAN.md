# LLM villager narrative implementation plan

Build short, persistent villager stories that unfold during ordinary conversations. Gameplay and event narrative always takes precedence. The first playable milestone is one villager telling a three-beat story across separate encounters, remembering its progress across restarts, and yielding to the base game whenever required.

This is the implementation handoff for Sol. Start with Stage 0 and work through the gates below. Planning is complete; implementation has not started. Initial target: ACNH 3.0.3, one player, one save, one supported dialogue language, and single-player sessions. Confirm the installed game version before adding hooks; do not assume addresses apply to another build.

## Product scope

The story structure is introduction, development, and payoff. Suitable premises include a small ambition, an unusual opinion, or a harmless personal dilemma. For example, a villager experiments with a personal motto, revisits their first attempt, and arrives at an amusing conclusion. This should not invoke or alter the game's actual catchphrase mechanics.

The first implementation advances through returning conversations. Choices and remembered player responses are a subsequent enhancement. A story must work without choices and must never invent a response the player did not give.

The LLM proposes an outline and writes dialogue. Native code owns eligibility, pacing, persistence, and stage transitions. Use existing presentation where verified. The initial scope excludes item rewards, inventory changes, furniture placement, NPC movement, schedules, generated graphics, custom voices, multiplayer, and autonomous NPC agents. Dialogue must not promise unsupported actions or assert that unobserved world events occurred.

## Required behaviour

- Native gameplay and event narrative wins whenever pending, even if no message window is visible.
- Unknown eligibility means native behaviour. An ordinary small-talk message alone is insufficient proof that no gameplay narrative is pending.
- Native conversation selection runs before replacement. Do not bypass its side effects, event checks, or required branches.
- Recheck eligibility and conversation identity immediately before delivery. Discard stale generation results after a save, villager, scene, or story revision changes.
- Never wait for inference, network access, or disk operations inside a game hook. If content is unavailable, retain the original conversation.
- Only delivered content enters memory. Preparing a response does not advance a story.
- Keep narrative state outside the native save. Disabling the feature restores ordinary gameplay.
- Start disabled and enable only on a verified supported build. Unknown builds and unsupported sessions retain native behaviour.

## Existing foundations and gaps

| Foundation | Useful starting point | What still needs investigation |
| --- | --- | --- |
| [Message page hooks](../games/acnh/pointer.cpp) | Detect active message pages and preserve input behaviour | Speaker identity, semantic conversation category, insertion point, and completion signal |
| [Message tooling](../tools/msg_rewrap.py) | MSBT string handling, control tags, and font-based wrapping | Safe runtime text ownership, limits, and compatibility with the live renderer |
| [Native feature framework](NATIVE_FEATURES.md) | Feature registration, configuration, module hooks, resource transforms | Narrative-specific callbacks and integration |
| [Version registry](../games/acnh/versions.txt) and [3.0.3 hooks](../games/acnh/hooks-3.0.3.txt) | Build-specific integration | Fingerprints for newly discovered hooks |
| [Existing play harness](../tools/play/) | Reproducible input and regression patterns | Narrative scenarios and observable outcomes |

Active UI detection is not a pending-event detector. No semantic narrative scheduler or dynamic conversation insertion API has yet been established. Resource transforms run synchronously and can hold filesystem locks; they are not an inference location. Replacing an archive globally also cannot enforce per-conversation priority reliably.

## Stage 0 Establish observation and priority detection

**Outcome:** a trace explains why each observed interaction would be eligible or blocked, without replacing any dialogue.

1. Inspect workspace instructions and current changes; identify the actual installed build and established build/run commands. Use an isolated save copy for experiments. Keep dump-derived material and traces in ignored private storage.
2. Trace the path from initiating a villager conversation through native branch selection to message resolution and display. Record stable speaker identity, message or flow identifiers, conversation lifetime, and available event-state signals.
3. Find where the game decides that required dialogue or an event is pending. Investigate both the current NPC's interaction and broader player/island obligations. Define exactly which pending states are covered, with evidence for each.
4. Implement an observation-only eligibility result with three states: eligible, blocked, unknown. Include compact reason codes and context revisions in debug traces.
5. Exercise ordinary chatter alongside available requests, event dialogue, tutorials/progression, menus, scene changes, and session changes. Identify missing scenarios explicitly.

**Gate:** distinguish verified small-talk opportunities from protected interactions, and document how pending gameplay/event narrative is detected. If broader pending state cannot be established, retain unknown and keep generated dialogue disabled in those contexts. Continue hook research and offline controller work; do not quietly weaken the user's priority rule.

**Deliverables:** observation hooks, a private trace sample, and concise findings covering addresses/fingerprints, context fields, evidence, and unresolved blockers. Do not publish extracted game text or generated game code.

## Stage 1 Deliver one fixed line safely

**Outcome:** one eligible villager interaction displays a short authored test line in the normal dialogue presentation.

1. Find the narrowest substitution point after native branch selection and before text layout. Scope it to the verified NPC and eligible flow; retain the original control flow and necessary tags.
2. Establish encoding, allocation, buffer ownership, lifetime, wrapping, and page limits. Treat inserted text as plain text; do not accept arbitrary game control codes.
3. Add fingerprint checks and a debug toggle. Chain existing hooks correctly. Update instruction hook lists and regenerate/rebuild when required; dispatch wrappers alone do not intercept direct generated calls.
4. Establish a delivery acknowledgement and interruption signal. Recheck eligibility before replacement. Once a page has begun, use a verified native cancellation or completion boundary rather than tearing down the UI arbitrarily.

**Gate:** the fixed line appears with correct input and layout; native protected interactions remain unchanged; typing, page advance, cancellation, and repeated interactions behave correctly. Disabling the feature restores the original line. Missing fingerprints or unknown state prevents insertion.

Do not add an LLM dependency until this gate passes. An external overlay can aid debugging but does not satisfy this milestone.

## Stage 2 Run a persistent authored story

**Outcome:** one villager tells a deterministic three-beat story across encounters and restarts.

1. Add a small story controller with explicit stages: not started, introduction, development, payoff, complete. Track prepared and in-flight delivery separately from committed progress.
2. Advance only after the verified delivery acknowledgement. Commit progress once per delivery ID so repeated callbacks cannot advance twice. If interrupted before acknowledgement, leave the beat pending; repeating an interrupted beat is preferable to skipping unseen content.
3. Persist a versioned sidecar under the user data root, associated with the save identity and stable resident identity. Store story ID/revision, outline, current beat, delivered text summaries or bounded text, and pacing metadata. Generated suggestions are not world facts.
4. Write atomically outside hooks. Detect missing/corrupt state and mismatched save identity. Handle reload, save import/restore, villager departure, and clock changes conservatively. If native save rollback cannot be matched reliably, document the limit and offer a narrative reset rather than merging incompatible history.
5. Permit at most one new beat per encounter, with a configurable cooldown. Start with one active story total and a deterministic debug override for testing.

**Gate:** the complete story survives a normal restart, advances once per acknowledged beat, pauses for protected interactions, and resumes without skipping a stage. Corrupt or mismatched sidecars cannot affect the native save. Clock rollback cannot trigger a burst of story advancement.

## Stage 3 Connect a bounded LLM writer

**Outcome:** the authored story can be replaced by a generated outline and generated dialogue without changing the delivery contract.

1. Define a provider-independent writer interface plus a deterministic fake provider. Implement one real provider adapter after selecting an available endpoint and model; keep that configuration separate from game integration. Do not hardcode credentials or log them.
2. Supply only verified context: speaker/personality fields actually identified, supported language, current outline/stage, delivered history, and known relevant facts. Missing facts remain missing. Do not upload saves or raw traces.
3. Request a compact three-beat outline from an authored story shape, then generate each beat against that outline and current history. Keep one coherent premise and a bounded ending. Begin with a small set of story shapes rather than arbitrary quests.
4. Require structured output containing schema version, story revision, beat index, and bounded plain-text pages. Validate types, lengths, encoding, supported characters, and references. Enforce structural constraints in code; assess tone and factual consistency with example evaluations, since schema validation cannot prove narrative truth.
5. Generate on a worker ahead of the next opportunity. Cache validated results by save/resident/story/context revision. Use bounded queues, output/token limits, timeouts, cancellation, and limited retries. Late results must not revive abandoned stories.
6. On timeout, provider failure, malformed output, stale context, or missing content, use native dialogue and leave story progress unchanged. Never replace gameplay with an error message.

**Gate:** a real generated story completes in game. A fake provider deterministically exercises delays, failures, malformed replies, and stale results. None stalls gameplay, skips a beat, or overrides a protected interaction.

Provider/model choice is deliberately deferred until the bridge works. Use the configured service available at implementation time; credentials or paid service activation may require user input. All earlier stages remain executable without them.

## Stage 4 Make the story feel personal

**Outcome:** restrained variety and callbacks make repeat conversations worth seeking out.

1. Add a few authored story shapes and personality guidance grounded in supported context. Limit recent-premise repetition and retain a short history of completed stories.
2. Tune dialogue length to observed native presentation. Verify pacing and tone with multiple generated examples, including a complete arc read in sequence.
3. If choice insertion is now understood, add two or three bounded responses and store the selected option. Acknowledge choices only after the game confirms them. Use them to shape a later beat; otherwise keep the returning-conversation interaction.
4. Optionally add expressions from a small verified allowlist. Preserve ordinary presentation if no safe expression hook exists.

**Gate:** several complete arcs have a recognisable premise and payoff, consistent character voice, and accurate callbacks. Choices, if implemented, affect later dialogue. No story depends on unsupported gameplay actions or fabricated player behaviour.

Choices and expression hooks are enhancements, not blockers for the first playable release. Expand resident coverage only after the one-resident slice works.

## Stage 5 Package and verify the playable milestone

**Outcome:** an opt-in feature that can be enabled, disabled, diagnosed, and reset without touching the native save.

1. Integrate enabled state, provider configuration, and narrative reset with established configuration/UI conventions. Show unsupported-build or unavailable-provider status in settings, outside normal conversations.
2. Add focused automated checks for eligibility precedence, exactly-once advancement within a delivery, persistence recovery, and stale asynchronous results. Use synthetic data; tests must not require game assets or paid inference.
3. Run game scenarios covering normal chatter, pending gameplay/event content, interrupted delivery, restart, scene transition, unavailable provider, and feature disablement. Include existing mouse/controller dialogue and camera input regressions where hooks overlap.
4. Record supported builds, language, pending-event coverage, known limitations, local storage location, reset behaviour, and provider setup. Measure frame-time behaviour during generation rather than assuming asynchronous work is free.

**Gate:** demonstrate a complete generated arc, a restart between beats, a protected interaction taking precedence, and uninterrupted native dialogue with the provider unavailable. Required regression checks pass. Unverified pending-state categories remain blocked.

## Suggested implementation layout

Use `runtime/src/features/narrative/` for the controller, owned context snapshots, persistence, validation, and worker/provider interface. Keep build-specific hooks and fingerprints with the established game/version integration. Register through the existing native feature framework rather than adding scattered branches to unrelated runtime services.

The minimal boundary is:

```text
Native conversation selection
  -> owned context and eligibility snapshot
  -> lookup of a ready validated beat
  -> fresh eligibility check
  -> native message presentation
  -> delivery acknowledgement
  -> controller commit and queued persistence

Controller context snapshot
  -> background writer
  -> validation and revision check
  -> ready beat cache
```

Avoid a general scripting engine, vector database, multi-agent simulation, or world-action planner for this milestone. Small bounded records and an explicit state machine are sufficient.

## Future stages after the milestone

Add capabilities individually: an existing reaction, a verified observed gameplay trigger, an item interaction, a location rendezvous, and eventually coordinated scenes. Each capability needs explicit preconditions, an execution result, cancellation, and memory based on observed completion. The model may propose an action only when its capability exists; the controller remains responsible for authorising and executing it.

## Sol starting task

Begin Stage 0. Inspect current workspace changes and the installed build, then trace one ordinary villager conversation and one available protected interaction. Implement observation-only instrumentation and report the concrete interception candidates, pending-state evidence, and unresolved coverage. Preserve unrelated work. Do not begin with provider plumbing or blanket message-archive replacement.

After each stage, update this file with completion status, relevant implementation paths, commands/checks run, and the next unresolved gate. Continue across completed gates without routine reconfirmation. Surface a blocker when required game state, a suitable test scenario, or provider configuration is unavailable; continue independent work that does not depend on it.
