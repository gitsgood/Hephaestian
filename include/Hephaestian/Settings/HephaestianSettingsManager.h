/**
 * @file HephaestianSettingsManager.h
 * @brief The runtime owner of the engine settings: changes, change notifications, saving, and hot reloading of
 * HephaestianSettings.json.
 */
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

/**
 * @brief Called whenever the settings change.
 *
 * Both versions are handed over, so a listener can react only to what it cares about:
 * @code
 * if (NewSettings.m_ShowInfiniteGrid != PreviousSettings.m_ShowInfiniteGrid) { ... }
 * @endcode
 */
using SettingsChangedCallback = std::function<void(const HephaestianSettings& NewSettings, const HephaestianSettings& PreviousSettings)>;

/**
 * @brief Where a change came from, in case a listener wants to treat them differently (e.g. a GUI not echoing its own
 * edits back).
 */
enum class SettingsChangeSource
{
	RuntimeModification,	///< HephaestianSettingsManager::modifySettings (the settings widget, a key press, a console command...)
	SettingsFileReload		///< Someone edited HephaestianSettings.json while the engine was running
};

class HephaestianSettingsManager;

/**
 * @brief Keeps a callback registered for as long as this object lives.
 *
 * Destroying it (or calling unsubscribe) removes the callback, so a listener that goes away can never be called
 * afterwards. Safe to destroy even after the manager itself is gone. Made by
 * HephaestianSettingsManager::subscribeToSettingsChanges.
 */
class SettingsChangeSubscription
{
public:
	/** @brief An empty subscription (subscribed to nothing). */
	SettingsChangeSubscription() = default;
	/** @brief Unsubscribes. */
	~SettingsChangeSubscription() { unsubscribe(); }

	/**
	 * @brief Takes over another subscription (which is left empty).
	 * @param OtherSubscription The subscription to take over.
	 */
	SettingsChangeSubscription(SettingsChangeSubscription&& OtherSubscription) noexcept;
	/**
	 * @brief Unsubscribes this one, then takes over another subscription (which is left empty).
	 * @param OtherSubscription The subscription to take over.
	 * @return This subscription.
	 */
	SettingsChangeSubscription& operator=(SettingsChangeSubscription&& OtherSubscription) noexcept;
	SettingsChangeSubscription(const SettingsChangeSubscription&) = delete;
	SettingsChangeSubscription& operator=(const SettingsChangeSubscription&) = delete;

	/** @brief Removes the callback now (does nothing if it's already gone, or if the manager no longer exists). */
	void unsubscribe() noexcept;
	/**
	 * @brief Whether the callback is still registered.
	 * @return False once unsubscribed, or once the manager is gone.
	 */
	[[nodiscard]] bool isSubscribed() const noexcept;

private:
	friend class HephaestianSettingsManager;

	struct SubscriberRegistry;	///< Shared between the manager and its subscriptions, so either can outlive the other

	/**
	 * @brief Made by the manager only.
	 * @param RegistryToUnsubscribeFrom The manager's callbacks (weak: the manager may go first).
	 * @param SubscriptionIdentifier This callback's key in the registry.
	 */
	SettingsChangeSubscription(std::weak_ptr<SubscriberRegistry> RegistryToUnsubscribeFrom, std::uint64_t SubscriptionIdentifier) noexcept;

	std::weak_ptr<SubscriberRegistry> m_SubscriberRegistry;	///< The manager's callbacks, expired once the manager is gone
	std::uint64_t m_SubscriptionIdentifier{ 0 };				///< This callback's key in the registry (0 = not subscribed)
};

/**
 * @brief The one owner of the engine settings at runtime, kept in sync with HephaestianSettings.json both ways.
 *
 * - modifySettings(...) changes them, notifies every subscriber, and saves the JSON (so the change persists).
 * - pollSettingsFileForExternalChanges() (call it once per frame) picks up edits made to the JSON while running, and
 *   notifies subscribers exactly the same way.
 *
 * Everything runs on the calling thread (the main thread), so callbacks may touch the renderer, the ECS, etc. directly.
 *
 * Typical use:
 * @code
 * auto GridSubscription = SettingsManager.subscribeToSettingsChanges([&](const HephaestianSettings& New, const HephaestianSettings&)
 * {
 *     renderer.setInfiniteGridVisible(New.m_ShowInfiniteGrid);
 * });
 * ...
 * SettingsManager.modifySettings([](HephaestianSettings& Settings) { Settings.m_ShowInfiniteGrid = false; });	// e.g. from a GUI checkbox
 * @endcode
 */
class HephaestianSettingsManager
{
public:
	/** @brief Loads (or creates) HephaestianSettings.json right away. */
	HephaestianSettingsManager();
	/** @brief Every subscription still alive simply stops being called. */
	~HephaestianSettingsManager();

	HephaestianSettingsManager(const HephaestianSettingsManager&) = delete;
	HephaestianSettingsManager& operator=(const HephaestianSettingsManager&) = delete;

	/**
	 * @brief The settings as they are now.
	 * @return A reference that stays valid, and changes, as the settings do.
	 */
	[[nodiscard]] const HephaestianSettings& getCurrentSettings() const noexcept { return m_CurrentSettings; }

	/**
	 * @brief Registers a callback for every future change.
	 * @param CallbackToRegister Called with the new and the previous settings.
	 * @param ShouldInvokeImmediatelyWithCurrentSettings Also calls it once right now (with the current settings as both
	 * arguments), so whatever it drives starts out in the right state without duplicating the "apply" code.
	 * @return The subscription: the callback stays registered for as long as it lives.
	 */
	[[nodiscard]] SettingsChangeSubscription subscribeToSettingsChanges(SettingsChangedCallback CallbackToRegister, bool ShouldInvokeImmediatelyWithCurrentSettings = true);

	/**
	 * @brief Edits a copy of the settings, then (only if something actually changed) applies it, notifies subscribers
	 * and saves the file.
	 *
	 * Calling this from inside a callback is fine: the nested change is applied once the current notification round is over.
	 * @param ModificationToApply Changes whatever it wants in the copy it's given.
	 */
	void modifySettings(const std::function<void(HephaestianSettings& SettingsToModify)>& ModificationToApply);

	/**
	 * @brief Replaces every setting at once (same rules as modifySettings).
	 * @param NewSettings The settings to switch to.
	 */
	void replaceSettings(const HephaestianSettings& NewSettings);

	/**
	 * @brief Where the change being notified came from.
	 * @return The source of the current (or last) notification. Meaningful inside a callback.
	 */
	[[nodiscard]] SettingsChangeSource getCurrentChangeSource() const noexcept { return m_CurrentChangeSource; }

	/**
	 * @brief Picks up edits made to HephaestianSettings.json while running, and notifies subscribers like any change.
	 *
	 * Cheap to call every frame: it only looks at the file's modification time, at most every
	 * m_SettingsFilePollingInterval. A file that doesn't parse (half saved, typo) is ignored until it does.
	 */
	void pollSettingsFileForExternalChanges();

	/**
	 * @brief Whether runtime changes are saved to the file straight away.
	 * @param ShouldAutomaticallySaveRuntimeChanges False makes changes temporary (e.g. while dragging a GUI slider):
	 * call saveSettingsToFile() when done.
	 */
	void setAutomaticallySaveRuntimeChanges(bool ShouldAutomaticallySaveRuntimeChanges) noexcept { m_ShouldAutomaticallySaveRuntimeChanges = ShouldAutomaticallySaveRuntimeChanges; }
	/**
	 * @brief Writes the current settings to HephaestianSettings.json (without the hot reload picking its own save up).
	 * @return False if the file couldn't be written (logged).
	 */
	bool saveSettingsToFile();

private:
	/**
	 * @brief Applies new settings and calls every subscriber, or queues them if a notification is already under way.
	 * @param NewSettings The settings to switch to.
	 * @param ChangeSource Where they came from (reported by getCurrentChangeSource while notifying).
	 */
	void applySettingsAndNotifySubscribers(const HephaestianSettings& NewSettings, SettingsChangeSource ChangeSource);
	/** @brief Notes the file's current modification time, so the next poll doesn't mistake our own save for an edit. */
	void rememberSettingsFileModificationTime();

	HephaestianSettings m_CurrentSettings;	///< The settings as they are now
	std::shared_ptr<SettingsChangeSubscription::SubscriberRegistry> m_SubscriberRegistry;	///< Every registered callback (shared with the subscriptions)

	// - Re-entrancy: changes requested while subscribers are being notified wait here
	bool m_IsNotifyingSubscribers{ false };	///< True while callbacks are being called
	std::vector<std::pair<HephaestianSettings, SettingsChangeSource>> m_ChangesRequestedDuringNotification;	///< Applied in order once the current round is over
	SettingsChangeSource m_CurrentChangeSource{ SettingsChangeSource::RuntimeModification };	///< See getCurrentChangeSource

	// - Persistence
	bool m_ShouldAutomaticallySaveRuntimeChanges{ true };	///< See setAutomaticallySaveRuntimeChanges
	std::optional<std::filesystem::file_time_type> m_LastKnownSettingsFileModificationTime;	///< The file's time when last read or saved (none if it couldn't be read)
	std::chrono::steady_clock::time_point m_LastSettingsFilePollTime{};	///< When the file was last checked
	std::chrono::milliseconds m_SettingsFilePollingInterval{ 500 };		///< How often the file is checked at most
};

}

#endif // !HEPHAESTIAN_HEPHAESTIANSETTINGSMANAGER_H
