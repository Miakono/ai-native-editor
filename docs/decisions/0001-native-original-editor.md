# Decision 0001: Native Original Editor

## Status

Accepted for initial planning.

## Context

The goal is to build our own AI-native game engine editor. The user does not want to use Godot, Electron, or Tauri as the foundation.

Unity and Unreal are useful reference points and possible bridge targets, but not source bases to copy.

## Decision

Build an original native editor and runtime.

The editor should use permissively licensed dependencies where useful, but the product should own its project model, scene model, command API, AI integration, and editor workflow.

## Consequences

Benefits:

- full control over UX
- no engine royalty from a host engine
- AI system can be deeply integrated
- self-development can target our own plugin/module API

Costs:

- more engine/editor infrastructure to build
- graphics and asset pipeline take longer
- more responsibility for packaging, validation, and performance

## Implementation Notes

Start small:

- native app shell
- panels
- scene model
- command gateway
- AI chat dock
- viewport primitives

Do not start by chasing Unreal-class graphics. Prove the AI-in-editor loop first.
