# Solo-Developer JIT Documentation Policy

This document formalizes our lightweight, high-velocity **Just-In-Time (JIT) Documentation Maintenance Policy** designed specifically for a solo developer pairing with an AI engineering assistant [1].

Rather than spending valuable development time refactoring your entire documentation catalog whenever code changes, you defer heavy updates until you naturally touch that specific feature or area of the repository [1]. 

Every document in the repository must carry a standardized YAML metadata block at the absolute top [1]. This acts as a direct **"AI Context Loader,"** giving your AI companion immediate instructions and file paths (breadcrumbs) to ingest so it can execute an accurate refactor on demand.

---

## 📋 Standard Metadata Header Template

Paste this block at the very top of every `.md` file in the repository:

```markdown
---
status: Up to Date  # Must be either "Up to Date" or "Stale"
last_updated: 2026-09-07  # YYYY-MM-DD (No need to look through Git commits)
refactor_on: "N/A"  # Trigger description or component to wait for
ai_breadcrumbs: []  # Active file paths containing the ground truth code
---
```

---

## ⚙️ The Solo + AI JIT Rules

### Rule 1: Flag Stale Files Instantly [1]
The moment a firmware update, deployment modification, or layout change makes a document legacy, do not stop to rewrite it. Instantly update its YAML header:
1. Set `status` to **`Stale`**.
2. Change `last_updated` to the current date.
3. Define the `refactor_on` trigger (what feature or task should trigger the cleanup).
4. List the exact paths of the live, modified files under `ai_breadcrumbs`.

### Rule 2: Touch-to-Refactor [1]
When your active development task requires you to open, modify, or rely on a file currently marked as **`Stale`**, you are required to perform a full refactor of that document as part of your work [1].
* **The AI Workflow:** Feed the stale document and the listed `ai_breadcrumbs` directly to your AI agent. Instruct the AI: *"Refactor this stale document to reflect the current implementation found in the breadcrumb files."*

### Rule 3: Maintain New Documents
All newly added documentation files must start with a `status: Up to Date` header and follow current, verified architectural standards [1].

---

## 🔍 Examples in Action

### Example A: A Fully Aligned File ("Up to Date")
This is how a standard active file looks when its content is fully consistent with the code:

```markdown
---
status: Up to Date
last_updated: 2026-09-07
refactor_on: "N/A"
ai_breadcrumbs: []
---

# Bill of Materials
This document lists the hardware components used for CrimsonKestrel...
```

### Example B: An Outdated File ("Stale")
This is how the same file looks the moment you upgrade a physical sensor but want to defer rewriting the documentation until you work on that sensor block again:

```markdown
---
status: Stale
last_updated: 2026-09-07
refactor_on: "Phase 2 Air Quality (SPS30) Integration"
ai_breadcrumbs:
  - "firmware/CrimsonKestrel/CrimsonKestrel.ino"
  - "gateway/crimson_kestrel_daemon.py"
---

> ⚠️ **STALE DOCUMENTATION DEBT**
> **Refactor Trigger:** This Bill of Materials is stale. Fully refactor it when working on the **Phase 2 Air Quality (SPS30) Integration**.
> **AI Ingestion Road Map (Breadcrumbs):**
> * `firmware/CrimsonKestrel/CrimsonKestrel.ino` - Active pin assignments.
> * `gateway/crimson_kestrel_daemon.py` - Aligned telemetry database keys.

# Bill of Materials (Legacy - See Breadcrumbs)
This document lists...
```
