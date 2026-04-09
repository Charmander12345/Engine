# Copilot Instructions

## Project Guidelines
- User prefers an embedded Python interpreter using the newest available Python version.
- User prefers minimal, simple code changes without losing functionality. Changes should be compact and targeted, ensuring visual/UI modifications are simple, intuitive, clear, and comfortable for end users.
- Simplify widget lookup to a single function that searches all widgets in the UIManager member list.
- When Python scripting APIs change, also update the generated `engine.pyi` so IntelliSense stays in sync.
- Implement new UI controls as separate classes with their own header and cpp files in the UIWidgets folder for easier reuse.
- Keep the `PROJECT_OVERVIEW.md` document in the repository root up to date whenever changes are made to the engine project. All modifications should be reflected in this overview document.
- Keep the `ENGINE_STATUS.md` document in the repository root up to date whenever changes are made to the engine project, just like `PROJECT_OVERVIEW.md`. All modifications should be reflected in this status document.
- A major rework is planned: the editor will be fundamentally separated from the engine. The editor will connect to the engine core via an API and can be disabled during the build for the final game using a preprocessor definition (e.g., `#if ENGINE_EDITOR`). The build game functionality (Phase 10) will be re-implemented after this rework.
- Whenever possible use stack allocations instead of heap allocations for better performance and memory management, especially in performance-critical sections of the code.

## Implementation Approach
- When asked to continue with the next implementation step, proceed directly with the implementation instead of only describing it as a future next step.

## File Modification Preferences
- Remember that the ViewportOverlay toolbar previously had Select/Move/Rotate/Scale buttons (tool mode buttons) in a left StackPanel, which were removed by user request but should be considered for potential re-addition later.