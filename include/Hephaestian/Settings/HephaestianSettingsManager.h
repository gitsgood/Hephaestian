#ifndef HEPHAESTIAN_HEPHAESTIANSETTINGSMANAGER_H
#define HEPHAESTIAN_HEPHAESTIANSETTINGSMANAGER_H

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <vector>

#include "Hephaestian/Settings/HephaestianSettings.h"

namespace Hephaestian
{

// Called whenever the settings change. Both versions are handed over, so a listener can react only to what it cares about:
//	if (NewSettings.m_ShowInfiniteGrid != PreviousSettings.m_ShowInfiniteGrid) { ... }
using SettingsChangedCallback = std::function<void(const HephaestianSettings& NewSettings, const HephaestianSettings& PreviousSettings)>;

// Where a change came from, in case a listener wants to treat them differently (e.g. a GUI not echoing its own edits back)
enum class SettingsChangeSource
{
	RuntimeModification,	// modifySettings (your future GUI, a key press, a console command...)
	SettingsFileReload		// Someone edited HephaestianSettings.json while the engine was running
};

class HephaestianSettingsManager;

/*
* Keeps a callback registered for as long as this object lives. Destroying it (or calling unsubscribe) removes the callback,
* so a listener that goes away can never be called afterwards. Safe to destroy even after the manager itself is gone.
*/
class SettingsChangeSubscription
{
public:
	SettingsChangeSubscription() = default;
	~SettingsChangeSubscription() { unsubscribe(); }

	SettingsChangeSubscription(SettingsChangeSubscription&& OtherSubscription) noexcept;
	SettingsChangeSubscription& operator=(SettingsChangeSubscription&& OtherSubscription) noexcept;
	SettingsChangeSubscription(const SettingsChangeSubscription&) = delete;
	SettingsChangeSubscription& operator=(const SettingsChangeSubscription&) = delete;

	void unsubscribe() noexcept;
	[[nodiscard]] bool isSubscribed() const noexcept;

private:
	friend class HephaestianSettingsManager;

	struct SubscriberRegistry;	// Shared between the manager and its subscriptions, so either can outlive the other
	SettingsChangeSubscription(std::weak_ptr<SubscriberRegistry> RegistryToUnsubscribeFrom, std::uint64_t SubscriptionIdentifier) noexcept;

	std::weak_ptr<SubscriberRegistry> m_SubscriberRegistry;
	std::uint64_t m_SubscriptionIdentifier{ 0 };
};

/*
* The one owner of the engine settings at runtime, kept in sync with HephaestianSettings.json both ways:
*	- modifySettings(...) changes them, notifies every subscriber, and saves the JSON (so the change persists)
*	- pollSettingsFileForExternalChanges() (call it once per frame) picks up edits made to the JSON while running,
*	  and notifies subscribers exactly the same way
*
* Everything runs on the calling thread (the main thread), so callbacks may touch the renderer, the ECS, etc. directly.
*
* Typical use:
*	auto GridSubscription = SettingsManager.subscribeToSettingsChanges([&](const HephaestianSettings& New, const HephaestianSettings&)
*	{
*		renderer.setInfiniteGridVisible(New.m_ShowInfiniteGrid);
*	});
*	...
*	SettingsManager.modifySettings([](HephaestianSettings& Settings) { Settings.m_ShowInfiniteGrid = false; });	// e.g. from a GUI checkbox
*/
class HephaestianSettingsManager
{
public:
	// Loads (or creates) HephaestianSettings.json right away
	HephaestianSettingsManager();
	~HephaestianSettingsManager();

	HephaestianSettingsManager(const HephaestianSettingsManager&) = delete;
	HephaestianSettingsManager& operator=(const HephaestianSettingsManager&) = delete;

	[[nodiscard]] const HephaestianSettings& getCurrentSettings() const noexcept { return m_CurrentSettings; }

	// Registers a callback. By default it's also called once immediately (with the current settings as both arguments),
	// so whatever it drives starts out in the right state without duplicating the "apply" code.
	[[nodiscard]] SettingsChangeSubscription subscribeToSettingsChanges(SettingsChangedCallback CallbackToRegister, bool ShouldInvokeImmediatelyWithCurrentSettings = true);

	// Edits a copy of the settings, then (only if something actually changed) applies it, notifies subscribers and saves the file.
	// Calling this from inside a callback is fine: the nested change is applied once the current notification round is over.
	void modifySettings(const std::function<void(HephaestianSettings& SettingsToModify)>& ModificationToApply);

	// Replaces every setting at once (same rules as modifySettings)
	void replaceSettings(const HephaestianSettings& NewSettings);

	// Where the last notification came from (valid inside a callback)
	[[nodiscard]] SettingsChangeSource getCurrentChangeSource() const noexcept { return m_CurrentChangeSource; }

	// Cheap to call every frame: it only looks at the file's modification time, at most every m_SettingsFilePollingInterval.
	void pollSettingsFileForExternalChanges();

	// Turn off to make runtime changes temporary (e.g. while dragging a GUI slider), then call saveSettingsToFile() when done
	void setAutomaticallySaveRuntimeChanges(bool ShouldAutomaticallySaveRuntimeChanges) noexcept { m_ShouldAutomaticallySaveRuntimeChanges = ShouldAutomaticallySaveRuntimeChanges; }
	bool saveSettingsToFile();

private:
	void applySettingsAndNotifySubscribers(const HephaestianSettings& NewSettings, SettingsChangeSource ChangeSource);
	void rememberSettingsFileModificationTime();

	HephaestianSettings m_CurrentSettings;
	std::shared_ptr<SettingsChangeSubscription::SubscriberRegistry> m_SubscriberRegistry;

	// Re-entrancy: changes requested while subscribers are being notified wait here
	bool m_IsNotifyingSubscribers{ false };
	std::vector<std::pair<HephaestianSettings, SettingsChangeSource>> m_ChangesRequestedDuringNotification;
	SettingsChangeSource m_CurrentChangeSource{ SettingsChangeSource::RuntimeModification };

	// Persistence
	bool m_ShouldAutomaticallySaveRuntimeChanges{ true };
	std::optional<std::filesystem::file_time_type> m_LastKnownSettingsFileModificationTime;
	std::chrono::steady_clock::time_point m_LastSettingsFilePollTime{};
	std::chrono::milliseconds m_SettingsFilePollingInterval{ 500 };
};

}

#endif // !HEPHAESTIAN_HEPHAESTIANSETTINGSMANAGER_H
