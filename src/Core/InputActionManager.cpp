#include "InputActionManager.h"
#include <SDL3/SDL.h>
#include <algorithm>

InputActionManager& InputActionManager::Instance()
{
	static InputActionManager instance;
	return instance;
}

// ── Action / Binding management ────────────────────────────────────────────

void InputActionManager::addAction(const ActionDef& action)
{
	for (auto& a : m_actions)
	{
		if (a.name == action.name)
		{
			a.requiredMods = action.requiredMods;
			return;
		}
	}
	m_actions.push_back(action);
}

void InputActionManager::addAction(const std::string& name, int requiredMods)
{
	addAction({ name, static_cast<uint8_t>(requiredMods) });
}

void InputActionManager::addBinding(const KeyBinding& binding)
{
	for (auto& b : m_bindings)
	{
		if (b.actionName == binding.actionName && b.keycode == binding.keycode)
			return; // already exists
	}
	m_bindings.push_back(binding);
}

void InputActionManager::addBinding(const std::string& actionName, uint32_t keycode)
{
	addBinding({ actionName, keycode });
}

void InputActionManager::removeAction(const std::string& name)
{
	m_actions.erase(
		std::remove_if(m_actions.begin(), m_actions.end(),
			[&](const ActionDef& a) { return a.name == name; }),
		m_actions.end());
}

void InputActionManager::removeBinding(const std::string& actionName, uint32_t keycode)
{
	m_bindings.erase(
		std::remove_if(m_bindings.begin(), m_bindings.end(),
			[&](const KeyBinding& b) { return b.actionName == actionName && b.keycode == keycode; }),
		m_bindings.end());
}

void InputActionManager::clearActions()  { m_actions.clear(); }
void InputActionManager::clearBindings() { m_bindings.clear(); }
void InputActionManager::clearAll()
{
	m_actions.clear();
	m_bindings.clear();
	m_callbacks.clear();
	// Note: dispatch hook is NOT cleared here — it is set once by the
	// scripting layer and should survive project reloads.
}

const InputActionManager::ActionDef* InputActionManager::findAction(const std::string& name) const
{
	for (const auto& a : m_actions)
	{
		if (a.name == name)
			return &a;
	}
	return nullptr;
}

// ── Callback registration ──────────────────────────────────────────────────

int InputActionManager::registerCallback(const std::string& actionName, ActionCallback callback)
{
	int id = m_nextCallbackId++;
	m_callbacks.push_back({ id, actionName, std::move(callback) });
	return id;
}

void InputActionManager::unregisterCallback(int callbackId)
{
	m_callbacks.erase(
		std::remove_if(m_callbacks.begin(), m_callbacks.end(),
			[&](const CallbackEntry& e) { return e.id == callbackId; }),
		m_callbacks.end());
}

void InputActionManager::clearCallbacks() { m_callbacks.clear(); }

// ── Key dispatch ───────────────────────────────────────────────────────────

bool InputActionManager::modifiersMatch(uint8_t required, uint16_t sdlMod) const
{
	if ((required & ModShift) && !(sdlMod & SDL_KMOD_SHIFT)) return false;
	if ((required & ModCtrl)  && !(sdlMod & SDL_KMOD_CTRL))  return false;
	if ((required & ModAlt)   && !(sdlMod & SDL_KMOD_ALT))   return false;
	return true;
}

void InputActionManager::fireAction(const std::string& actionName, bool pressed)
{
	// C++ callbacks (only on pressed for now, can be extended)
	if (pressed)
	{
		for (const auto& cb : m_callbacks)
		{
			if (cb.actionName == actionName && cb.callback)
				cb.callback();
		}
	}

	// Scripting dispatch hook
	if (m_dispatchHook)
		m_dispatchHook(actionName, pressed);
}

void InputActionManager::handleKeyDown(uint32_t sdlKey, uint16_t sdlMod)
{
	for (const auto& binding : m_bindings)
	{
		if (binding.keycode != sdlKey)
			continue;

		const ActionDef* action = findAction(binding.actionName);
		if (!action)
			continue;

		if (!modifiersMatch(action->requiredMods, sdlMod))
			continue;

		fireAction(action->name, true);
	}
}

void InputActionManager::handleKeyUp(uint32_t sdlKey, uint16_t sdlMod)
{
	for (const auto& binding : m_bindings)
	{
		if (binding.keycode != sdlKey)
			continue;

		const ActionDef* action = findAction(binding.actionName);
		if (!action)
			continue;

		// For key-up we don't check modifiers (they may have been released)
		fireAction(action->name, false);
	}
}
