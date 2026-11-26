#pragma once

namespace RE::BSScript {
    class IVirtualMachine;
}

namespace MORWHA::Bridge::Papyrus {

    bool RegisterPapyrusFunctions(RE::BSScript::IVirtualMachine* vm);

}  // namespace MORWHA::Bridge::Papyrus
