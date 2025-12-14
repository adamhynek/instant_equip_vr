#include "common/IDebugLog.h"  // IDebugLog
#include "skse64_common/skse_version.h"  // RUNTIME_VERSION
#include "skse64_common/SafeWrite.h"
#include "skse64_common/BranchTrampoline.h"
#include "skse64/PluginAPI.h"  // SKSEInterface, PluginInfo
#include "skse64/PapyrusEvents.h"
#include "skse64/GameData.h"

#include <ShlObj.h>  // CSIDL_MYDOCUMENTS
#include "version.h"
#include "config.h"

// SKSE globals
static PluginHandle	g_pluginHandle = kPluginHandle_Invalid;
static SKSEMessagingInterface *g_messaging = nullptr;
SKSETrampolineInterface *g_trampoline = nullptr;

int g_numSkipAnimationFrames = 0;


struct EquipActionEventHandler : ActionEventHandler
{
    virtual	EventResult	ReceiveEvent(SKSEActionEvent *evn, EventDispatcher<SKSEActionEvent> *dispatcher)
    {
        if (evn->actor != *g_thePlayer) {
            return EventResult::kEvent_Continue;
        }

        if (evn->type == SKSEActionEvent::kType_BeginDraw || evn->type == SKSEActionEvent::kType_BeginSheathe) {
            g_numSkipAnimationFrames = Config::options.numSkipAnimationFrames;
        }

        return EventResult::kEvent_Continue;
    }
};
EquipActionEventHandler equipActionHandler;


struct BShkbAnimationGraph_UpdateData
{
        float deltaTime; // 00
        void *unkFunctionPtr08; // function pointer to some Character function
        Character *character; // 10
        NiPoint3 *cameraPos; // 18 - literally points to the pos within the worldTransform of the WorldRoot NiCamera node
        IPostAnimationChannelUpdateFunctor *unk20; // points to the IPostAnimationChannelUpdateFunctor part of the Character
        UInt8 unk28;
        UInt8 unk29;
        UInt8 forceUpdate; // 2A
        UInt8 unk2B;
        UInt8 unk2C;
        UInt8 useGenerateJob; // 2D - if 0, call generate(). If not 0, queue up a job for it
        UInt8 doFootIK; // 2E
        UInt8 unk2F;
        float scale1; // 30 - for rabbit, 1.3f
        float scale2; // 34 - for rabbit, 1.3f
        // ...
};

typedef bool(*_IAnimationGraphManagerHolder_UpdateAnimation)(IAnimationGraphManagerHolder *a1, BShkbAnimationGraph_UpdateData *a2);
_IAnimationGraphManagerHolder_UpdateAnimation g_originalPCAnimationGraphManagerHolderUpdateAnimation = nullptr;
static RelocPtr<_IAnimationGraphManagerHolder_UpdateAnimation> PlayerCharacter_UpdateAnimation_IAnimationGraphManagerHolder_UpdateAnimation_HookLoc(0x6C5716);
bool PlayerCharacter_UpdateAnimation_IAnimationGraphManagerHolder_UpdateAnimation_Hook(IAnimationGraphManagerHolder *a1, BShkbAnimationGraph_UpdateData *a2)
{
    float originalDeltaTime = a2->deltaTime;

    if (g_numSkipAnimationFrames > 0) {
        --g_numSkipAnimationFrames;
        a2->deltaTime = Config::options.skipAnimationDeltaTime;
    }

    bool ret = g_originalPCAnimationGraphManagerHolderUpdateAnimation(a1, a2);

    a2->deltaTime = originalDeltaTime;

    return ret;
}


bool TryHook()
{
    // This should be sized to the actual amount used by your trampoline
    static const size_t TRAMPOLINE_SIZE = 16;

    if (g_trampoline) {
        void *branch = g_trampoline->AllocateFromBranchPool(g_pluginHandle, TRAMPOLINE_SIZE);
        if (!branch) {
            _ERROR("couldn't acquire branch trampoline from SKSE. this is fatal. skipping remainder of init process.");
            return false;
        }

        g_branchTrampoline.SetBase(TRAMPOLINE_SIZE, branch);

        void *local = g_trampoline->AllocateFromLocalPool(g_pluginHandle, TRAMPOLINE_SIZE);
        if (!local) {
            _ERROR("couldn't acquire codegen buffer from SKSE. this is fatal. skipping remainder of init process.");
            return false;
        }

        g_localTrampoline.SetBase(TRAMPOLINE_SIZE, local);
    }
    else {
        if (!g_branchTrampoline.Create(TRAMPOLINE_SIZE)) {
            _ERROR("couldn't create branch trampoline. this is fatal. skipping remainder of init process.");
            return false;
        }
        if (!g_localTrampoline.Create(TRAMPOLINE_SIZE, nullptr))
        {
            _ERROR("couldn't create codegen buffer. this is fatal. skipping remainder of init process.");
            return false;
        }
    }

    return true;
}

std::uintptr_t Write5Call(std::uintptr_t a_src, std::uintptr_t a_dst)
{
    const auto disp = reinterpret_cast<std::int32_t *>(a_src + 1);
    const auto nextOp = a_src + 5;
    const auto func = nextOp + *disp;

    g_branchTrampoline.Write5Call(a_src, a_dst);

    return func;
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

        g_trampoline = (SKSETrampolineInterface *)skse->QueryInterface(kInterface_Trampoline);
        if (!g_trampoline) {
            _WARNING("Couldn't get trampoline interface");
        }
        if (!TryHook()) {
            _ERROR("[CRITICAL] Failed to perform hooks");
            return false;
        }

        {
            std::uintptr_t originalFunc = Write5Call(PlayerCharacter_UpdateAnimation_IAnimationGraphManagerHolder_UpdateAnimation_HookLoc.GetUIntPtr(), uintptr_t(PlayerCharacter_UpdateAnimation_IAnimationGraphManagerHolder_UpdateAnimation_Hook));
            g_originalPCAnimationGraphManagerHolderUpdateAnimation = (_IAnimationGraphManagerHolder_UpdateAnimation)originalFunc;
        }

        return true;
    }
};
