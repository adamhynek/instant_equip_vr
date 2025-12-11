#include "common/IDebugLog.h"  // IDebugLog
#include "skse64_common/skse_version.h"  // RUNTIME_VERSION
#include "skse64/PluginAPI.h"  // SKSEInterface, PluginInfo
#include "skse64/PapyrusEvents.h"
#include "skse64/GameData.h"
#include "skse64_common/SafeWrite.h"
#include "skse64/GameRTTI.h"

#include <ShlObj.h>  // CSIDL_MYDOCUMENTS
#include "version.h"
#include "config.h"

// SKSE globals
static PluginHandle	g_pluginHandle = kPluginHandle_Invalid;
static SKSEMessagingInterface *g_messaging = nullptr;

int g_numSkipAnimationFrames = 0;


bool IsBoundWeapon(TESForm *form)
{
    if (!form || !form->IsWeapon()) {
        return false;
    }
    TESObjectWEAP *weapon = DYNAMIC_CAST(form, TESForm, TESObjectWEAP);
    if (!weapon) {
        return false;
    }
    return (weapon->gameData.flags1 & TESObjectWEAP::GameData::Flags1::kFlags_BoundWeapon) != 0;
}

struct EquipActionEventHandler : ActionEventHandler
{
    virtual	EventResult	ReceiveEvent(SKSEActionEvent *evn, EventDispatcher<SKSEActionEvent> *dispatcher)
    {
        if (evn->actor != *g_thePlayer) {
            return EventResult::kEvent_Continue;
        }

        if (evn->type == SKSEActionEvent::kType_BeginDraw || evn->type == SKSEActionEvent::kType_BeginSheathe) {
            PlayerCharacter *player = *g_thePlayer;
            bool isDraw = (evn->type == SKSEActionEvent::kType_BeginDraw);
            bool ignoreBoundWeapons = isDraw ? Config::options.drawIgnoreBoundWeapons : Config::options.sheatheIgnoreBoundWeapons;
            std::vector<UInt32> &ignoreFormIDs = isDraw ? Config::options.drawIgnoreFormIDs : Config::options.sheatheIgnoreFormIDs;

            TESForm *mainHandObj = player->GetEquippedObject(false);
            if (mainHandObj) {
                if ((ignoreBoundWeapons && IsBoundWeapon(mainHandObj)) ||
                    (std::find(ignoreFormIDs.begin(), ignoreFormIDs.end(), mainHandObj->formID) != ignoreFormIDs.end()))
                {
                    return EventResult::kEvent_Continue;
                }
            }

            TESForm *offHandObj = player->GetEquippedObject(true);
            if (offHandObj) {
                if ((ignoreBoundWeapons && IsBoundWeapon(offHandObj)) ||
                    (std::find(ignoreFormIDs.begin(), ignoreFormIDs.end(), offHandObj->formID) != ignoreFormIDs.end()))
                {
                    return EventResult::kEvent_Continue;
                }
            }

            g_numSkipAnimationFrames = Config::options.numSkipAnimationFrames;
        }

        return EventResult::kEvent_Continue;
    }
};
EquipActionEventHandler equipActionHandler;


// PlayerCharacter::UpdateAnimation hook for altering the player's animation rate
typedef void(*_Actor_UpdateAnimation)(Actor *_this, float deltaTime);
_Actor_UpdateAnimation g_originalPCUpdateAnimation = nullptr;
static RelocPtr<_Actor_UpdateAnimation> PlayerCharacter_UpdateAnimation_vtbl(0x16E2618); // 0x16E2230 + 0x7D * 8
void PlayerCharacter_UpdateAnimation_Hook(Actor *_this, float deltaTime)
{
    if (g_numSkipAnimationFrames > 0) {
        --g_numSkipAnimationFrames;
        deltaTime = Config::options.skipAnimationDeltaTime;
    }
    g_originalPCUpdateAnimation(_this, deltaTime);
}


extern "C" {
    void OnDataLoaded()
    {

    }

    void OnInputLoaded()
    {
        auto actionEventDispatcher = static_cast<EventDispatcher<SKSEActionEvent> *>(g_messaging->GetEventDispatcher(SKSEMessagingInterface::kDispatcher_ActionEvent));
        actionEventDispatcher->AddEventSink(&equipActionHandler);
    }

    // Listener for SKSE Messages
    void OnSKSEMessage(SKSEMessagingInterface::Message* msg)
    {
        if (msg) {
            if (msg->type == SKSEMessagingInterface::kMessage_InputLoaded) {
                OnInputLoaded();
            }
            else if (msg->type == SKSEMessagingInterface::kMessage_DataLoaded) {
                OnDataLoaded();
            }
        }
    }

    bool SKSEPlugin_Query(const SKSEInterface* skse, PluginInfo* info)
    {
        gLog.OpenRelative(CSIDL_MYDOCUMENTS, "\\My Games\\Skyrim VR\\SKSE\\instant_equip_vr.log");
        gLog.SetPrintLevel(IDebugLog::kLevel_DebugMessage);
        gLog.SetLogLevel(IDebugLog::kLevel_DebugMessage);

        _MESSAGE("Instant Equip VR v%s", IEVR_VERSION_VERSTRING);

        info->infoVersion = PluginInfo::kInfoVersion;
        info->name = "Instant Equip VR";
        info->version = IEVR_VERSION_MAJOR;

        g_pluginHandle = skse->GetPluginHandle();

        if (skse->isEditor) {
            _FATALERROR("[FATAL ERROR] Loaded in editor, marking as incompatible!\n");
            return false;
        }
        else if (skse->runtimeVersion != RUNTIME_VR_VERSION_1_4_15) {
            _FATALERROR("[FATAL ERROR] Unsupported runtime version %08X!\n", skse->runtimeVersion);
            return false;
        }

        return true;
    }

    bool SKSEPlugin_Load(const SKSEInterface * skse)
    {	// Called by SKSE to load this plugin
        _MESSAGE("Instant Equip VR loaded");

        if (Config::ReadConfigOptions()) {
            _MESSAGE("Successfully read config parameters");
        }
        else {
            _WARNING("[WARNING] Failed to read config options. Using defaults instead.");
        }

        _MESSAGE("Registering for SKSE messages");
        g_messaging = (SKSEMessagingInterface*)skse->QueryInterface(kInterface_Messaging);
        g_messaging->RegisterListener(g_pluginHandle, "SKSE", OnSKSEMessage);

        g_originalPCUpdateAnimation = *PlayerCharacter_UpdateAnimation_vtbl;
        SafeWrite64(PlayerCharacter_UpdateAnimation_vtbl.GetUIntPtr(), uintptr_t(PlayerCharacter_UpdateAnimation_Hook));

        return true;
    }
};
