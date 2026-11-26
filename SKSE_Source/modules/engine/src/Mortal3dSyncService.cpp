#include "morwha/engine/Mortal3dSyncService.h"

#include "morwha/engine/ChimeraAccessoryCatalog.h"
#include "morwha/engine/ChimeraMeshTinter.h"
#include "morwha/engine/EngineLogging.h"
#include "morwha/engine/HybridSkinService.h"
#include "morwha/engine/SkeeOverlayClient.h"

#include <RE/B/BSDynamicTriShape.h>
#include <RE/B/BSFaceGenAnimationData.h>
#include <RE/B/BSFaceGenBaseMorphExtraData.h>
#include <RE/N/NiBooleanExtraData.h>

#include <algorithm>
#include <chrono>
#include <cmath>

namespace MORWHA::Engine {

    namespace {
        constexpr std::uint32_t kDefaultSyncRetries = 100;
        constexpr std::int64_t kPollIntervalMs = 100;
        constexpr const char* kEarMorphMarkerKey = "MORWHA_EarMorph";

        void ApplyActorEarMorphIfNeeded(
            RE::TESNPC* npc,
            RE::BSFaceGenNiNode* faceNode,
            const std::vector<float>& earDeltas) {
            if (!npc || !faceNode || earDeltas.empty() || (earDeltas.size() % 3u) != 0u) {
                return;
            }
            const auto expectedVerts = static_cast<std::uint32_t>(earDeltas.size() / 3u);
            RE::BSDynamicTriShape* headDyn = nullptr;

            if (npc->headParts && npc->numHeadParts > 0) {
                for (std::int8_t i = 0; i < npc->numHeadParts; ++i) {
                    const RE::BGSHeadPart* hp = npc->headParts[i];
                    if (!hp || hp->type.get() != RE::BGSHeadPart::HeadPartType::kFace ||
                        hp->formEditorID.empty()) {
                        continue;
                    }
                    if (auto* obj = faceNode->GetObjectByName(hp->formEditorID)) {
                        if (auto* geom = obj->AsGeometry()) {
                            if (auto* dyn = geom->AsDynamicTriShape();
                                dyn && dyn->GetTrishapeRuntimeData().vertexCount == expectedVerts) {
                                headDyn = dyn;
                                break;
                            }
                        }
                    }
                }
            }

            if (!headDyn) {
                RE::BSVisit::TraverseScenegraphGeometries(
                    faceNode,
                    [&](RE::BSGeometry* geom) -> RE::BSVisit::BSVisitControl {
                        if (auto* dyn = geom ? geom->AsDynamicTriShape() : nullptr) {
                            if (dyn->GetTrishapeRuntimeData().vertexCount == expectedVerts &&
                                dyn->GetExtraData<RE::BSFaceGenBaseMorphExtraData>("FGBM") != nullptr) {
                                headDyn = dyn;
                                return RE::BSVisit::BSVisitControl::kStop;
                            }
                        }
                        return RE::BSVisit::BSVisitControl::kContinue;
                    });
            }

            if (!headDyn || headDyn->HasExtraData(kEarMorphMarkerKey)) {
                return;
            }

            bool appliedAny = false;
            if (auto* fgbm = headDyn->GetExtraData<RE::BSFaceGenBaseMorphExtraData>("FGBM");
                fgbm && fgbm->vertexData && fgbm->modelVertexCount == expectedVerts) {
                for (std::uint32_t v = 0u; v < expectedVerts; ++v) {
                    const float dx = earDeltas[static_cast<std::size_t>(v) * 3u + 0u];
                    const float dy = earDeltas[static_cast<std::size_t>(v) * 3u + 1u];
                    const float dz = earDeltas[static_cast<std::size_t>(v) * 3u + 2u];
                    if (dx != 0.0f || dy != 0.0f || dz != 0.0f) {
                        fgbm->vertexData[v].x += dx;
                        fgbm->vertexData[v].y += dy;
                        fgbm->vertexData[v].z += dz;
                        appliedAny = true;
                    }
                }
            }

            {
                auto& dynData = headDyn->GetDynamicTrishapeRuntimeData();
                RE::BSSpinLockGuard guard(dynData.lock);
                if (dynData.dynamicData != nullptr &&
                    dynData.dataSize == expectedVerts * 16u) {
                    auto* rawFloats = static_cast<float*>(dynData.dynamicData);
                    for (std::uint32_t v = 0u; v < expectedVerts; ++v) {
                        const float dx = earDeltas[static_cast<std::size_t>(v) * 3u + 0u];
                        const float dy = earDeltas[static_cast<std::size_t>(v) * 3u + 1u];
                        const float dz = earDeltas[static_cast<std::size_t>(v) * 3u + 2u];
                        if (dx != 0.0f || dy != 0.0f || dz != 0.0f) {
                            rawFloats[static_cast<std::size_t>(v) * 4u + 0u] += dx;
                            rawFloats[static_cast<std::size_t>(v) * 4u + 1u] += dy;
                            rawFloats[static_cast<std::size_t>(v) * 4u + 2u] += dz;
                            appliedAny = true;
                        }
                    }
                    dynData.frameCount = 0u;
                }
            }

            if (appliedAny) {
                if (auto& anim = faceNode->GetRuntimeData().animationData; anim) {
                    anim->unk217 = true;
                }
                if (auto* marker = RE::NiBooleanExtraData::Create(kEarMorphMarkerKey, true)) {
                    (void)headDyn->AddExtraData(marker);
                }
            }
        }
    }  // namespace

    Mortal3dSyncService& Mortal3dSyncService::GetSingleton() noexcept {
        static Mortal3dSyncService instance;
        return instance;
    }

    void Mortal3dSyncService::RegisterEventSink() noexcept {
        {
            std::lock_guard lock(mutex_);
            if (!objectSinkRegistered_) {
                if (auto* holder = RE::ScriptEventSourceHolder::GetSingleton()) {
                    holder->AddEventSink<RE::TESObjectLoadedEvent>(this);
                    holder->AddEventSink<RE::TESEquipEvent>(this);
                    MORWHA_LOG_INFO("Mortal3dSyncService: Registered TESObjectLoadedEvent & TESEquipEvent sinks");
                } else {
                    MORWHA_LOG_ERROR("Mortal3dSyncService: ScriptEventSourceHolder unavailable");
                }

                if (auto* niSource = SKSE::GetNiNodeUpdateEventSource()) {
                    niSource->AddEventSink(this);
                    MORWHA_LOG_INFO("Mortal3dSyncService: Registered SKSE::NiNodeUpdateEvent sink");
                } else {
                    MORWHA_LOG_WARN("Mortal3dSyncService: SKSE NiNodeUpdateEventSource unavailable");
                }

                objectSinkRegistered_ = true;
            }
        }
        RegisterInputSink();
    }

    void Mortal3dSyncService::RegisterInputSink() noexcept {
        std::lock_guard lock(mutex_);
        if (inputSinkRegistered_) {
            return;
        }
        if (auto* dm = RE::BSInputDeviceManager::GetSingleton()) {
            dm->AddEventSink(this);
            inputSinkRegistered_ = true;
            MORWHA_LOG_INFO("Mortal3dSyncService: Registered BSInputDeviceManager frame-sync sink");
        }
    }

    void Mortal3dSyncService::ClearPendingOnRevert() noexcept {
        std::lock_guard lock(mutex_);
        pendingActors_.clear();
        hasPending_.store(false, std::memory_order_release);
    }

    void Mortal3dSyncService::RegisterHydratedNpc(
        RE::FormID baseNpcFormId,
        std::string slotKey,
        Foundation::RgbColor hairColor,
        Foundation::RgbColor skinColor,
        std::optional<Foundation::RgbColor> tailColor,
        std::optional<Foundation::RgbColor> hornColor,
        std::optional<Foundation::RgbColor> earColor,
        std::shared_ptr<const std::vector<float>> earMorphDeltas) {
        if (baseNpcFormId == 0) {
            return;
        }
        std::lock_guard lock(mutex_);
        hydratedByBaseId_.insert_or_assign(
            baseNpcFormId,
            HydratedVisualEntry{
                .slotKey = std::move(slotKey),
                .hairColor = hairColor,
                .skinColor = skinColor,
                .tailColor = tailColor,
                .hornColor = hornColor,
                .earColor = earColor,
                .earMorphDeltas = std::move(earMorphDeltas)
            });
    }

    void Mortal3dSyncService::UnregisterHydratedNpcsBySlotKeys(
        const std::unordered_set<std::string>& slotKeys) {
        if (slotKeys.empty()) {
            return;
        }
        std::lock_guard lock(mutex_);
        std::erase_if(hydratedByBaseId_, [&](const auto& kv) {
            return slotKeys.contains(kv.second.slotKey);
        });
    }

    std::optional<HydratedVisualEntry> Mortal3dSyncService::LookupEntry(RE::FormID baseNpcFormId) const {
        std::lock_guard lock(mutex_);
        const auto it = hydratedByBaseId_.find(baseNpcFormId);
        if (it == hydratedByBaseId_.end()) {
            return std::nullopt;
        }
        return it->second;
    }

    void Mortal3dSyncService::QueueActor3dSync(RE::Actor* actor) {
        if (!actor) {
            return;
        }
        const auto* base = actor->GetActorBase();
        if (!base || !LookupEntry(base->GetFormID()).has_value()) {
            return;
        }

        const RE::FormID actorFormId = actor->GetFormID();
        const RE::ActorHandle handle = actor->GetHandle();
        const State::SessionEpoch epoch = State::GetSessionEpoch();

        {
            std::lock_guard lock(mutex_);
            const auto it = std::ranges::find_if(pendingActors_, [&](const PendingActorSync& entry) {
                return entry.actorFormId == actorFormId;
            });
            if (it != pendingActors_.end()) {
                it->handle = handle;
                it->epoch = epoch;
                it->retriesRemaining = kDefaultSyncRetries;
            } else {
                pendingActors_.push_back(PendingActorSync{
                    .handle = handle,
                    .actorFormId = actorFormId,
                    .epoch = epoch,
                    .retriesRemaining = kDefaultSyncRetries
                });
            }
            hasPending_.store(true, std::memory_order_release);
        }

        ScheduleMainThreadDrain();
    }

    void Mortal3dSyncService::ScheduleMainThreadDrain() noexcept {
        if (drainTaskQueued_.exchange(true, std::memory_order_acq_rel)) {
            return;
        }
        const auto* tasks = SKSE::GetTaskInterface();
        if (!tasks) {
            drainTaskQueued_.store(false, std::memory_order_release);
            return;
        }

        tasks->AddTask([this]() {
            drainTaskQueued_.store(false, std::memory_order_release);

            std::vector<PendingActorSync> due;
            {
                std::lock_guard lock(mutex_);
                due.swap(pendingActors_);
                hasPending_.store(false, std::memory_order_release);
            }

            if (due.empty()) {
                return;
            }

            auto* ui = RE::UI::GetSingleton();
            const bool gamePaused = (ui != nullptr) && ui->GameIsPaused();

            std::vector<PendingActorSync> requeue;
            requeue.reserve(due.size());

            for (auto& item : due) {
                if (!State::IsSessionEpochCurrent(item.epoch)) {
                    continue;
                }
                const auto actorPtr = item.handle.get();
                if (!actorPtr || actorPtr->IsDeleted() || actorPtr->IsDisabled()) {
                    continue;
                }

                RE::TESNPC* npc = actorPtr->GetActorBase();
                const auto entryOpt = npc ? LookupEntry(npc->GetFormID()) : std::nullopt;
                if (!entryOpt) {
                    continue;
                }

                RE::NiAVObject* root3D = actorPtr->Is3DLoaded() ? actorPtr->Get3D(false) : nullptr;
                RE::BSFaceGenNiNode* faceNode =
                    (root3D != nullptr) ? actorPtr->GetFaceNodeSkinned() : nullptr;

                if (root3D != nullptr && faceNode != nullptr && !actorPtr->IsOnMount()) {
                    applyingSync_.store(true, std::memory_order_release);
                    if (ChimeraAccessoryCatalog::GetSingleton().SyncActorChimeraSkin(actorPtr.get())) {
                        actorPtr->DoReset3D(true);
                        applyingSync_.store(false, std::memory_order_release);
                        requeue.push_back(item);
                        continue;
                    }
                    const RE::NiColor hairColor{
                        static_cast<float>(entryOpt->hairColor.r) / 128.0f,
                        static_cast<float>(entryOpt->hairColor.g) / 128.0f,
                        static_cast<float>(entryOpt->hairColor.b) / 128.0f
                    };
                    // Run base hair/skin updates before custom part tinting so
                    // Actor::UpdateSkinColor() (UpdateBodyTint) never clobbers horn or ear tints.
                    root3D->UpdateHairColor(hairColor);
                    actorPtr->UpdateSkinColor();
                    ApplyActorChimeraPartTints(
                        actorPtr.get(),
                        root3D,
                        faceNode,
                        entryOpt->tailColor,
                        entryOpt->hornColor,
                        entryOpt->earColor);
                    if (entryOpt->earMorphDeltas && !entryOpt->earMorphDeltas->empty()) {
                        ApplyActorEarMorphIfNeeded(npc, faceNode, *entryOpt->earMorphDeltas);
                    }
                    applyingSync_.store(false, std::memory_order_release);

                    const auto& accessoryCatalog = ChimeraAccessoryCatalog::GetSingleton();
                    const auto& hybridSkinSvc = HybridSkinService::GetSingleton();
                    if (npc->headParts && npc->numHeadParts > 0) {
                        for (std::int8_t i = 0; i < npc->numHeadParts; ++i) {
                            const RE::BGSHeadPart* hp = npc->headParts[i];
                            if (!hp) {
                                continue;
                            }
                            if (!hp->formEditorID.empty() &&
                                (hybridSkinSvc.IsRuntimeHybridFacePart(hp) ||
                                 accessoryCatalog.IsRuntimeBeastMouthPart(hp) ||
                                 accessoryCatalog.IsCatalogedAccessoryMouth(hp))) {
                                SkeeOverlayClient::GetSingleton().ClearHeadNodeTextureOverrides(
                                    actorPtr.get(), hp->formEditorID.c_str());
                            }
                            if (!accessoryCatalog.IsRuntimePromotedHornPart(hp)) {
                                continue;
                            }
                            const bool attached = !hp->formEditorID.empty() &&
                                                  (faceNode->GetObjectByName(hp->formEditorID) != nullptr);
                            if (attached) {
                                MORWHA_LOG_INFO(
                                    "Mortal3dSyncService: Verified promoted horn headPart '{}' ({:08X}) in FaceGen 3D for actor {:08X} (base {:08X})",
                                    hp->formEditorID.c_str(),
                                    hp->GetFormID(),
                                    actorPtr->GetFormID(),
                                    npc->GetFormID());
                            } else {
                                MORWHA_LOG_WARN(
                                    "Mortal3dSyncService: Promoted horn headPart '{}' ({:08X}) not found under FaceGen node for actor {:08X} (base {:08X})",
                                    hp->formEditorID.c_str(),
                                    hp->GetFormID(),
                                    actorPtr->GetFormID(),
                                    npc->GetFormID());
                            }
                        }
                    }

                    MORWHA_LOG_INFO(
                        "Mortal3dSyncService: Synced 3D hair & skin shaders for actor {:08X} (base {:08X}, key '{}')",
                        actorPtr->GetFormID(),
                        npc->GetFormID(),
                        entryOpt->slotKey);
                } else {
                    if (!gamePaused) {
                        if (item.retriesRemaining > 0) {
                            --item.retriesRemaining;
                        }
                        if (item.retriesRemaining == 0) {
                            MORWHA_LOG_WARN(
                                "Mortal3dSyncService: Timed out waiting for 3D FaceGen on actor {:08X}",
                                item.actorFormId);
                            continue;
                        }
                    }
                    requeue.push_back(item);
                }
            }

            if (!requeue.empty()) {
                std::lock_guard lock(mutex_);
                for (const auto& item : requeue) {
                    if (!State::IsSessionEpochCurrent(item.epoch)) {
                        continue;
                    }
                    const bool alreadyQueued = std::ranges::any_of(pendingActors_, [&](const PendingActorSync& cur) {
                        return cur.actorFormId == item.actorFormId;
                    });
                    if (!alreadyQueued) {
                        pendingActors_.push_back(item);
                    }
                }
                hasPending_.store(!pendingActors_.empty(), std::memory_order_release);
            }
        });
    }

    RE::BSEventNotifyControl Mortal3dSyncService::ProcessEvent(
        const RE::TESObjectLoadedEvent* event,
        RE::BSTEventSource<RE::TESObjectLoadedEvent>*) {
        if (!event || !event->loaded) {
            return RE::BSEventNotifyControl::kContinue;
        }
        auto* refr = RE::TESForm::LookupByID<RE::TESObjectREFR>(event->formID);
        auto* actor = refr ? refr->As<RE::Actor>() : nullptr;
        if (actor) {
            QueueActor3dSync(actor);
        }
        return RE::BSEventNotifyControl::kContinue;
    }

    RE::BSEventNotifyControl Mortal3dSyncService::ProcessEvent(
        const RE::TESEquipEvent* event,
        RE::BSTEventSource<RE::TESEquipEvent>*) {
        if (applyingSync_.load(std::memory_order_acquire) || !event || !event->actor) {
            return RE::BSEventNotifyControl::kContinue;
        }
        if (auto* actor = event->actor->As<RE::Actor>()) {
            QueueActor3dSync(actor);
        }
        return RE::BSEventNotifyControl::kContinue;
    }

    RE::BSEventNotifyControl Mortal3dSyncService::ProcessEvent(
        const SKSE::NiNodeUpdateEvent* event,
        RE::BSTEventSource<SKSE::NiNodeUpdateEvent>*) {
        if (applyingSync_.load(std::memory_order_acquire) || !event || !event->reference) {
            return RE::BSEventNotifyControl::kContinue;
        }
        if (auto* actor = event->reference->As<RE::Actor>()) {
            QueueActor3dSync(actor);
        }
        return RE::BSEventNotifyControl::kContinue;
    }

    RE::BSEventNotifyControl Mortal3dSyncService::ProcessEvent(
        RE::InputEvent* const*,
        RE::BSTEventSource<RE::InputEvent*>*) {
        if (!hasPending_.load(std::memory_order_acquire)) {
            return RE::BSEventNotifyControl::kContinue;
        }
        const auto nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                               std::chrono::steady_clock::now().time_since_epoch())
                               .count();
        const auto prevMs = lastPollMs_.load(std::memory_order_relaxed);
        if ((nowMs - prevMs) >= kPollIntervalMs) {
            lastPollMs_.store(nowMs, std::memory_order_relaxed);
            ScheduleMainThreadDrain();
        }
        return RE::BSEventNotifyControl::kContinue;
    }

}  // namespace MORWHA::Engine
