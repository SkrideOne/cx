### GLOBAL RULES

Think first, write later — hidden CoT only.
CODE & comments ⇒ ENGLISH.  Explanations ⇒ RUSSIAN (outside fenced code).
These tasks are shared. They must be performed for each response in our chat room.

### DEFAULT PARAMETERS

* τ = 0.55        # self‑certainty threshold
* FactScore = 0.9 # evidence acceptance bar
* target_budget = clamp(1.5 × input_tokens, 512, 16 384)

### INJECTION‑SCAN (before concatenation)

* LLM‑Guard v2 (DeBERTa‑v3)
* SecAlign Prompt‑Guard rules
* Rebuff semantic PI filter
  Accept when **all** detectors < High‑Risk, *or* InjecGuard = benign ∧ LLM‑Guard ≤ Medium.

### OVER‑QUALIFICATION POLICY

Audience: senior domain experts — provide ≈ 30 % extra depth vs exhaustive answer:

1. Core solution  2) Edge cases  3) Sec/Perf trade‑offs  4) Alternatives & history  5) ≥ 5 IEEE refs.
   If `target_budget` would be exceeded — compress via layered summaries, do **not** drop sections.

### TOKEN‑BUDGET GUARD

Abort generation once output > 1.1 × `target_budget`.

### QUALITY PIPELINE (≤ 3 de debates · ≤ 2 reflections · 1 deep‑scrutiny)

0. Secure‑Scan ✓
1. Hidden CoT plan
2. Draft → measure self‑certainty *(sc)*
   – if `sc < τ`: spawn `N_extra = ceil((τ/sc)²) – 1` (cap 4) → Self‑Consistency modal answer
3. **LANGUAGE HOOK** – domain static checks (inserted by specific lang layer)
4. TDD reasoning (if tests)
5. Auto‑Checks: constraints ✓ facts ✓ `<redacted>`
6. UnCert‑CoT (internal flag)
7. Review Panel (8 agents) + contradiction & deception monitors
8. Score loop / ONE de‑scrutiny when 95 ≤ score ≤ 99
9. Consensus Gate → accept if checks ✓ & score ≥ 95
10. Output: deliverable + log + ≤ 8‟line RU‟summary + reference list

### ANTI‑HALLUCINATION

* Retrieve‑Augment‑Cite (METEORA / TopClustRAG) k≈6, MMR 0.8
* Discard evidence if score < FactScore; re‑scan retrieved text
* Chain‑of‑Verification + Holmes fact‑check; mark LOW‑CONF when score < 0.8
* Decoding: T 0.3, p 0.9, respect `target_budget`.

### CONTEXT MANAGEMENT

* Keep active window ≤ 60 k (≈ 30 %)
* Every 50 k: `<<SUMMARIZE_AND_PURGE>>` → ≤ 500 tok summary, drop raw text.

# LANGUAGE LAYER • C Quality Hooks  (rev‑2025‑07‑13 v4)

# Extends CORE at step 3; inherits all global rules, budgets, scans.

### LL‑0  CANVAS PROTOCOL

* For every source file the Project‑Analysis layer has created a canvas
  `FILEDOC::<relative/path>`.
* **All modifications must be made by updating that canvas**; never paste large
  code blocks back into chat.
* When adding new files create `FILEDOC::<new/path>`.

---

### LL‑1  LANGUAGE & TOOLCHAIN

* Detect tool versions:
  `GCC_VER=$(gcc --version | head -1)`
  `CLANG_VER=$(clang --version | head -1)`
* Fast build flags:
  `gcc-$GCC_VER -std=c11 -O2 -Wall -Werror -Wno-unused-parameter`
* Release build (PGO + LTO):
  `gcc-$GCC_VER -std=c11 -O2 -flto -fprofile-use -fprofile-dir=.pgo -DNDEBUG -march=native`
* Static analysers: clang‑tidy‑$CLANG_VER · clang‑sa · Facebook Infer · cppcheck‑latest
* Sanitizers: **UBSan** + **ASan** (+ LSan optional)
* Structs aligned to 64‟byte cache lines; forbid implicit int / decl.

---

### LL‑2  META‑FLAGS

* Enforce const‑correctness & explicit scopes
* Replace magic numbers with enums/macros
* Separate I/O from pure logic for testability
* Follow MISRA‑C 2024 (user‐space subset, Annex‐A)
* Add SPDX‑License‑Identifier to every file; auto‑generate SPDX SBOM.
* **Clean code** mandate: no commented‑out code, no obsolete wrappers; prefer
  modern C idioms (`static inline`, `sizeof *p` style).
* **No backward‑compat layers**: remove legacy shims unless explicitly required.

---

### LL‑3  PIPELINE EXTENSIONS  (Core step 3a → 3e)

3a Static analysis → clang‑tidy · clang‑sa · Infer · cppcheck
3b Secret & CWE scan (120/121/125/787/798/910/416) + SBOM
3c UBSan / ASan instrumented test run
3d Unit tests (cmocka / Unity) + TDD reasoning
3e libFuzzer ≥ 30‟s per public API (stop early if 10 k exec/s stable)
⇢ return to Core step 5

### LL‑3b  Workflow Order

clang-format → clang-tidy → smatch → coccinelle → build → verify (bpftool prog load --dry-run) → load-test → func-test → bench → perf → bolt (ebpf-bolt + llvm-bolt)

---

### LL‑4  DEAD‑CODE HANDLING

* Detect unused functions, variables, macros via static analysis + link map.
* **Mark dead‑code blocks in their canvas with `/* DEAD_CODE_START */ … /* DEAD_CODE_END */`.
* In the same update, append a comment `// REMOVE_IN_NEXT_REFRACTOR` and add a
  backlog item in the work plan; new code must not call dead elements.

---

### LL‑5  UNIT‑TEST TARGETS

* Present failing tests first; aim ≥ 90 % branch coverage (gcov / llvm‑cov).

---

### LL‑6  DELIVERABLE ADDITIONS

* PGO performance report (cycles / bytes saved)
* Static‑analysis findings & fixes summary
* Fuzz stats: corpus, unique paths, crashes
* Dead‑code removal log (file · line range · reason)

---

### LL‑7  RUBRIC EXTENSIONS  (weights from Core)

* **Robustness** = tests + sanitizers + fuzz (≥ 92).
* **Clean Code** = absence of dead code & commented‑out blocks (must be 100).
* Any remaining dead‑code or legacy shim ⇒ automatic −5 points each.

### PRE‑PROCESSING STEPS

* **Semantic chunking** – split at logical boundaries (function / class) into ≤ 300 LOC blocks using embedding‑aware segmentation.
* **Vector index** – embed every chunk and build / update an in‑memory FAISS index (incremental on re‑runs).
* **Dependency graph** – parse includes, imports and call graph to generate a directed code graph.

### ANALYSIS TASKS

1. **Repository overview** – purpose, major components, build system, external dependencies (≤ 120 tokens).
2. **Architecture & pipelines** – create a Mermaid `graph TD` of module dependencies; highlight nodes in the top 5 % betweenness centrality.
3. **Hotspots & complexity** – list the 10 most complex functions ranked by LOC × cyclomatic complexity; report TODO / FIXME density per file.
4. **Security & quality risks** – CWE scan (120 / 121 / 125 / 787 / 798 / 910 / 416) plus hard‑coded secret detection.
5. **Workstream plan** – for each hotspot propose: Quick‑win (≤ 1 h) • Refactor‑epic • Test‑gap, prioritised by risk × effort.

### OUTPUT SPECIFICATION

Return **one** Markdown document containing:

1. **File inventory** – table: path · LOC · language · role.
2. **Architecture diagram** – Mermaid block.
3. **Hotspots** – table with scores and line ranges.
4. **Risks** – CWE table with recommended mitigations.
5. **Work plan** – ordered backlog from quick wins to epics.

*Do not reproduce full source code; cite line numbers only.*

### BUDGET & SAFETY

* Respect the Core target_budget; when near the limit compress using the chunk → file → repo hierarchical‑summary strategy.

### HAND‐OFF

Store chunk embeddings and summaries in memory so that the subsequent language‑specific layer can retrieve relevant chunks via similarity search.

---

## REPOSITORY NOTE

Это **Agent.md** для **codex**.
Активный в нём репозиторий используется как **прокси-репозиторий** — он является **форком основного**.
**Все коммиты нужно направлять сразу в основной репозиторий в ветку`​GPT`”:
[https://github.com/SkrideOne/tested/tree/gpt](https://github.com/SkrideOne/tested/tree/gpt)
