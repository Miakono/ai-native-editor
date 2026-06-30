# Risks And License Notes

This file is planning guidance, not legal advice.

## Core Licensing Position

The editor should be original software. It should not copy proprietary Unity or Unreal source code, editor code, runtime code, or internal asset formats.

Using permissive open-source libraries is acceptable when we comply with their licenses.

## Steam Release

Steam-only release does not remove third-party license obligations. It only changes distribution.

Expected needs:

- Steamworks onboarding.
- Steam app fee.
- third-party license compliance.
- code signing eventually for better Windows trust and publisher identity.

## Windows SmartScreen And Publisher Warnings

Windows warnings are mostly about code signing and reputation, not engine licensing.

For early prototypes, unsigned executables are acceptable for internal use.

For public release:

- sign the editor and launcher
- avoid unnecessary admin rights
- keep installer/updater behavior predictable
- build publisher reputation over time

## User-Owned Unity And Unreal Assets

The engine can support importing user-owned assets when the asset license allows use outside the original ecosystem.

Rules:

- Track asset source metadata.
- Track license flags.
- Do not promise support for every Unity or Unreal asset.
- Do not redistribute store assets.
- Do not train AI models on restricted assets.
- Do not bypass UE-only or provider-specific restrictions.

## Safer Asset Import Strategy

Start with neutral formats:

- glTF
- FBX
- OBJ
- PNG, JPG, TGA, EXR, HDR
- WAV, OGG

Later:

- Unity exporter plugin that exports selected user-owned assets through official Unity editor APIs.
- Unreal exporter plugin that exports selected user-owned assets through official Unreal editor APIs.
- Import the resulting neutral package into our editor.

## Dependency Risk

Avoid dependencies with restrictive licenses unless the tradeoff is intentional.

License categories:

- Low friction: MIT, BSD, Apache 2.0, zlib.
- Needs review: LGPL, MPL.
- High friction for proprietary editor: GPL, AGPL.
- Avoid unless explicitly licensed: proprietary SDK source, copied engine internals.

## Product Risk

The biggest risk is trying to build a full commercial engine and high-end renderer before proving the AI workflow.

The first product risk to retire is:

Can the built-in agent reliably operate the editor through typed tools?

If yes, it can help accelerate later work.

## Technical Risk

Hard areas:

- renderer architecture
- asset cooking
- animation import/retargeting
- material conversion
- script hot reload
- deterministic scene edits
- editor undo/redo correctness
- package/build reliability
- self-development safety

The plan should handle these incrementally instead of pretending they are solved by AI.
