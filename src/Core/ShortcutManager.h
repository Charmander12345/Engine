#pragma once

#include <string>
#include <vector>
#include <functional>
#include <unordered_map>
#include <cstdint>

// Central, configurable keyboard shortcut system.
// Replaces scattered hardcoded key checks with a single registry.
class ShortcutManager
{
public:
	// Modifier flags (matchable bitmask)
	enum Mod : uint8_t
	{
		None  = 0,
		Ctrl  = 1 << 0,
		Shift = 1 << 1,
		Alt   = 1 << 2,
	};

	// A key combination: SDL keycode + modifier bitmask
	struct KeyCombo
	{
		uint32_t key{ 0 };     // SDL_Keycode
		uint8_t  mods{ 0 };    // Mod bitmask

		bool operator==(const KeyCombo& o) const { return key == o.key && mods == o.mods; }
		bool operator!=(const KeyCombo& o) const { return !(*this == o); }

		// Human-readable label (e.g. "Ctrl+Z", "Shift+F1", "DELETE")
		std::string toString() const;
	};

	// Trigger phase: some shortcuts fire on key-down, others on key-up
	enum class Phase : uint8_t { KeyDown, KeyUp };

	// A registered shortcut action
	struct Action
	{
		std::string id;              // unique identifier (e.g. "Editor.Undo")
		std::string displayName;     // shown in UI  (e.g. "Undo")
		std::string category;        // grouping     (e.g. "Editor")
		KeyCombo    defaultCombo;    // factory default binding
		KeyCombo    currentCombo;    // active binding (may differ after rebind)
		Phase       phase{ Phase::KeyDown };
		std::function<bool()> callback; // return true = consumed
	};

	static ShortcutManager& Instance();

	// Register a new shortcut.  If the id already exists, the previous entry is overwritten.
	void registerAction(const std::string& id,
		const std::string& displayName,
		const std::string& category,
		KeyCombo defaultCombo,
		Phase phase,
		std::function<bool()> callback);

	// Dispatch a key event.  Returns true if any shortcut consumed it.
	bool handleKey(uint32_t sdlKey, uint16_t sdlMod, Phase phase) const;

	// Rebind an action to a new key combo.  Returns false if the id is unknown.
	bool rebind(const std::string& id, KeyCombo newCombo);

	// Reset a single action to its default binding.
	bool resetToDefault(const std::string& id);

	// Reset ALL actions to their default bindings.
	void resetAllToDefaults();

	// Detect conflicts: returns the id of the action already bound to this combo
	// in the same phase, or empty string if no conflict.
	std::string findConflict(const KeyCombo& combo, Phase phase, const std::string& excludeId = {}) const;

	// All registered actions (ordered by insertion)
	const std::vector<Action>& getActions() const { return m_actions; }

	// Persistence: save/load as JSON to/from a file path
	bool saveToFile(const std::string& filePath) const;
	bool loadFromFile(const std::string& filePath);

	// Enable/disable (e.g. disable while capturing a new key)
	void setEnabled(bool enabled) { m_enabled = enabled; }
	bool isEnabled() const { return m_enabled; }

private:
	ShortcutManager() = default;
	~ShortcutManager() = default;
	ShortcutManager(const ShortcutManager&) = delete;
	ShortcutManager& operator=(const ShortcutManager&) = delete;

	Action* findAction(const std::string& id);
	const Action* findAction(const std::string& id) const;

	std::vector<Action> m_actions;
	std::unordered_map<std::string, size_t> m_idIndex; // id -> index in m_actions
	bool m_enabled{ true };
};
