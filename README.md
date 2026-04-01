# abstracto-engine

Integrated engine/app repo that composes the full Abstracto ecosystem.

## Workspace Layout

`abstracto-engine` is the workspace root. The other systems live inside
`systems/` as git submodules:

- `systems/abstracto`
- `systems/abstracto-scene`
- `systems/abstracto-animation`
- `systems/abstracto-assets`

`abstracto-engine` is the only repo that wires those systems together.
