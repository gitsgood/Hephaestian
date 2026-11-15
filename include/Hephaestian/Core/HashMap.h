/**
 * @file HashMap.h
 * @brief Hephaestian's hash maps and sets: ankerl::unordered_dense instead of std::unordered_map / std::unordered_set.
 *
 * Every entry lives packed in one array, with a small table of buckets pointing into it: no allocation per entry,
 * lookups that touch one or two cache lines, and iterating is walking a plain array.
 *
 * The price, compared to the standard containers:
 * - **Inserting** can move every entry (the array grows like a std::vector).
 * - **Erasing** moves the last entry into the erased one's place (that's how the array stays free of holes).
 *
 * So never keep a reference, pointer or iterator into a map across an insert or erase. When erasing while iterating,
 * carry on from the iterator erase() returns (it's the moved-in entry, which still needs looking at): never ++ past it.
 * std::erase_if on these containers already does this right (it walks back to front).
 *
 * std::string keys can be looked up with a std::string_view or a const char* without building a std::string first.
 */
#ifndef HEPHAESTIAN_HASHMAP_H
#define HEPHAESTIAN_HASHMAP_H

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <type_traits>

#include <ankerl/unordered_dense.h>

namespace Hephaestian
{

/**
 * @brief Implementation details of HashMap and HashSet: which hash and equality each key type gets.
 */
namespace HashMapDetail
{
	/**
	 * @brief Hashes every kind of string the same way, so a std::string key can be found from a std::string_view
	 * (no allocation).
	 */
	struct TransparentStringHash
	{
		using is_transparent = void;	///< Lets find, contains and erase take any string type
		using is_avalanching = void;	///< Already well mixed: unordered_dense doesn't need to scramble it again

		/**
		 * @brief Hashes a string.
		 * @param Text The characters to hash (a std::string or const char* converts to this for free).
		 * @return The 64 bit hash, the same for every string type holding the same characters.
		 */
		[[nodiscard]] std::uint64_t operator()(std::string_view Text) const noexcept
		{
			return ankerl::unordered_dense::hash<std::string_view>{}(Text);
		}
	};

	/**
	 * @brief Whether a key type is std::string (those get the transparent hash and equality).
	 * @tparam KeyType The map's key type.
	 */
	template <typename KeyType>
	inline constexpr bool c_IsStringKey = std::is_same_v<KeyType, std::string>;

	/**
	 * @brief The hash a HashMap or HashSet uses for a key type: TransparentStringHash for std::string, unordered_dense's
	 * own (fast, well mixed) hash for everything else.
	 * @tparam KeyType The map's key type.
	 */
	template <typename KeyType>
	using HashFor = std::conditional_t<c_IsStringKey<KeyType>, TransparentStringHash, ankerl::unordered_dense::hash<KeyType>>;

	/**
	 * @brief The key comparison a HashMap or HashSet uses: transparent (std::equal_to<>) for std::string, so any string
	 * type can be compared to the keys.
	 * @tparam KeyType The map's key type.
	 */
	template <typename KeyType>
	using EqualityFor = std::conditional_t<c_IsStringKey<KeyType>, std::equal_to<>, std::equal_to<KeyType>>;
}

/**
 * @brief The engine's hash map: same interface as std::unordered_map, much faster (see the file description for what
 * moving entries means).
 * @tparam KeyType The key type (std::string keys can be looked up with any string type).
 * @tparam ValueType The mapped type.
 */
template <typename KeyType, typename ValueType>
using HashMap = ankerl::unordered_dense::map<KeyType, ValueType, HashMapDetail::HashFor<KeyType>, HashMapDetail::EqualityFor<KeyType>>;

/**
 * @brief The engine's hash set: same interface as std::unordered_set, much faster (see the file description for what
 * moving entries means).
 * @tparam KeyType The element type (std::string elements can be looked up with any string type).
 */
template <typename KeyType>
using HashSet = ankerl::unordered_dense::set<KeyType, HashMapDetail::HashFor<KeyType>, HashMapDetail::EqualityFor<KeyType>>;

}

#endif // !HEPHAESTIAN_HASHMAP_H
