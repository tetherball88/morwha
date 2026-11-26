#pragma once

#include "morwha/state/RegistryTypes.h"

namespace RE {
    class TESForm;
}

namespace MORWHA::Engine {

    [[nodiscard]] State::PluginFormRef MakePluginFormRef(const RE::TESForm* form);

    [[nodiscard]] RE::TESForm* ResolvePluginForm(const State::PluginFormRef& ref);

    template <class T>
    [[nodiscard]] T* ResolvePluginForm(const State::PluginFormRef& ref) {
        auto* form = ResolvePluginForm(ref);
        return form ? form->As<T>() : nullptr;
    }

}  // namespace MORWHA::Engine
