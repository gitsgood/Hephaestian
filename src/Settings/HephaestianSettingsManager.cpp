#include "Hephaestian/Settings/HephaestianSettingsManager.h"

#include <utility>

#include <Vulkanised/Logger.h>

namespace Hephaestian
{

// =====================================================================================================================
// SUBSCRIPTIONS
// =====================================================================================================================
struct SettingsChangeSubscription::SubscriberRegistry
{
	std::map<std::uint64_t, SettingsChangedCallback> m_CallbacksBySubscriptionIdentifier;	// Ordered, so subscribers are notified in subscription order
	std::uint64_t m_NextSubscriptionIdentifier{ 1 };
};

SettingsChangeSubscription::SettingsChangeSubscription(std::weak_ptr<SubscriberRegistry> RegistryToUnsubscribeFrom, std::uint64_t SubscriptionIdentifier) noexcept
	: m_SubscriberRegistry(std::move(RegistryToUnsubscribeFrom))
	, m_SubscriptionIdentifier(SubscriptionIdentifier)
{
}

SettingsChangeSubscription::SettingsChangeSubscription(SettingsChangeSubscription&& OtherSubscription) noexcept
	: m_SubscriberRegistry(std::move(OtherSubscription.m_SubscriberRegistry))
	, m_SubscriptionIdentifier(std::exchange(OtherSubscription.m_SubscriptionIdentifier, 0))
{
}

SettingsChangeSubscription& SettingsChangeSubscription::operator=(SettingsChangeSubscription&& OtherSubscription) noexcept
{
	if (this != &OtherSubscription)
	{
		unsubscribe();
		m_SubscriberRegistry = std::move(OtherSubscription.m_SubscriberRegistry);
		m_SubscriptionIdentifier = std::exchange(OtherSubscription.m_SubscriptionIdentifier, 0);
	}
	return *this;
}

void SettingsChangeSubscription::unsubscribe() noexcept
{
	// If the manager is already gone, the weak_ptr is expired and there's simply nothing to remove
	if (const std::shared_ptr<SubscriberRegistry> LockedSubscriberRegistry = m_SubscriberRegistry.lock())
	{
		LockedSubscriberRegistry->m_CallbacksBySubscriptionIdentifier.erase(m_SubscriptionIdentifier);
	}
	m_SubscriberRegistry.reset();
	m_SubscriptionIdentifier = 0;
}

bool SettingsChangeSubscription::isSubscribed() const noexcept
{
	const std::shared_ptr<SubscriberRegistry> LockedSubscriberRegistry = m_SubscriberRegistry.lock();
	return LockedSubscriberRegistry && LockedSubscriberRegistry->m_CallbacksBySubscriptionIdentifier.contains(m_SubscriptionIdentifier);
}

// =====================================================================================================================
// MANAGER
// =====================================================================================================================
HephaestianSettingsManager::HephaestianSettingsManager()
	: m_CurrentSettings(loadHephaestianSettings())
	, m_SubscriberRegistry(std::make_shared<SettingsChangeSubscription::SubscriberRegistry>())
{
	rememberSettingsFileModificationTime();
	m_LastSettingsFilePollTime = std::chrono::steady_clock::now();
}

HephaestianSettingsManager::~HephaestianSettingsManager() = default;

SettingsChangeSubscription HephaestianSettingsManager::subscribeToSettingsChanges(SettingsChangedCallback CallbackToRegister, bool ShouldInvokeImmediatelyWithCurrentSettings)
{
	if (!CallbackToRegister)
	{
		LOGW("Tried to subscribe an empty callback to settings changes, ignoring it.");
		return {};
	}

	const std::uint64_t NewSubscriptionIdentifier = m_SubscriberRegistry->m_NextSubscriptionIdentifier++;
	m_SubscriberRegistry->m_CallbacksBySubscriptionIdentifier.emplace(NewSubscriptionIdentifier, CallbackToRegister);

	if (ShouldInvokeImmediatelyWithCurrentSettings)
	{
		CallbackToRegister(m_CurrentSettings, m_CurrentSettings);
	}

	return SettingsChangeSubscription(m_SubscriberRegistry, NewSubscriptionIdentifier);
}

void HephaestianSettingsManager::modifySettings(const std::function<void(HephaestianSettings& SettingsToModify)>& ModificationToApply)
{
	// Build on top of any change that's still waiting, so two queued modifications don't overwrite each other
	HephaestianSettings ModifiedSettings = m_ChangesRequestedDuringNotification.empty() ? m_CurrentSettings : m_ChangesRequestedDuringNotification.back().first;
	ModificationToApply(ModifiedSettings);
	applySettingsAndNotifySubscribers(ModifiedSettings, SettingsChangeSource::RuntimeModification);
}

void HephaestianSettingsManager::replaceSettings(const HephaestianSettings& NewSettings)
{
	applySettingsAndNotifySubscribers(NewSettings, SettingsChangeSource::RuntimeModification);
}

void HephaestianSettingsManager::applySettingsAndNotifySubscribers(const HephaestianSettings& NewSettings, SettingsChangeSource ChangeSource)
{
	// A callback changed the settings while we're still telling everyone about the previous change: do it right after
	if (m_IsNotifyingSubscribers)
	{
		m_ChangesRequestedDuringNotification.emplace_back(NewSettings, ChangeSource);
		return;
	}

	if (NewSettings == m_CurrentSettings)
	{
		return;	// Nothing changed (e.g. a checkbox clicked twice, or the file saved without edits): nobody needs to hear about it
	}

	const HephaestianSettings PreviousSettings = std::exchange(m_CurrentSettings, NewSettings);
	m_CurrentChangeSource = ChangeSource;

	// Copy the identifiers first: callbacks are allowed to subscribe/unsubscribe while we iterate
	std::vector<std::uint64_t> SubscriptionIdentifiersToNotify;
	SubscriptionIdentifiersToNotify.reserve(m_SubscriberRegistry->m_CallbacksBySubscriptionIdentifier.size());
	for (const auto& [CurrentSubscriptionIdentifier, CurrentCallback] : m_SubscriberRegistry->m_CallbacksBySubscriptionIdentifier)
	{
		SubscriptionIdentifiersToNotify.push_back(CurrentSubscriptionIdentifier);
	}

	m_IsNotifyingSubscribers = true;
	for (const std::uint64_t CurrentSubscriptionIdentifier : SubscriptionIdentifiersToNotify)
	{
		const auto CallbackEntry = m_SubscriberRegistry->m_CallbacksBySubscriptionIdentifier.find(CurrentSubscriptionIdentifier);
		if (CallbackEntry == m_SubscriberRegistry->m_CallbacksBySubscriptionIdentifier.end())
		{
			continue;	// Unsubscribed by an earlier callback in this same round
		}

		// Copied so the callback can unsubscribe itself without destroying the std::function it's running from
		const SettingsChangedCallback CallbackToInvoke = CallbackEntry->second;
		try
		{
			CallbackToInvoke(m_CurrentSettings, PreviousSettings);
		}
		catch (const std::exception& CallbackException)
		{
			// One broken listener shouldn't stop everyone else from hearing about the change
			LOGE("A settings change callback threw: {}", CallbackException.what());
		}
	}
	m_IsNotifyingSubscribers = false;

	// Runtime changes persist. Changes that came FROM the file obviously don't need writing back to it.
	if (ChangeSource == SettingsChangeSource::RuntimeModification && m_ShouldAutomaticallySaveRuntimeChanges)
	{
		saveSettingsToFile();
	}

	// Anything requested by the callbacks above, in the order it was asked for
	while (!m_ChangesRequestedDuringNotification.empty())
	{
		const auto [QueuedSettings, QueuedChangeSource] = m_ChangesRequestedDuringNotification.front();
		m_ChangesRequestedDuringNotification.erase(m_ChangesRequestedDuringNotification.begin());
		applySettingsAndNotifySubscribers(QueuedSettings, QueuedChangeSource);
	}
}

bool HephaestianSettingsManager::saveSettingsToFile()
{
	const bool WasSaveSuccessful = saveHephaestianSettings(m_CurrentSettings);

	// Our own save bumps the file's modification time: remember it, or the poll below would "reload" what we just wrote
	rememberSettingsFileModificationTime();
	return WasSaveSuccessful;
}

void HephaestianSettingsManager::pollSettingsFileForExternalChanges()
{
	const std::chrono::steady_clock::time_point CurrentTime = std::chrono::steady_clock::now();
	if (CurrentTime - m_LastSettingsFilePollTime < m_SettingsFilePollingInterval)
	{
		return;
	}
	m_LastSettingsFilePollTime = CurrentTime;

	std::error_code ModificationTimeError;
	const std::filesystem::file_time_type CurrentModificationTime = std::filesystem::last_write_time(getHephaestianSettingsFilePath(), ModificationTimeError);
	if (ModificationTimeError || (m_LastKnownSettingsFileModificationTime.has_value() && CurrentModificationTime == m_LastKnownSettingsFileModificationTime.value()))
	{
		return;	// Unchanged, or the file is momentarily missing (some editors delete and recreate on save)
	}
	m_LastKnownSettingsFileModificationTime = CurrentModificationTime;

	// A half-written or mistyped file must not reset everything to defaults: keep the current settings until it parses again
	const std::optional<HephaestianSettings> ReloadedSettings = tryReadHephaestianSettingsFile();
	if (!ReloadedSettings.has_value())
	{
		LOGW("HephaestianSettings.json changed but couldn't be read, keeping the current settings.");
		return;
	}

	LOG("HephaestianSettings.json changed on disk, applying it.");
	applySettingsAndNotifySubscribers(ReloadedSettings.value(), SettingsChangeSource::SettingsFileReload);
}

void HephaestianSettingsManager::rememberSettingsFileModificationTime()
{
	std::error_code ModificationTimeError;
	const std::filesystem::file_time_type CurrentModificationTime = std::filesystem::last_write_time(getHephaestianSettingsFilePath(), ModificationTimeError);
	if (!ModificationTimeError)
	{
		m_LastKnownSettingsFileModificationTime = CurrentModificationTime;
	}
}

}
