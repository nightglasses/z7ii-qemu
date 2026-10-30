# NOR descriptor manifest

The native test manifest `tests/nor.json` records the original descriptor
geometry and registry fields. This view imports that file directly; it does
not supply a factory payload, infer missing field meanings or establish that
erased input is valid for normal startup.

For input semantics see the [NOR device profile](profile.md),
[external descriptor contract](descriptor.md), [module loader](module.md)
and explicit [cold-neutral resource80 profile](virtual.md).

<<< ../../../tests/nor.json
