# Copilot Instructions

## Project Guidelines
- User prefers an embedded Python interpreter using the newest available Python version.
- User prefers minimal, simple code changes without losing functionality.
- Simplify widget lookup to a single function that searches all widgets in the UIManager member list.
- When Python scripting APIs change, also update the generated `engine.pyi` so IntelliSense stays in sync.
- Implement new UI controls as separate classes with their own header and cpp files in the UIWidgets folder for easier reuse.