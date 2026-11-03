# PLAN: Schema-Driven UI Manifest & Complex Parameter Form Generator

## 1. Overview & Objective
Enable the Visual Chain Builder to render complex, nested, and extensible handler configurations (such as `route_handler` rules, boolean expressions, and Lua scripts) dynamically from self-describing JSON Schema manifests, eliminating hardcoded UI components while preserving clean in-node canvas ergonomics.

---

## 2. Architectural Design

### 2.1 Manifest Contract (`handler.schema.json`)
The authoritative source of truth for handler parameters remains in `handler/<name>/handler.schema.json`. We enhance standard JSON Schema attributes with lightweight `ui:` rendering hints:

- **Complex Structures**:
  - `type: "array"`: Dynamic repeating lists of items (e.g. routing rules).
  - `type: "object"`: Nested groups of properties.
- **UI Widget Directives (`ui:widget`)**:
  - `ui:widget: "rule-list"` or `"array"`: Repeatable card container with `+ Add Item` and `✕ Delete` controls.
  - `ui:widget: "code-editor"` or `"code"`: Monospace editor supporting language syntax highlighting hints (`ui:language: "lua"` or `"proxy-expr"`).
  - `ui:widget: "channel-select"`: Dropdown selector automatically populated with output channels defined on the node or chain.
- **Conditional Visibility (`ui:visible_if`)**:
  - Declarative conditions (e.g. `{"type": "expression"}`) that toggle field visibility based on peer field values.

### 2.2 Two-Tier Parameter Editing UX
1. **Tier 1 — In-Node Card Canvas View**:
   - Scalar fields (strings, enums, numbers, booleans) render directly in the node card.
   - Complex types (`array`, `object`) render as a concise status pill:
     `[ ⚙ Configure Rules (N rules defined) ]`
   - Keeps nodes compact and avoids canvas clutter.
2. **Tier 2 — Slide-Out Configuration Drawer / Modal**:
   - Clicking `Configure Rules` opens an expanded slide-out drawer or focused modal dialog.
   - The generic form generator recursively parses the JSON Schema definition:
     - Renders array item headers, reordering, and item deletion.
     - Evaluates `ui:visible_if` to dynamically hide/show fields.
     - Offers syntax-friendly code editing for expressions and Lua blocks.
     - Synchronizes updates directly with the reactive node model (`node.config`).

### 2.3 Serialization & Validation Compatibility
- **TOML Export / Import**:
  - `builder_app.js`'s `toTomlString()` handles nested arrays of tables (`[[handlers.config.rules]]`) and multi-line strings.
  - `importChainDefinition()` preserves nested structures into `node.config`.
- **Python Runner & Schema Validator**:
  - `scripts/proxy_runner/schema_validator.py` validates nested rules and array types against the JSON Schema definition, flagging missing required rule fields or invalid enum choices.

---

## 3. Concrete Example: `route_handler` Manifest

```json
{
  "name": "route_handler",
  "displayName": "Route Dispatcher",
  "role": "processor",
  "parameters": {
    "default_action": {
      "type": "string",
      "label": "Default Action",
      "enum": ["drop", "forward", "reject"],
      "default": "drop",
      "description": "Action taken when no rules match."
    },
    "rules": {
      "type": "array",
      "label": "Routing Rules",
      "description": "Sequential rules evaluated against message payload and metadata.",
      "ui:widget": "rule-list",
      "items": {
        "type": "object",
        "properties": {
          "name": {
            "type": "string",
            "label": "Rule Name"
          },
          "type": {
            "type": "string",
            "label": "Condition Engine",
            "enum": ["expression", "pattern", "lua"],
            "default": "expression"
          },
          "pattern": {
            "type": "string",
            "label": "Match Pattern",
            "ui:visible_if": { "type": "pattern" }
          },
          "expression": {
            "type": "string",
            "label": "Boolean Expression",
            "ui:widget": "code",
            "ui:placeholder": "metadata.price > 100 && payload contains 'VIP'",
            "ui:visible_if": { "type": "expression" }
          },
          "script": {
            "type": "string",
            "label": "Lua Script",
            "ui:widget": "code-editor",
            "ui:language": "lua",
            "ui:visible_if": { "type": "lua" }
          },
          "action": {
            "type": "string",
            "label": "Action",
            "enum": ["forward", "reject", "drop"],
            "default": "forward"
          },
          "target": {
            "type": "string",
            "label": "Target Channel",
            "ui:widget": "channel-select",
            "ui:visible_if": { "action": "forward" }
          }
        }
      }
    }
  }
}
```

---

## 4. Implementation Tasks & Checklist

- [x] **Phase 1: Feature Documentation & Schema Definition**
  - [x] Create `docs/features/simulation/009-schema-driven-ui-manifest-and-nested-forms.md`.
  - [x] Update `handler/route_handler/handler.schema.json` with the structured `rules` array, expressions, Lua, and actions schema.
- [x] **Phase 2: Generic Form Renderer in Visual Builder**
  - [x] Add slide-out parameter drawer / modal component in `scripts/builder/index.html`.
  - [x] Implement recursive schema interpreter in `scripts/builder/js/builder_app.js`:
    - [x] Support `type: "array"` with add/remove/reorder items.
    - [x] Support `type: "object"` nested rendering.
    - [x] Implement `ui:visible_if` conditional logic.
    - [x] Implement `ui:widget: "channel-select"` bound to node output ports.
    - [x] Implement code/monospace input for expressions and Lua scripts.
  - [x] Add compact `[ ⚙ Configure <Field> (N) ]` trigger buttons on node cards for complex parameters.
- [x] **Phase 3: TOML Serialization & Schema Validator Updates**
  - [x] Extend `toTomlString()` in `builder_app.js` to serialize nested object lists (`[[handlers.config.rules]]`).
  - [x] Update `importChainDefinition()` to parse and bind nested TOML tables into node state.
  - [x] Extend `scripts/proxy_runner/schema_validator.py` to recursively validate array/object schema types.
- [x] **Phase 4: Verification & Automated Tests**
  - [x] Add test cases in `test/test_handler_schemas.py` for structured rule validation.
  - [x] Verify `route_handler` end-to-end in Visual Builder (create rule, set expression, configure target channel, export to TOML, import back).

