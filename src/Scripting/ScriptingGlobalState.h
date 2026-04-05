#pragma once

#include <unordered_map>
#include <string>

enum class EngineVariableType
{
	None,
	Number,
	String,
	Boolean,
	Object,
	Function
};

struct GlobalVariable
{
	EngineVariableType type;
	union
	{
		double number;
		const char* string;
		bool boolean;
		void* object;
		void (*function)();
	};
};

class ScriptingGlobalState
{
public:
	~ScriptingGlobalState() = default;
	static ScriptingGlobalState& Instance()
	{
		static ScriptingGlobalState instance;
		return instance;
	}
	bool registerVariable(const char* name, const GlobalVariable& variable)
	{
		auto result = variables.emplace(name, variable);
		return result.second; // true if inserted, false if already exists
	}
	bool setVariable(const char* name, const GlobalVariable& variable)
	{
		variables[name] = variable;
		return true;
	}
	GlobalVariable* getVariable(const char* name)
	{
		auto it = variables.find(name);
		if (it != variables.end())
		{
			return &it->second;
		}
		return nullptr;
	}
	bool removeVariable(const char* name)
	{
		return variables.erase(name) > 0;
	}
	const std::unordered_map<std::string, GlobalVariable>& getAllVariables() const
	{
		return variables;
	}
	void clear()
	{
		variables.clear();
	}
private:
	ScriptingGlobalState() = default;
	ScriptingGlobalState(const ScriptingGlobalState&) = delete;
	ScriptingGlobalState& operator=(const ScriptingGlobalState&) = delete;
	std::unordered_map<std::string, GlobalVariable> variables;
};