#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <functional>
#include <cstdint>

/// Runtime manager for the Input Action / Input Mapping system.
/// Stores action definitions (name + required modifiers) and key bindings
/// (action name → SDL keycode).  When a matching key event occurs the
/// registered C++ callbacks are invoked and the scripting dispatch hook
/// is called so that Python callbacks can fire as well.
class InputActionManager
{
public:
	/// Modifier requirement flags for an action.
	enum Modifier : uint8_t
	{
		ModNone  = 0,
		ModShift = 1 << 0,
		ModCtrl  = 1 << 1,
		ModAlt   = 1 << 2,
	};

	/// Definition of an input action (loaded from an InputAction asset).
	struct ActionDef
	{
		std::string name;
		uint8_t     requiredMods{ ModNone }; // bitmask of Modifier
	};

	/// A key binding that maps one SDL keycode to an action name.
	struct KeyBinding
	{
		std::string actionName;
		uint32_t    keycode{ 0 }; // SDL_Keycode
	};

	static InputActionManager& Instance();

	// ── Action / Binding management ────────────────────────────────────
	void addAction(const ActionDef& action);
	void addAction(const std::string& name, int requiredMods);
	void addBinding(const KeyBinding& binding);
	void addBinding(const std::string& actionName, uint32_t keycode);
	void removeAction(const std::string& name);
	void removeBinding(const std::string& actionName, uint32_t keycode);
	void clearActions();
	void clearBindings();
	void clearAll();

	const std::vector<ActionDef>&  getActions()  const { return m_actions; }
	const std::vector<KeyBinding>& getBindings() const { return m_bindings; }

	const ActionDef* findAction(const std::string& name) const;

	// ── Key event dispatch ─────────────────────────────────────────────
	/// Call from the main event loop.  Checks bindings + modifiers and
	/// fires matching callbacks.
	void handleKeyDown(uint32_t sdlKey, uint16_t sdlMod);
	void handleKeyUp(uint32_t sdlKey, uint16_t sdlMod);

	// ── C++ callback registration ──────────────────────────────────────
	using ActionCallback = std::function<void()>;
	int  registerCallback(const std::string& actionName, ActionCallback callback);
	void unregisterCallback(int callbackId);
	void clearCallbacks();

	// ── Scripting dispatch hook ────────────────────────────────────────
	/// Generic hook called whenever an action fires.
	/// The scripting layer sets this to forward to Python callbacks.
	using DispatchHook = std::function<void(const std::string& actionName, bool pressed)>;
	void setDispatchHook(DispatchHook hook) { m_dispatchHook = std::move(hook); }

private:
	InputActionManager() = default;

	void fireAction(const std::string& actionName, bool pressed);
	bool modifiersMatch(uint8_t required, uint16_t sdlMod) const;

	std::vector<ActionDef>  m_actions;
	std::vector<KeyBinding> m_bindings;

	struct CallbackEntry
	{
		int         id{ 0 };
		std::string actionName;
		ActionCallback callback;
	};
	std::vector<CallbackEntry> m_callbacks;
	int m_nextCallbackId{ 1 };

	DispatchHook m_dispatchHook;
};
