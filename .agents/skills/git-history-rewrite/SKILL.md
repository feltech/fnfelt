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
- **Signature verification environment (verified 2026-10-25)**: cached `direnv` produced
  `E`/`NO_PUBKEY` for commits that verified as `G` under `nix develop ./build_env --command git …`.
  The cause was not diagnosed. Preserve the diagnostic and retry in the proper dev environment
  before classifying a signature as bad; do not bypass signature verification.

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
7. Scope the rewrite by POLICY, not just by feature: grep the entire history for every rule the
   rewrite must enforce (caller references, header self-references, banned docstring wording) and
   include the earliest violating commit as an edit stop:
   `for c in $(git rev-list --reverse HEAD); do git grep -n -E '<policy-regex>' "$c" -- include src tests; done`
   plus `git log -S'<offending phrase>' --oneline -- <path>`. A violation discovered after the first
   rebase completes forces a second full re-sign pass.
8. Capture the whole working state as a signed backup commit, not a stash (stash is not durable
   across linked-worktree operations; reflog-only commits are gc-vulnerable):
   `git branch backup/<dangling-name> <dangling-hash>` immediately; `git add -u` (tracked only);
   `tree=$(git write-tree); c=$(git commit-tree "$tree" -p HEAD -S FF83B87DDF71AD3D -m "chore: backup work state"); git branch backup/<name> "$c"`.
   Prove both index and worktree are represented: `test -z "$(git diff backup/<name> --stat)"` and
   `test -z "$(git diff --cached backup/<name> --stat)"`. Note `-S` takes the KEYID, never the tree.

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

## Comment/metadata-only passes (metafix) — verified live, 2026-10-23

When only comments/docs/metadata change, rewrite once and prove tree equality PER COMMIT; gate only
the commits whose trees actually change, plus the tip. Re-gating 20+ commits for one comment line is
pure waste.

1. Snapshot metadata before the pass:
   `git rev-list --reverse <base>..HEAD | while read c; do printf '%s %s %s\n' "$c" "$(git rev-parse "$c^{tree}")" "$(git log -1 --format='%an|%ae|%ad|%s' "$c")"; done > /tmp/opencode/pre.txt`
2. Rebase with `edit` only at the commits introducing the changes (scripted sequence editor).
3. Prove: per-commit tree hash from `pre.txt` vs new chain (unchanged where only comments in commit
   metadata changed); `git range-diff <old-base>..<old-tip> <new-base>..<new-tip>` shows only the
   intended lines; author identity/dates match old vs new.

## Verification gates (all must pass; verify from command output, never from

narrative)

01. `git log --oneline --format='%h %G? %s'` — expected count, expected subjects, no leftover
    `fixup!`/`squash!` titles.
02. `git rev-parse HEAD^{tree}` == recorded old-tip tree (content-neutral rewrites).
    Content-changing rewrites have no tree-equality gate — use the drift gate from the relocation
    section.
03. `git rev-parse backup/<name>` == old tip commit.
04. `git status` clean.
05. `git log --format='%G?' | sort | uniq -c` — all `G`.
06. Full cycle at the tip: `conan install`, `cmake --preset conan-debug -Dfnfelt_ENABLE_DEV=ON`,
    `cmake --build`, `ctest` — green.
07. `git show <rewritten commit> --stat` — the squashed commit contains exactly the union of the two
    originals' files.
08. Placement greps match DEFINITIONS (`^consteval .*<name>(`), not occurrences — the moved-from
    header legitimately keeps call sites.
09. `git range-diff <old-base>..<old-tip> <new-base>..<new-tip>` — per-commit patch audit, not just
    tip tree. `=` means identical; every `!` requires hunk inspection. A commit can silently absorb
    a neighbour's content and still yield the same tip tree; range-diff localises the drift. Under a
    STATE-TRANSFORM replay, `=` pairs are EXPECTED: the neighbour-to-neighbour patch
    `diff(T(orig_{i-1}), T(orig_i))` is unchanged whenever no hunk overlaps a transformed region.
    Only `!` pairs need hunk inspection; `=` pairs need a one-line justification, not a deviation
    report.
10. Conflict-marker sweep INCLUDING comments and strings, at every gate and across every rewritten
    commit: `git grep -n -E '^(<<<<<<<|=======|>>>>>>>)' HEAD -- .` and per-commit over the range. A
    marker inside a doxygen block compiles and passes build, doxygen, cpplint, and clang-format.
11. Expected-test-count table per commit: record expected ctest count and kept-test names in the
    plan before the sweep; compare at every stop (`ctest -N` for names). Fail the stop on any
    non-monotonic count or name mismatch — a test dropped at the commit it was lost is invisible to
    a terminal-only check, and a same-count swap hides behind monotonic totals.
12. Policy sweeps at every rewritten commit and at the tip: maintain the documented banned-regex
    list and grep EVERY syntactic variant up front (e.g.
    `used by|called by|provided to|returned by|passed to|overridable|@ref|for an invalid`),
    classifying hits as function docs vs type docs before editing. Narrow patterns miss
    function-docstring caller references and cost an extra history-wide re-sign pass.

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
- **Parallel sweep engine** (verified live, 2026-10-23): gate every commit in parallel detached
  workers over linked worktrees partitioned round-robin from `git rev-list --reverse`:
  `git worktree add --detach /tmp/opencode/sweep-<w> <hash>`, write
  `{"version":6,"include":["build/Debug/CMakePresets.json"]}` into each worker's
  `CMakeUserPresets.json`, per commit
  `checkout --detach <hash>; rm -rf build/Debug; conan install; cmake --preset; cmake --build --parallel 4 --preset; ctest`,
  write `<hash> PASS/FAIL <counts>` to a per-worker result file, launch with
  `setsid nohup bash <worker>.sh > <worker>.driver.log 2>&1 < /dev/null &`, remove worktrees at the
  end. Bound per-worker `--parallel` (4) so workers do not oversubscribe the machine; clean build
  dirs per commit avoid stale-preset false greens. 25+ commits finished in ~10 min on 3 workers vs
  ~1 hr serial.
- **Conservative sweep cache (verified with ccache 4.13.6, 2026-10-25)**: a task-local `CCACHE_DIR`
  and `CMAKE_CXX_COMPILER_LAUNCHER=ccache` accelerated clean gates with default correctness
  settings: no sloppiness, ignored headers, or remote cache; retain `hash_dir=true`. Failed
  compilations were uncached and unsupported PCH operations fell back to the compiler. Record tool
  version/settings and cached versus uncached provenance per row; keep clean-build, exact-test-set,
  and actual-PCH-consumption checks. Cached timings are not an uncached benchmark.
- **Sweep coordination and progress (verified 2026-10-25)**: in controlled `functions.exec` probes,
  active cells saw a snapshot of `store`/`load` state and published writes only on completion. Do
  not use live store flags to control active workers; use filesystem checkpoints or pause at a safe
  boundary and record an explicit ownership handoff. CTest may write live progress to randomized
  `Testing/Temporary/LastTest.log.tmp*` files; count completed `^Test time =` entries there rather
  than assuming a fixed `.tmp` filename or a live final log.
- **Sweep mdformat workaround (verified live, 2026-10-23)**: eras predating a docs lint fix fail the
  mdformat gate. In each sweep worktree, run `mdformat` on the tracked markdown (AGENTS.md,
  README.md, agent docs) after the checkout and NEVER commit it; switch eras with
  `git checkout -f --detach <hash>` so the dirty workaround file doesn't block the checkout. The
  gate then measures the commit's C++ content, not a pre-existing docs defect.
- **Classify "not gated by construction" commits**: commits predating the build system (no
  `conanfile`/preset) are recorded as structurally ungated, not regressions. Prove a pre-existing
  format/build failure by comparing the failing file's blob at the untouched base
  (`git rev-parse <commit>:<f>` vs base) and `git log -S` for the fix. Keeps the sweep report honest
  and stops you chasing red that predates the rewrite.
- **Pre-compile a target tip before rewriting**: stage every intended transformation,
  `git write-tree` / backup commit, run the full gate, keep it as `backup/<target>`. Transformation
  bugs discovered after ten edit stops cost a full replay; a pre-compiled target turns each era into
  a merge toward a known-good file and anchors the drift gate.
- **Author-date quirks replay verbatim**: out-of-order author dates in the original chain are
  preserved; committer dates change, author dates must not.

### State-transform replay (verified live, 2026-10-23) — PREFERRED over edit-stop rebase

When the intended change is a pure function of each ORIGINAL tree (relocations, retroactive helper
adoption, policy sweeps), replay a chain instead of rebasing. Branch from the PARENT of the earliest
tree-changing commit — untouched history keeps its hashes and committer dates and the gate surface
shrinks — then per commit: `git read-tree -u --reset <orig>^{tree}` → transform → `git add -A` →
`git commit -C <orig>`. Zero conflicts, no sequence-editor trap, no per-stop merges; any failure
restarts from base for free. The "never read-tree to the original" rule below applies to rebase edit
stops ONLY, not to a from-scratch chain.

- **Transform purity**: a pure function of `(original blob, era)` that never reads the previously
  rewritten tree, with `assert` old-XOR-new — pre-intro ABSENCE of the new form plus exactly one
  old-or-new form after. Absence catches premature application that presence-only checks miss.
- **Anti-circular gate**: hand-build the TARGET TIP first, gate it fully (build+lint), keep it as
  `backup/<target>`, then assert `transform(original tip) == target tip`. A transform validated only
  against replay output is circular. Whole-file substitution of the target-tip version is legitimate
  only behind a blob guard: `git hash-object <file>` == recorded `<old-tip>:<path>` blob.
- **Formatter configuration (verified 2026-10-25)**: generated CMake files outside the repository
  picked up default formatting and caused unrelated drift. Pass the repository configuration
  explicitly (`cmake-format --config-files <repo>/.cmake-format -i <generated-file>`); retain the
  independent target-tree equality check to catch configuration mistakes.
- **Precompute expected trees/blobs before any commit**: `git archive <c> | tar -x` → transform →
  `git hash-object -w` each changed file (mandatory — `write-tree` cannot resolve a bare hash) →
  `GIT_INDEX_FILE=<tmp>` set for read-tree, update-index AND write-tree alike → `git write-tree`.
  Replay verification becomes hash comparison, separating transform bugs from replay-mechanics bugs.
- **Dry-run the transform over ALL eras** before the first commit, exercising every branch: paths
  reachable only in pre-introduction eras are otherwise first executed during replay, where the only
  remaining check is self-comparison.
- **HEAD invariant: snapshot, never compare to base.** A dry run resets to base after each era, so
  `HEAD == base` holds inside its loop; the real replay commits advance HEAD, making the same check
  fail on the first era. Snapshot HEAD before each `read-tree` and assert it equals the snapshot
  after — valid in both scripts.
- **Implementation-independent anchors** (free, independent of transform correctness): exact
  function-body equality to the transformed tip; no-op eras where the original already has the
  target form (`transformed(era) == original blob`); determinism pairs — equal input blobs must
  produce equal outputs.
- **Assert the per-era changed-path set**: `git diff-tree -r` original vs predicted tree == the
  expected active set for that era. Catches accidental edits and carried drift a tip-only gate
  cannot see.
- **Sweep the ORIGINAL commits as controls** alongside the rewrites (same clean-build protocol);
  diff sorted `ctest -N` name sets per pair — a free expected-test table that catches renames/drops
  count-only checks hide. Sweep setup: delete `build/` AND the root `CMakeUserPresets.json` per
  state (conan regenerates it); `-of <worktree>/build/Debug` with the profile path resolved against
  the worktree; 3 workers × `--parallel 4`; `setsid nohup … < /dev/null &` + per-state results.
- **Keep scripts and prediction artefacts OUTSIDE the rewrite worktree** — `read-tree` /
  `reset --hard` must never be able to clobber the tooling or the audit trail.
- **Reflected types**: keep a forward declaration above the first `^^Name` use (`^^Name` is
  non-dependent lookup at template-definition time, so a later definition cannot satisfy it), and
  relocate FILES, not namespaces — moving a type into `detail::` changes qualified names,
  result-type identity, `^^` sites and diagnostic text (drift unrelated to the requested move).
- **Pin the byte layout of moved blocks** including blank-line joins: define the block to exclude
  its trailing blank; removal deletes `block + "\n"`; insertion replaces the existing single blank.
  Assert `clang-format --dry-run --Werror`, never `-i` plus "accept the result" — a double blank
  auto-formats into a delta that silently breaks the byte proof mid-run.
- **Parsing recipes (silent false negatives)**: `grep -c … | wc -l` returns 0 for BOTH no-match and
  grep failure (use `grep -q`); in Python use exact `str.count()`, not NUL-join tricks; anchor
  new-form detection on the extracted function body (single-line searches miss wrapped calls);
  `git diff-tree --name-only` is mode/tab decorated (use `-z`); `git rev-list A..B` EXCLUDES A.
  `git grep -c` exits 1 on zero matches and kills a `set -o pipefail` pipeline — wrap `|| true`.
  Perl: an unescaped `(` in a multiline pattern silently opens a group and never matches (escape
  literal parens in comment anchors); bare `perl -e` never reads files — probe with `perl -0 -n`.
  `ctest -N` pads single-digit tests as `Test  #N:` (two spaces) and 10+ as `Test #N:` — strip
  numbering before diffing name sets.
- **Large JSON evidence (verified 2026-10-25)**: tool output truncation/condensation produced
  valid-looking but incomplete data. Keep full JSON in files, use `jq` first to extract bounded
  rows, and assert expected row counts, unique state keys, and per-row test-name counts before
  merging results. Capture stdout and stderr separately: environment startup diagnostics appeared
  before JSON in combined output. Parse the stdout file directly; investigate unexpected prefixes
  instead of silently filtering them away.

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
- **Guard every `--continue` with a stopped-SHA equality test.** Because one continue can consume
  several todo items, assert the pause identity first and the advance after:
  `sm=$(git rev-parse --git-path rebase-merge); test "$(cat "$sm/stopped-sha")" = <expected-full-sha>`
  (and `test -f "$sm/amend"` to detect a pending edit-amend), require a clean
  `git status --porcelain`, then `GIT_EDITOR=true git rebase --continue`, then re-read HEAD and
  assert it advanced to the expected commit before doing anything else. Without the guard you
  silently amend the wrong commit or proceed from an unexpected state.
- **Third stop type: an `edit`-marked pick that CONFLICTS consumes its own edit pause.** The
  sequencer applies the commit and skips the pause. Resolve, `git add -A`, run the FULL gate on the
  resolved worktree, then `git rebase --continue`; verify `git diff <new-commit> --stat` is empty
  (the commit matches the gated tree). Never expect a later pause in which to amend it — amending
  then folds into the previous commit.
- **Never `read-tree` to the original commit at a clean edit stop.** The picked tree already carries
  all ancestor transformations; resetting to the original silently reverts earlier relocations and
  later picks never restore them (duplicate definitions, missing headers). Reset to an original tree
  only to rebuild a CONFLICTED state, and then reconstruct the full cumulative desired tree for all
  eras ≤ current, not just the current era's delta.
- **Deterministic file construction beats marker-merging**: build each conflicted file from
  `git show <orig-commit>:<file>` (theirs) and `git show HEAD:<file>` (rewritten parent, ours) plus
  relocated blocks copied VERBATIM from the original blobs; verify every auto-merged file is
  byte-identical to its original blob (`git hash-object <file>` ==
  `git show <orig>:<file> | git hash-object --stdin`).
- **Per-stop three-way merge workspace with a blob-keyed resolution cache** (verified live,
  2026-10-23): for repeated conflicts on the same helper files across eras, materialise
  `ours/base/theirs` under `/tmp/opencode/stopN/` (`git show HEAD:<f>` / `git show <orig>^:<f>` /
  `git show <orig>:<f>`), resolve once per DISTINCT original blob with `git merge-file -p`, and
  cache the result keyed by `git rev-parse <orig>:<f>`. Later eras with the same original blob
  re-apply by `git cat-file blob <key> > <f>` — never hand-merge the same file twice. Always compute
  the era delta against the TRUE original parent (`<orig>^`), never the rewritten parent; a wrong
  base silently produces hybrid blobs.
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

**Clean-index alternative (verified 2026-10-25)**: when the index is clean and a scratch-built new
tip is fully gated, prefer a guarded `git reset --mixed <new-tip>` if retaining the current working
files produces exactly the intended remaining user edits. This updates HEAD/index while preserving
dirty and untracked file bytes; it avoids the stash/hard-reset sequence below.

- Snapshot the branch/old HEAD, backup refs, clean index, status, dirty-file bytes, and untracked
  paths/bytes immediately before landing. Check the new tip/tree against the independently gated
  target and enumerate the expected post-landing diff.
- If a tracked working file needs the rewrite's new content first, apply only the reviewed
  preparation patch, preserving the user's edits. Verify its exact expected result, then recheck
  HEAD, index, refs, and all preserved bytes immediately before the mixed reset. Stop on drift.
- After the reset, verify the new HEAD/tree, clean index, unchanged backups and untracked files, and
  exactly the planned user diff; compare preserved bytes to the appropriate pre/post-patch
  snapshots. A clean-worktree gate of the identical tree certifies the landed commit without
  building the user's dirty worktree. This alternative does not preserve staged changes; use an
  explicit index-preserving plan when the index is dirty.

For a landing that requires replacing working files, use the following stash-based protocol:

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
7. **Recovery without stash — restore index+worktree from a backup commit, HEAD unmoved** (verified
   live, 2026-10-23): record forensics first (`git status --porcelain=v1`, `git diff --stat`,
   `git diff --cached --stat`, `git rev-parse --abbrev-ref HEAD HEAD` to a scratch file), then
   `git read-tree -u --reset <backup-commit>` (reproduces the exact staged state including
   deletions, leaves branch semantics intact), verify `test -z "$(git diff <backup-commit> --stat)"`
   and `test -z "$(git diff --stat)"` (index == worktree). Move a ref back with a compare-and-swap:
   `git update-ref refs/heads/main <new> <old>` (fails if the ref moved concurrently). Then verify
   the untracked-file list is unchanged and no `.lock`/`rebase-merge` residue remains.
8. **Staged-added user files (verified live, 2026-10-23)**: `reset --hard` DELETES index-tracked
   files — "untracked files survive" does NOT cover a file the user STAGED (added, uncommitted).
   Protocol: snapshot the worktree `sha256sum` AND the index blob id (`git rev-parse :<path>`);
   `git restore --staged <path>` (worktree bytes survive as untracked);
   `git reset --hard <new-tip>`; recreate the index entry with
   `git update-index --add --cacheinfo <mode>,<captured-blob>,<path>` BEFORE the long final gate, so
   a concurrent user `git add` supersedes rather than clobbers; byte-compare
   `git status --porcelain` against the pre-landing snapshot. Re-read the landing worktree's
   status/index IMMEDIATELY before `reset --hard` and abort on any deviation from a just-taken
   snapshot — users create and stage files while a long rewrite runs, so baseline at landing time,
   not at kickoff.
9. **Re-apply prepared resolutions by blob, not by re-resolving**: when a reset or mistaken checkout
   discards conflict resolutions, restore from cached blobs/patches keyed by
   `(original commit, path)` (`/tmp/opencode/resolutions/<orig>/<path>`):
   `git cat-file blob <hash> > <path>` or `git apply --3way <patch>`, verify against the intended
   blob. Re-resolution is where hybrid content and silent ancestor-loss creep in.

### Gate scoping with untouchable files

- The user's pre-existing dirty entries are the baseline: "clean" means *no changes we introduced*.
  Record the baseline before starting.
- Untouchable docs (e.g. `PLAN.md`) legitimately reference erased symbols — scope final grep gates
  to exclude them and flag the conflict in the report; never silently pass or edit them.
- A user file can block the tip gate (e.g. unformatted root `*.md` under mdformat). Do not touch it;
  certify the landed tree via a clean worktree with the identical tree hash
  (`git rev-parse HEAD^{tree}` both sides).
- **mdformat scope (verified live, 2026-10-23)**: mdformat preserves trailing whitespace inside
  fenced code blocks — only prose trailing spaces fail `--check`. Scope a docs-fix commit to prose
  violations; do not chase in-fence whitespace the formatter itself leaves.

## Pitfalls

- **Sequence-editor format trap** (verified live, 2026-10-23): this git renders the todo as
  `pick <hash> # <subject>`; a sed keyed on the old `pick <hash> <subject>` format is a silent
  no-op, and `cat` as `GIT_SEQUENCE_EDITOR` silently RUNS the whole rebase with no edits. Preview
  with an aborting editor (`GIT_SEQUENCE_EDITOR='cp "$1" /tmp/opencode/todo.txt; false'`), match
  `pick <hash> # <subject>`, and assert the edit count:
  `test "$(grep -c '^edit ' /tmp/opencode/todo.txt)" -eq N || exit 9`.
- **Enforce a trailing newline on every created file**: agent file-writes must end with `\n`
  (`printf '\n' >> file` when absent) and run the formatter before the first commit — the cpplint
  `whitespace/ending_newline` gate otherwise costs a full build cycle.
- **Assume the repo is shared; re-read state before every write**: use `git -C <worktree>` (or
  `cd <worktree> || exit 9`) for every command; assert `pwd`, branch, and HEAD; check for
  `index.lock`, `rebase-merge`, and `amend` state before acting. A silent cwd change applies every
  later command to the wrong worktree and `reset --hard` there moves the wrong branch; a concurrent
  IDE/agent session can run the pending rebase step and consume an edit pause.
- **Never detach a `rebase --continue`** (`setsid nohup git rebase --continue &`): an orphaned git
  process holds the repo lock and the sequencer state diverges from the agent's view. Detach only
  build/test sweeps, never sequencer steps.
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
