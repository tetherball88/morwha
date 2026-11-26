#pragma once

#include <cstdint>

namespace RE {
    class Actor;
    class BGSTextureSet;
    class NiAVObject;
    class NiNode;
    class TESForm;
    class TESObjectARMA;
    class TESObjectARMO;
    class TESObjectREFR;
}

namespace MORWHA::Engine::SKEE {

    using skee_u64 = std::uint64_t;
    using skee_u32 = std::uint32_t;
    using skee_i32 = std::int32_t;
    using skee_u16 = std::uint16_t;
    using skee_u8 = std::uint8_t;

    class IPluginInterface {
    public:
        IPluginInterface() = default;
        virtual ~IPluginInterface() = default;

        virtual skee_u32 GetVersion() = 0;
        virtual void Revert() = 0;
    };

    class IInterfaceMap {
    public:
        virtual IPluginInterface* QueryInterface(const char* name) = 0;
        virtual bool AddInterface(const char* name, IPluginInterface* pluginInterface) = 0;
        virtual IPluginInterface* RemoveInterface(const char* name) = 0;
    };

    struct InterfaceExchangeMessage {
        enum : skee_u32 {
            kMessage_ExchangeInterface = 0x9E3779B9u
        };

        IInterfaceMap* interfaceMap{ nullptr };
    };

    class IOverrideInterface : public IPluginInterface {
    public:
        enum {
            kPluginVersion1 = 1,
            kPluginVersion2,
            kCurrentPluginVersion = kPluginVersion2,
            kSerializationVersion1 = 1,
            kSerializationVersion2,
            kSerializationVersion3,
            kSerializationVersion = kSerializationVersion3
        };

        class GetVariant {
        public:
            virtual void Int(const skee_i32 i) = 0;
            virtual void Float(const float f) = 0;
            virtual void String(const char* str) = 0;
            virtual void Bool(const bool b) = 0;
            virtual void TextureSet(const RE::BGSTextureSet* textureSet) = 0;
        };

        class SetVariant {
        public:
            enum class Type { None, Int, Float, String, Bool, TextureSet };

            virtual Type GetType() { return Type::None; }
            virtual skee_i32 Int() { return 0; }
            virtual float Float() { return 0.0f; }
            virtual const char* String() { return nullptr; }
            virtual bool Bool() { return false; }
            virtual RE::BGSTextureSet* TextureSet() { return nullptr; }
        };

        virtual bool HasArmorAddonNode(RE::TESObjectREFR* refr, bool firstPerson, RE::TESObjectARMO* armor, RE::TESObjectARMA* addon, const char* nodeName, bool debug) = 0;
        virtual bool HasArmorOverride(RE::TESObjectREFR* refr, bool isFemale, RE::TESObjectARMO* armor, RE::TESObjectARMA* addon, const char* nodeName, skee_u16 key, skee_u8 index) = 0;
        virtual void AddArmorOverride(RE::TESObjectREFR* refr, bool isFemale, RE::TESObjectARMO* armor, RE::TESObjectARMA* addon, const char* nodeName, skee_u16 key, skee_u8 index, SetVariant& value) = 0;
        virtual bool GetArmorOverride(RE::TESObjectREFR* refr, bool isFemale, RE::TESObjectARMO* armor, RE::TESObjectARMA* addon, const char* nodeName, skee_u16 key, skee_u8 index, GetVariant& visitor) = 0;
        virtual void RemoveArmorOverride(RE::TESObjectREFR* refr, bool isFemale, RE::TESObjectARMO* armor, RE::TESObjectARMA* addon, const char* nodeName, skee_u16 key, skee_u8 index) = 0;
        virtual void SetArmorProperties(RE::TESObjectREFR* refr, bool immediate) = 0;
        virtual void SetArmorProperty(RE::TESObjectREFR* refr, bool firstPerson, RE::TESObjectARMO* armor, RE::TESObjectARMA* addon, const char* nodeName, skee_u16 key, skee_u8 index, SetVariant& value, bool immediate) = 0;
        virtual bool GetArmorProperty(RE::TESObjectREFR* refr, bool firstPerson, RE::TESObjectARMO* armor, RE::TESObjectARMA* addon, const char* nodeName, skee_u16 key, skee_u8 index, GetVariant& value) = 0;
        virtual void ApplyArmorOverrides(RE::TESObjectREFR* refr, RE::TESObjectARMO* armor, RE::TESObjectARMA* addon, RE::NiAVObject* object, bool immediate) = 0;
        virtual void RemoveAllArmorOverrides() = 0;
        virtual void RemoveAllArmorOverridesByReference(RE::TESObjectREFR* reference) = 0;
        virtual void RemoveAllArmorOverridesByArmor(RE::TESObjectREFR* refr, bool isFemale, RE::TESObjectARMO* armor) = 0;
        virtual void RemoveAllArmorOverridesByAddon(RE::TESObjectREFR* refr, bool isFemale, RE::TESObjectARMO* armor, RE::TESObjectARMA* addon) = 0;
        virtual void RemoveAllArmorOverridesByNode(RE::TESObjectREFR* refr, bool isFemale, RE::TESObjectARMO* armor, RE::TESObjectARMA* addon, const char* nodeName) = 0;

        virtual bool HasNodeOverride(RE::TESObjectREFR* refr, bool isFemale, const char* nodeName, skee_u16 key, skee_u8 index) = 0;
        virtual void AddNodeOverride(RE::TESObjectREFR* refr, bool isFemale, const char* nodeName, skee_u16 key, skee_u8 index, SetVariant& value) = 0;
        virtual bool GetNodeOverride(RE::TESObjectREFR* refr, bool isFemale, const char* nodeName, skee_u16 key, skee_u8 index, GetVariant& visitor) = 0;
        virtual void RemoveNodeOverride(RE::TESObjectREFR* refr, bool isFemale, const char* nodeName, skee_u16 key, skee_u8 index) = 0;
        virtual void SetNodeProperties(RE::TESObjectREFR* refr, bool immediate) = 0;
        virtual void SetNodeProperty(RE::TESObjectREFR* refr, bool firstPerson, const char* nodeName, skee_u16 key, skee_u8 index, SetVariant& value, bool immediate) = 0;
        virtual bool GetNodeProperty(RE::TESObjectREFR* refr, bool firstPerson, const char* nodeName, skee_u16 key, skee_u8 index, GetVariant& value) = 0;
        virtual void ApplyNodeOverrides(RE::TESObjectREFR* refr, RE::NiAVObject* object, bool immediate) = 0;
        virtual void RemoveAllNodeOverrides() = 0;
        virtual void RemoveAllNodeOverridesByReference(RE::TESObjectREFR* reference) = 0;
        virtual void RemoveAllNodeOverridesByNode(RE::TESObjectREFR* refr, bool isFemale, const char* nodeName) = 0;

        virtual bool HasSkinOverride(RE::TESObjectREFR* refr, bool isFemale, bool firstPerson, skee_u32 slotMask, skee_u16 key, skee_u8 index) = 0;
        virtual void AddSkinOverride(RE::TESObjectREFR* refr, bool isFemale, bool firstPerson, skee_u32 slotMask, skee_u16 key, skee_u8 index, SetVariant& value) = 0;
        virtual bool GetSkinOverride(RE::TESObjectREFR* refr, bool isFemale, bool firstPerson, skee_u32 slotMask, skee_u16 key, skee_u8 index, GetVariant& visitor) = 0;
        virtual void RemoveSkinOverride(RE::TESObjectREFR* refr, bool isFemale, bool firstPerson, skee_u32 slotMask, skee_u16 key, skee_u8 index) = 0;
        virtual void SetSkinProperties(RE::TESObjectREFR* refr, bool immediate) = 0;
        virtual void SetSkinProperty(RE::TESObjectREFR* refr, bool firstPerson, skee_u32 slotMask, skee_u16 key, skee_u8 index, SetVariant& value, bool immediate) = 0;
        virtual bool GetSkinProperty(RE::TESObjectREFR* refr, bool firstPerson, skee_u32 slotMask, skee_u16 key, skee_u8 index, GetVariant& value) = 0;
        virtual void ApplySkinOverrides(RE::TESObjectREFR* refr, bool firstPerson, RE::TESObjectARMO* armor, RE::TESObjectARMA* addon, skee_u32 slotMask, RE::NiAVObject* object, bool immediate) = 0;
        virtual void RemoveAllSkinOverrides() = 0;
        virtual void RemoveAllSkinOverridesByReference(RE::TESObjectREFR* reference) = 0;
        virtual void RemoveAllSkinOverridesBySlot(RE::TESObjectREFR* refr, bool isFemale, bool firstPerson, skee_u32 slotMask) = 0;
    };

}  // namespace MORWHA::Engine::SKEE
