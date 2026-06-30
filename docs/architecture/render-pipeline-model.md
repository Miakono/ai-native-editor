# Render Pipeline Model

AI Native Editor separates renderer choices into three layers:

- `rendererProfile`: the project-facing choice exposed in Project Settings.
- `renderFeatures`: the material, lighting, post-processing, 2D/3D, resource, UI, and gizmo capabilities implied by that profile.
- `graphicsBackend`: the lower-level implementation target, currently `opengl`.

This intentionally does not copy Unity's URP/HDRP asset model directly. It is closer to a profile model: Unity-style project-level clarity, Unreal-style separation between renderer features and backend/RHI concerns, and Godot-style named renderer modes.

## Built-In Profiles

- `basic-built-in`: current renderer formalized as a supported project setting. It uses unlit color materials, placeholder/resource-backed model handling, runtime UI, editor gizmos, and current fog.
- `2d`: project profile for sprite/canvas-first games. The first pass records the intended material and feature contract; sprite renderer implementation is future work.
- `lightweight-3d`: project profile for portable 3D. It keeps the current framebuffer path working while reserving simple-lit materials, ambient/directional lighting, fog, and basic color grading.
- `high-fidelity-3d`: future PBR profile. It is registered but validates as unavailable until a GPU buffer/resource backend exists.

## Current Implementation Boundary

The current Scene and Game views still render through the existing OpenGL framebuffer code in `EditorApp`. The profile system is project schema and validation groundwork, not a production renderer rewrite.

Next renderer work should move toward:

1. Extracting render queue and backend interfaces out of `EditorApp`.
2. Adding material resource models for unlit, sprite, simple-lit, and later PBR.
3. Adding profile-specific validation for sprites, lights, post-processing, cameras, and platform exports.
4. Keeping Scene View editor gizmos separate from Game View runtime rendering.
