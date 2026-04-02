#pragma once

#if defined(_WIN32)
	#if defined(GAMEPLAY_DLL_EXPORT)
		#define GAMEPLAY_API __declspec(dllexport)
	#else
		#define GAMEPLAY_API __declspec(dllimport)
	#endif
#else
	#define GAMEPLAY_API __attribute__((visibility("default")))
#endif
