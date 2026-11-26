#include "morwha/engine/FormRefResolver.h"

namespace MORWHA::Engine {

    State::PluginFormRef MakePluginFormRef(const RE::TESForm* form) {
        if (!form) {
            return {};
        }
        const auto* file = form->GetFile(0);
        if (!file) {
            return {};
        }

        const RE::FormID rawId = form->GetFormID();
        const std::uint32_t localId = file->IsLight()
                                          ? (rawId & 0x00000FFFu)
                                          : (rawId & 0x00FFFFFFu);
        if (localId == 0) {
            return {};
        }

        return State::PluginFormRef{
            .plugin = std::string(file->GetFilename()),
            .localId = localId
        };
    }

    RE::TESForm* ResolvePluginForm(const State::PluginFormRef& ref) {
        if (ref.IsEmpty() || ref.plugin.empty() || ref.localId == 0) {
            return nullptr;
        }
        auto* dataHandler = RE::TESDataHandler::GetSingleton();
        if (!dataHandler) {
            return nullptr;
        }
        const auto* file = dataHandler->LookupModByName(ref.plugin);
        if (!file || file->compileIndex == 0xFF) {
            return nullptr;
        }
        const RE::FormID lookupId =
            file->IsLight() ? (ref.localId & 0x00000FFFu) : (ref.localId & 0x00FFFFFFu);
        return dataHandler->LookupForm(lookupId, ref.plugin);
    }

}  // namespace MORWHA::Engine
