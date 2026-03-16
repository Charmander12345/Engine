#include "ShortcutManager.h"

#include <SDL3/SDL.h>
#include <fstream>
#include <sstream>

// ---------------------------------------------------------------------------
// KeyCombo::toString
// ---------------------------------------------------------------------------
std::string ShortcutManager::KeyCombo::toString() const
{
	std::string result;
	if (mods & Mod::Ctrl)  result += "Ctrl+";
	if (mods & Mod::Shift) result += "Shift+";
	if (mods & Mod::Alt)   result += "Alt+";

	const char* name = SDL_GetKeyName(static_cast<SDL_Keycode>(key));
	if (name && name[0] != '\0')
		result += name;
	else
		result += "???";
	return result;
}

// ---------------------------------------------------------------------------
// Singleton
// ---------------------------------------------------------------------------
ShortcutManager& ShortcutManager::Instance()
{
	static ShortcutManager instance;
	return instance;
}

// ---------------------------------------------------------------------------
// registerAction
// ---------------------------------------------------------------------------
void ShortcutManager::registerAction(const std::string& id,
	const std::string& displayName,
	const std::string& category,
	KeyCombo defaultCombo,
	Phase phase,
	std::function<bool()> callback)
{
	auto it = m_idIndex.find(id);
	if (it != m_idIndex.end())
	{
		auto& a = m_actions[it->second];
		a.displayName  = displayName;
		a.category     = category;
		a.defaultCombo = defaultCombo;
		a.currentCombo = defaultCombo;
		a.phase        = phase;
		a.callback     = std::move(callback);
		return;
	}
	Action a;
	a.id           = id;
	a.displayName  = displayName;
	a.category     = category;
	a.defaultCombo = defaultCombo;
	a.currentCombo = defaultCombo;
	a.phase        = phase;
	a.callback     = std::move(callback);

	m_idIndex[id] = m_actions.size();
	m_actions.push_back(std::move(a));
}

// ---------------------------------------------------------------------------
// handleKey
// ---------------------------------------------------------------------------
bool ShortcutManager::handleKey(uint32_t sdlKey, uint16_t sdlMod, Phase phase) const
{
	if (!m_enabled)
		return false;

	// Convert SDL mod flags to our bitmask
	uint8_t mods = 0;
	if (sdlMod & SDL_KMOD_CTRL)  mods |= Mod::Ctrl;
	if (sdlMod & SDL_KMOD_SHIFT) mods |= Mod::Shift;
	if (sdlMod & SDL_KMOD_ALT)   mods |= Mod::Alt;

	for (auto& a : m_actions)
	{
		if (a.phase != phase)
			continue;
		if (a.currentCombo.key != sdlKey)
			continue;
		if (a.currentCombo.mods != mods)
			continue;
		if (a.callback && a.callback())
			return true;
	}
	return false;
}

// ---------------------------------------------------------------------------
// rebind
// ---------------------------------------------------------------------------
bool ShortcutManager::rebind(const std::string& id, KeyCombo newCombo)
{
	Action* a = findAction(id);
	if (!a) return false;
	a->currentCombo = newCombo;
	return true;
}

bool ShortcutManager::resetToDefault(const std::string& id)
{
	Action* a = findAction(id);
	if (!a) return false;
	a->currentCombo = a->defaultCombo;
	return true;
}

void ShortcutManager::resetAllToDefaults()
{
	for (auto& a : m_actions)
		a.currentCombo = a.defaultCombo;
}

// ---------------------------------------------------------------------------
// findConflict
// ---------------------------------------------------------------------------
std::string ShortcutManager::findConflict(const KeyCombo& combo, Phase phase, const std::string& excludeId) const
{
	for (auto& a : m_actions)
	{
		if (a.id == excludeId) continue;
		if (a.phase != phase) continue;
		if (a.currentCombo == combo)
			return a.id;
	}
	return {};
}

// ---------------------------------------------------------------------------
// Persistence
// ---------------------------------------------------------------------------
bool ShortcutManager::saveToFile(const std::string& filePath) const
{
	std::ofstream file(filePath);
	if (!file.is_open()) return false;

	// Simple text format: one line per customized binding
	// Format: id key mods
	for (auto& a : m_actions)
	{
		if (a.currentCombo != a.defaultCombo)
		{
			file << a.id << " " << a.currentCombo.key << " " << static_cast<int>(a.currentCombo.mods) << "\n";
		}
	}
	return true;
}

bool ShortcutManager::loadFromFile(const std::string& filePath)
{
	std::ifstream file(filePath);
	if (!file.is_open()) return false;

	std::string line;
	while (std::getline(file, line))
	{
		if (line.empty()) continue;
		std::istringstream iss(line);
		std::string id;
		uint32_t key = 0;
		int mods = 0;
		if (!(iss >> id >> key >> mods)) continue;

		Action* a = findAction(id);
		if (!a) continue;
		a->currentCombo.key = key;
		a->currentCombo.mods = static_cast<uint8_t>(mods);
	}
	return true;
}

// ---------------------------------------------------------------------------
// findAction
// ---------------------------------------------------------------------------
ShortcutManager::Action* ShortcutManager::findAction(const std::string& id)
{
	auto it = m_idIndex.find(id);
	if (it == m_idIndex.end()) return nullptr;
	return &m_actions[it->second];
}

const ShortcutManager::Action* ShortcutManager::findAction(const std::string& id) const
{
	auto it = m_idIndex.find(id);
	if (it == m_idIndex.end()) return nullptr;
	return &m_actions[it->second];
}
