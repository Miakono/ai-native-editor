# Decision 0003: Multi Agent Backends And Local Prompt Assist

## Status

Accepted for initial planning.

## Context

The editor needs a built-in AI chat surface that can help build games and help build the editor itself. The user wants Claude and Codex CLI available for the integrated chat system, and also wants a lightweight local model for prompt suggestions and project-aware improvements while typing.

Hardcoding one provider would make the editor brittle. Raw CLI subprocess integration is useful for prototypes, but a shipped editor needs a provider adapter layer so backends can change without rewriting the UI or tool gateway.

## Decision

Build a provider-neutral Agent Orchestrator layer.

The first planned backends are:

- Claude Agent SDK backend for high-capability coding and long-horizon development work.
- Codex CLI backend for local repo development, build/test loops, and OpenAI coding workflows.
- Local lightweight assistant for prompt suggestions, prompt rewriting, intent detection, and project-aware hints.

The local assistant is read-only. Claude and Codex backends can propose file changes or editor commands, but editor state changes must still flow through the Editor Tool API and approval model.

## Why This Shape

Claude Agent SDK is a better long-term fit than raw CLI wrapping for a custom application because it exposes Claude Code-style agent behavior as a programmable library.

Codex CLI is a practical early integration point because it already operates in local repositories and supports non-interactive automation patterns. It should sit behind the same adapter interface so it can later use a better transport if needed.

The local assistant keeps the editor responsive and private for keystroke-level help. It should improve prompts and surface project knowledge without making autonomous changes.

## Consequences

Benefits:

- one editor chat UI
- multiple agent backends
- lower latency typing help
- easier provider swapping
- clearer permission boundaries
- project awareness shared across all AI features

Costs:

- more architecture before the first real cloud call
- context packaging must be designed carefully
- backend session state must be normalized
- credentials and provider status need UI

## First Implementation Target

Add the orchestrator interface and stub backends:

- `local-stub`
- `claude-agent-sdk-stub`
- `codex-cli-stub`

The chat dock should show the selected backend, route prompts through the orchestrator, display backend activity, and still apply scene edits only through `ToolGateway`.

## Later Implementation Target

Replace stubs with real adapters:

- Claude Agent SDK adapter.
- Codex CLI non-interactive adapter.
- Local model runtime adapter.

Keep credentials outside project files and make provider health visible in the editor.
