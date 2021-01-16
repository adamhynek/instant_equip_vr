#include "common/IDebugLog.h"  // IDebugLog
#include "skse64_common/skse_version.h"  // RUNTIME_VERSION
#include "skse64/PluginAPI.h"  // SKSEInterface, PluginInfo
#include "xbyak/xbyak.h"
#include "skse64_common/BranchTrampoline.h"
#include "skse64/PapyrusEvents.h"
#include "skse64/GameData.h"

#include <ShlObj.h>  // CSIDL_MYDOCUMENTS

#include "version.h"

// SKSE globals
static PluginHandle	g_pluginHandle = kPluginHandle_Invalid;
static SKSEMessagingInterface *g_messaging = nullptr;

SKSEVRInterface *g_vrInterface = nullptr;
SKSETrampolineInterface *g_trampoline = nullptr;

// Hook stuff
uintptr_t playerGetAnimRateHookedFuncAddr = 0;

auto playerGetAnimRateHookLoc = RelocAddr<uintptr_t>(0x702FCA);
auto playerGetAnimRateHookedFunc = RelocAddr<uintptr_t>(0x784770); // GetAnimationRate

// Variables used in hook
float g_animRate = 10000.0f;
bool g_overrideAnimRate = false;


struct EquipActionEventHandler : ActionEventHandler
{
	virtual	EventResult	ReceiveEvent(SKSEActionEvent *evn, EventDispatcher<SKSEActionEvent> *dispatcher)
	{
		if (evn->actor != *g_thePlayer) {
			return EventResult::kEvent_Continue;
		}

		if (evn->type == SKSEActionEvent::kType_BeginDraw || evn->type == SKSEActionEvent::kType_BeginSheathe) {
			g_overrideAnimRate = true;
		}

		return EventResult::kEvent_Continue;
	}
};
EquipActionEventHandler equipActionHandler;


void PerformHooks()
{
	playerGetAnimRateHookedFuncAddr = playerGetAnimRateHookedFunc.GetUIntPtr();

	{
		struct Code : Xbyak::CodeGenerator {
			Code(void * buf) : Xbyak::CodeGenerator(256, buf)
			{
				Xbyak::Label jumpBack, ret;

				// Original code
				mov(rax, playerGetAnimRateHookedFuncAddr);
				call(rax);

				// Only override the animrate if the global bool is set
				mov(al, ptr[(uintptr_t)&g_overrideAnimRate]);
				test(al, al);
				jz(ret);

				// Only override anim rate for 1 frame
				mov(rax, (uintptr_t)&g_overrideAnimRate);
				mov(byte[rax], 0);

				// Override the returned animrate with out own
				mov(rax, (uintptr_t)&g_animRate);
				movss(xmm0, ptr[rax]);

				L(ret);
				// Jump back to whence we came (+ the size of the initial branch instruction)
				jmp(ptr[rip + jumpBack]);

				L(jumpBack);
				dq(playerGetAnimRateHookLoc.GetUIntPtr() + 5);
			}
		};

		void * codeBuf = g_localTrampoline.StartAlloc();
		Code code(codeBuf);
		g_localTrampoline.EndAlloc(code.getCurr());

		g_branchTrampoline.Write5Branch(playerGetAnimRateHookLoc.GetUIntPtr(), uintptr_t(code.getCode()));

		_MESSAGE("Player GetAnimRate hook complete");
	}
}


bool TryHook()
{
	// This should be sized to the actual amount used by your trampoline
	static const size_t TRAMPOLINE_SIZE = 256;

	if (g_trampoline) {
		void* branch = g_trampoline->AllocateFromBranchPool(g_pluginHandle, TRAMPOLINE_SIZE);
		if (!branch) {
			_ERROR("couldn't acquire branch trampoline from SKSE. this is fatal. skipping remainder of init process.");
			return false;
		}

		g_branchTrampoline.SetBase(TRAMPOLINE_SIZE, branch);

		void* local = g_trampoline->AllocateFromLocalPool(g_pluginHandle, TRAMPOLINE_SIZE);
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

	PerformHooks();
	return true;
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

		return true;
	}
};
