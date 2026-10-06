#include "SaveLocation.h"
#include "Utf8.h"

#if defined(__ANDROID__)
#include <SDL2/SDL.h>
#elif defined(_WIN32)
#include <ShlObj.h>
#endif

#include <filesystem>

std::string SaveLocation::DiscoverPreferredRoot(const std::string& legacyRoot) {
#if defined(_WIN32) && !defined(__ANDROID__)
	PWSTR knownFolder = nullptr;
	const HRESULT result = SHGetKnownFolderPath(
		FOLDERID_SavedGames, KF_FLAG_CREATE, nullptr, &knownFolder);
	if (FAILED(result) || knownFolder == nullptr) {
		if (knownFolder != nullptr) {
			CoTaskMemFree(knownFolder);
		}
		return legacyRoot;
	}

	std::filesystem::path root(knownFolder);
	CoTaskMemFree(knownFolder);
	root /= L"PlantsVsZombies";
	root /= L"saves";
	return Utf8::ToString(root.u8string());
#elif defined(__ANDROID__)
	char* preferredPath = SDL_GetPrefPath("PvZ", "PlantsVsZombies");
	if (!preferredPath) {
		return legacyRoot;
	}

	std::string root(preferredPath);
	SDL_free(preferredPath);
	if (!root.empty() && (root.back() == '/' || root.back() == '\\')) {
		root.pop_back();
	}
	return root;
#else
	return legacyRoot;
#endif
}
