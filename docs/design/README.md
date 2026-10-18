# Design notes: the thinking behind ladder_editor

The [milestone lessons](../lessons/README.md) follow the order things were built. These notes take the finished editor and explain **why** it's shaped the way it is: the decisions, the alternatives that weren't taken, and what each choice costs. soft_plc has [its own design notes](../../../soft_plc/docs/design/README.md); several ideas here build on them.

| # | Note | The question it answers |
|---|---|---|
| 1 | [A model that can't be wrong](01-the-model.md) | why rungs are a tree of values, and why paths instead of pointers |
| 2 | [Undo as snapshots](02-undo.md) | the command pattern, and why commands store "before" and "after" instead of knowing how to invert themselves |
| 3 | [Drawing everything, every frame](03-immediate-mode.md) | immediate-mode UI, what it makes easy, what it costs, and how it's tested without a screen |
| 4 | [Standing on soft_plc](04-standing-on-soft-plc.md) | reusing the compiler and runtime instead of writing a second one |
| 5 | [A GUI that never waits](05-a-gui-that-never-waits.md) | one background thread for the network, and the rules that keep it safe |
| 6 | [Containers in the editor](06-containers.md) | the choices here, including the ones that don't matter and the one `shared_ptr` |

## The principles, in one place

1. **Make wrong states impossible to build.** A rung can't have a loose wire because the data structure has no way to express one. Checks are for what's left.
2. **Everything is a value.** Rungs, blocks and projects can be copied and compared with `==`. Undo, change detection, "modified" stars and tests all fall out of that.
3. **Derive, don't store.** The layout, the power flow, the generated ST and the errors are computed from the model when they're needed. Anything stored next to the model can go stale; anything derived can't.
4. **One source of truth for the language.** The editor doesn't have its own idea of ST: it uses soft_plc's lexer, parser, analyzer and runtime, so the editor and the PLC can never disagree.
5. **The UI thread never waits.** Not on the network, not on the PLC. Slow things happen elsewhere and hand back results.
6. **Test the way a person uses it.** The UI is driven by simulated mouse and keys in the tests, so what's tested is what a user does.
