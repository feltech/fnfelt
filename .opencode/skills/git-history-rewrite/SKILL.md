---
name: git-history-rewrite
description: >-
  Interactive rebasing and deep multi-commit restructures in this repo: squash/fixup, reword,
  reorder, insert, drop, content moved between commits. Verified conflict resolution rules,
  signing facts, and verification gates. Use when the user authorizes a git history rewrite.
license: MIT
---

# Git history rewrite (fnfelt)

Rewriting history is encouraged by this project's conventions, but an agent must NEVER start a
rebase on its own initiative — only when the user explicitly authorizes a specific rewrite.
AGENTS.md says "do not attempt to rebase – the user will do that"; an authorized run means the user
has become the rebase operator and you are executing for them.

## Project facts (verified 2026-10-14)

- Every commit is GPG-signed; `commit.gpgsign=true`, key `FF83B87DDF71AD3D`. `git rebase` re-signs
  rewritten commits NATIVELY — never pass `-S` manually, never add a post-rewrite signing hook. A
  plain `git commit --amend` outside a rebase also re-signs because gpgsign is on; verify with `%G?`
  afterwards.
- The `commit-msg` gitlint hook fires on every commit the rebase makes: title ≤ 50 chars, body lines
  ≤ 72 (pre-wrap with real newlines), Conventional-Commit title, `Signed-off-by:` trailer. Keep
  messages lint-clean or `--continue` fails mid-chain.
- `drop! PLAN.md`-style commits are gitlint-ignored by title regex; they replay like any other
  commit.
- The `commit-msg` hook shells out to `direnv`: run `direnv allow` in every worktree where commits
  are made (verified 2026-10-17).

## Pre-flight (run BEFORE the first rebase command)

1. `git status` — require a clean tree (untracked files are fine; commit or stash tracked changes
   first).
2. `git branch backup/<name> <tip>` — a backup branch at the current tip is mandatory before any
   rewrite. Never delete it.
3. Record `git rev-parse <tip>^{tree}` — the tree hash of the old tip. For content-neutral rewrites
   (squash/reword/reorder) the new tip MUST have the identical tree.
4. `git log --oneline --format='%h %G? %s'` — know the range, count, and signature status you are
   rewriting.
5. Predict conflicts: for each file the moved/squashed commit touches, list the commits between
   target and source that touch the same files (`git log --oneline <target>..<source> -- <file>`).
   Expect one conflict stop per such commit. Expect add/add (`AA`) conflicts for files a rewritten
   commit *creates* that a later commit also touches. Predicted lists are an upper bound: identical
   line additions on both sides auto-merge. Verify the real conflict set at each stop with
   `git diff --name-only --diff-filter=U`.
6. Signing smoke test: `git commit --allow-empty` with a lint-clean message, check `%G?` = `G`, then
   `git reset --hard HEAD~1`. Proves the gpg agent signs non-interactively (no pinentry hang
   mid-rebase).

## Squash a late commit V into an early commit T

Use `rebase -i` with a scripted todo (works for arbitrary titles; plain `--autosquash` needs a
`fixup!` title and never invokes the sequence editor without `-i`):

```bash
GIT_SEQUENCE_EDITOR='sed -i -e "/^pick <V> /d" \
  -e "/^pick <T> /a fixup <V> <subject>"' \
  GIT_EDITOR=true git rebase -i <parent-of-T>
```

`GIT_EDITOR=true` prevents editor hangs on `--continue`.

### Conflict resolution rules (verified live, 6 stops in one run)

Expect a stop at EVERY tail commit whose diff touches the same files as V (shared umbrella files
like `monad/io/traits.hpp` guarantee this).

- **Deterministic rule per stop**: target = *that commit's own blob* + V's insertions re-applied
  verbatim at the same anchors. Never hand-merge a hybrid; never take the final-tip blob wholesale
  (that smuggles later commits' content into an early commit).
- **Auto-merged files**: verify they landed on V's blob exactly — `git hash-object <file>` ==
  `git show <V>:<file> | git hash-object --stdin`. Auto-merge lands the late blob; eyeballing diffs
  misses content.
- **Resolved files**: the pass criterion is a *perfect subset of the final blob* — identical to the
  final file except missing only later-commit content. Verify with
  `diff <(git show <final>:<file>) <file>` and confirm every missing hunk belongs to a commit that
  replays after this stop.
- **Same-anchor ordering conflicts** (V's block and a replayed commit's block land at the same
  position, e.g. both at file end): resolve in the FINAL tip's order — the tail replay re-adds its
  own content, so place V's block where it sits in the final file.
- Verify every resolution BEFORE `git rebase --continue`; a bad resolution discovered later forces a
  full redo.

### Intermediate buildability

A squashed commit is often NOT standalone-buildable: V's code may reference helpers introduced by
commits between T and V (e.g. the `just` validator references `fnfelt::detail::append_string_view`,
added by the `create_async` commit). This is inherent to squashing a validator/extension into an
early feature commit. Do NOT backfill helpers to fix it — that changes trees and breaks the
tree-equality invariant. The gates are: tip tree == old tip tree, commit count −1, all `%G?` = G,
full build+test green at the TIP. Report the intermediate-buildability caveat to the user; do not
chase it.

## Reword-only runs

`git rebase -i` with `reword` todo lines; `GIT_EDITOR` a script that drops a prepared message file
per commit. Reword never conflicts (trees untouched) and re-signs natively. Pre-wrap body lines to
72 before feeding messages to the hook.

## Verification gates (all must pass; verify from command output, never from

narrative)

1. `git log --oneline --format='%h %G? %s'` — expected count, expected subjects, no leftover
   `fixup!`/`squash!` titles.
2. `git rev-parse HEAD^{tree}` == recorded old-tip tree (content-neutral rewrites). Content-changing
   rewrites have no tree-equality gate — use the drift gate from the relocation section.
3. `git rev-parse backup/<name>` == old tip commit.
4. `git status` clean.
5. `git log --format='%G?' | sort | uniq -c` — all `G`.
6. Full cycle at the tip: `conan install`, `cmake --preset conan-debug -Dfnfelt_ENABLE_DEV=ON`,
   `cmake --build`, `ctest` — green.
7. `git show <rewritten commit> --stat` — the squashed commit contains exactly the union of the two
   originals' files.
8. Placement greps match DEFINITIONS (`^consteval .*<name>(`), not occurrences — the moved-from
   header legitimately keeps call sites.

## Deep restructures (content moved between commits) — verified live, 2026-10-16

A restructure (content moving across ~6 commits, new commit inserted, commit dropped, file deleted)
is NOT a rebase job. Build the chain commit-by-commit on a scratch branch in a linked worktree
(`git worktree add /tmp/... -b scratch <base>`), gate every state, then land.

- **Scratch isolation**: a linked worktree keeps the user's dirty main worktree untouched; hooks and
  gpgsign work via the common dir. Per-commit `rm -rf build/Debug` gives a dedicated build dir per
  state without 20 worktrees. In a fresh worktree, conan writes `CMakePresets.json` into the `-of`
  dir, but the gitignored root `CMakeUserPresets.json` does not exist — generate it
  (`include: ["build/Debug/CMakePresets.json"]`) or `cmake --preset` fails.
- **File x era adaptation map first**: before constructing any state, classify every touched file as
  *fresh* (era-restore safe: `git checkout <era> -- <file>`) or *adapted* (carries earlier-phase
  adaptations — era checkout SILENTLY reverts them). Adapted files need a three-way merge: base =
  original parent's blob, ours = adapted parent's blob, theirs = original target's blob
  (`git merge-file`), resolving by "keep adapted form of previously-converted sites, take the era's
  new content everywhere else". Preview conflicts across all shared files before touching the
  worktree. Accidental clobber: `git restore <file>` then verify
  `git hash-object <file> == git rev-parse HEAD:<file>`.
- **Audit between phases**: read-only `git diff <orig> <new>` per state; never let more than one
  phase of unaudited adaptations accumulate.
- **Removing a constraint can hijack overload resolution**: an unconstrained perfect-forwarding
  constructor beats the implicit copy/move ctors in two ties — non-const-lvalue copies and
  const-rvalue moves (e.g. a lambda closure move of a captured `const IO` member; fix with
  init-capture `[x = x]` for a semantics-preserving one-token fix). Enumerate the ties a constraint
  guards before deleting it; map blast radius by grepping the affected *operations*, not the symbol.
- **CTAD/deduction-guide removal rewrites every call site**: grep the pattern (`IO{`) across ALL
  eras before construction and budget every hit as a test edit; "verbatim replay" can never include
  files exercising the removed feature. Sanctioned conversions:
  `using action_t = decltype(...); IO<action_t>(...)` for construction tests; `create(lambda)` where
  only an IO value is needed. Two written-out lambdas are distinct closure types — use a named
  variable. Tests travel with functionality: convert, never delete.
- **Deleted-file fallout is multi-class**: build manifests (FILE_SET lines re-introduced by every
  later era's whole-file checkout), include graph, qualified symbol uses, AND compile-fail
  ERROR_REGEXes that match diagnostic text byte-for-byte. Local error builders must produce
  byte-identical strings to the erased ones. "The file is gone" is never the finish line.
- **Include cycles from moving declarations**: treat as a content-ordering problem — something must
  be declared before the cycle closes. Use the project's `detail/fwd.hpp` idiom (consteval/inline
  function *declarations* suffice for header bodies; definitions only needed at instantiation).
  Probe linters/compilers on candidate layouts in /tmp before burning build cycles.
- **Linter drift at era commits**: era blobs fail today's clang-format/cmake-format/cpplint. Decide
  the fold-in policy up front ("format-only, semantic-free, documented"); record such fixes as
  tree-non-neutral deviations. Gate the untouched base FIRST to discover pre-existing violations
  before building 20 states on top.
- **Original history may be red at intermediate commits**: prove "pre-existing red" by compiling the
  untouched original before blaming your adaptations; fix with minimal, assertion-preserving driver
  changes and document them.
- **Per-commit gating cost**: full conan/cmake/build/ctest per state; later reflection+libfork
  builds ~2.5 min each — hours total. Red gate → diagnose exact compiler error → fix within plan
  rules → `--amend` → re-gate. Never proceed past a red gate; never weaken an assertion. If the fix
  exceeds the plan, STOP and report. Builders report per-commit hash, `%G?`, tree hash, ctest
  summary for the audit trail. ctest summary wording varies — don't pattern-match a phrase.
- **Author-date quirks replay verbatim**: out-of-order author dates in the original chain are
  preserved; committer dates change, author dates must not.

### Relocation with per-commit buildability (verified live, 2026-10-17)

Moving functions into per-operation headers AT their introduction commits: a content-changing
rewrite where every rewritten commit must build+test green. Tree equality does NOT hold; the gates
are the drift gate below plus the standard signature/message gates.

- **Drift gate**: enumerate the intended change set up front (moved-from header, moved-to headers,
  include/FILE_SET edits); `git diff <old-tip> <new-tip> -- . ':(exclude)<intended files>'` must be
  EMPTY. Add author preservation: `git log --format='%an|%ae|%ad|%s'` old-tip vs new-tip must match
  (committer dates change; author dates must not).
- **Edit-stop procedure depends on pick outcome.** Clean `edit` stop → edit, `git add -A`,
  `git commit --amend --no-edit`, `--continue`. Conflicted pick → resolve to the deterministic
  target, `git add -A`, `git rebase --continue` ONLY — the sequencer recreates the commit with the
  original message/author and re-signs. NEVER amend at a conflicted pause: it folds the current
  commit's changes into the PREVIOUS commit and clobbers its message/author-date.
- **One `--continue` runs ahead**: after resolving a conflicted pick, a single
  `git rebase --continue` commits the resolution AND applies subsequent todo commands, stopping at
  the next conflict/edit pause in the same invocation.
- **Deterministic file construction beats marker-merging**: build each conflicted file from
  `git show <orig-commit>:<file>` (theirs) and `git show HEAD:<file>` (rewritten parent, ours) plus
  relocated blocks copied VERBATIM from the original blobs; verify every auto-merged file is
  byte-identical to its original blob (`git hash-object <file>` ==
  `git show <orig>:<file> | git hash-object --stdin`).
- **Edit-stop verification compares WORKING TREE vs parent** (`git diff HEAD^ -- <file>`);
  `git diff HEAD^ HEAD -- <file>` compares commits, is inherently non-empty, and proves nothing
  about your edits.
- **Consteval relocation idiom**: definitions move to the new header; callers keep compiling via the
  project's `detail/fwd.hpp` declarations; definitions are required only at template instantiation
  points, which every public header include chain reaches. The compile-fail tests in ctest are the
  visibility safety net.
- **Checkpoint builds per rewritten commit** in linked worktrees
  (`git worktree add /tmp/ckpt-<n> <hash>`, copy `CMakeUserPresets.json`); launch detached with
  `setsid nohup bash -c '<full cycle>' > build.log 2>&1 < /dev/null &` — a plain `nohup … &` is
  killed when the agent tool call times out, silently truncating ctest mid-run.

### Commit primitives (authorship-preserving)

- Verbatim replay: `git cherry-pick <orig>` — message, author, date preserved; re-signs natively.
- Same content, same message: `git commit -C <orig>`.
- Reworded message, preserved author+date: `GIT_EDITOR='cp <msgfile>' git commit -c <orig>`.
- Brand-new commit: `git commit -F <msgfile> --author='Name <email>'`.
- Gate-fix redoes: `git commit --amend --no-edit` (re-signs natively; never `-S`).
- Verify message+author preservation en masse at the end: loop comparing `%B` and `%an|%ae|%ad`
  against originals.

### Landing protocol

1. Verify backup branch == old tip. Snapshot pre-landing state: `sha256sum` of the user's dirty
   files, `git status --porcelain`, `git stash list` empty, new tip hash+subject.
2. `git stash push -- <user's dirty paths>` (untracked files survive `reset --hard` untouched).
   Never `reset --hard` over unstaged user work.
3. `git reset --hard <new-tip>` (HEAD already on main — no checkout).
4. `git stash pop --index` (fallback: `apply --index` + `drop`, then plain pop).
5. Verify sha256s match, porcelain identical, stash list empty, `rev-list --count`, all `%G?` = G,
   backup unchanged; run the tip gate.
6. **Anomaly check**: after pop, diff worktree vs HEAD per file (`git hash-object <f>` vs
   `git rev-parse HEAD:<f>`) — `stash pop --index` can resurrect a stale tracked blob from the
   stash's index snapshot; fix with `git restore <file>` and re-verify blob equality.

### Gate scoping with untouchable files

- The user's pre-existing dirty entries are the baseline: "clean" means *no changes we introduced*.
  Record the baseline before starting.
- Untouchable docs (e.g. `PLAN.md`) legitimately reference erased symbols — scope final grep gates
  to exclude them and flag the conflict in the report; never silently pass or edit them.
- A user file can block the tip gate (e.g. unformatted root `*.md` under mdformat). Do not touch it;
  certify the landed tree via a clean worktree with the identical tree hash
  (`git rev-parse HEAD^{tree}` both sides).

## Pitfalls

- Repeated conflicts at the SAME stop across attempts usually mean the squash target is semantically
  wrong (V depends on machinery the target predates), not bad resolution. A standalone commit
  (reword the `fixup!` away) is often the right answer.
- `git commit --amend` keeps the ORIGINAL parent — detach+amend never chains commits; use
  cherry-pick steps for a sequential rewrite.
- `git commit --amend --no-edit` at a CONFLICTED rebase pause amends the PREVIOUS commit: the
  current commit's changes fold into it and its message/author-date are clobbered. At conflicted
  pauses resolve, `git add -A`, `git rebase --continue` only.
- Background builds from an agent tool must be detached with `setsid … < /dev/null &`; plain
  `nohup … &` dies with the tool call's timeout and the checkpoint log truncates mid-ctest.
- Amends default the committer identity; set `GIT_AUTHOR_NAME/EMAIL/DATE` and `GIT_COMMITTER_*` to
  preserve authorship when amending outside a rebase.
- A crash mid-editor-write can leave the message file as NUL bytes; recover with
  `git restore --source=<ref> -- <file>` and verify by blob diff.
- Never `git push` a rewritten branch without the user doing it; never delete the backup branch in
  the same session as the rewrite.

## Improvement feedback (mandatory clause)

If you discover a solution, pitfall, conflict shape, command, or project fact during a rewrite that
this skill does NOT cover, you MUST report it to the user at the end of the run in a section titled
"Skill improvement suggestions": one bullet per gap, each stating (a) what you had to figure out,
(b) the verified answer, and (c) which section of this skill it belongs in. Do not edit this skill
yourself — the user decides what gets incorporated.
