//     __ _____ _____ _____
//  __|  |   __|     |   | |  JSON for Modern C++
// |  |  |__   |  |  | | | |  version 3.12.0
// |_____|_____|_____|_|___|  https://github.com/nlohmann/json
//
// SPDX-FileCopyrightText: 2013-2026 Niels Lohmann <https://nlohmann.me>
// SPDX-License-Identifier: MIT

#pragma once

#include <type_traits> // false_type, true_type

#include <nlohmann/detail/abi_macros.hpp>
#include <nlohmann/detail/macro_scope.hpp>
#include <nlohmann/detail/meta/void_t.hpp>

NLOHMANN_JSON_NAMESPACE_BEGIN
namespace detail
{

/// Single definition of the sentinel byte that marks a key as an attribute when
/// it is routed through the map-like insert API of the attribute-aware object
/// containers (map_with_attrs, ordered_map_with_attrs). A control character is
/// used so that a well-formed UTF-8 key produced by the JSON parser (which
/// escapes all control characters) can never begin with it.
template <typename KeyType>
struct attr_key
{
    static constexpr char marker() noexcept { return '\x01'; }

    static bool is(const KeyType& k) noexcept
    {
        return !k.empty() && static_cast<unsigned char>(k[0]) ==
               static_cast<unsigned char>(marker());
    }

    /// Prepend the marker byte to a copy of the given key.
    static KeyType wrap(const KeyType& k)
    {
        KeyType res;
        res.reserve(k.size() + 1);
        res.push_back(marker());
        res.append(k);
        return res;
    }

    /// Strip the leading marker byte from a key. Assumes is(k).
    static KeyType strip(const KeyType& k)
    {
        return KeyType(k.data() + 1, k.size() - 1);
    }
};

/// True for object containers that store attributes next to their members.
/// Detected through the presence of a nested attribute_support typedef.
template <typename T, typename SFINAE = void>
struct has_attribute_support : std::false_type {};

template <typename T>
struct has_attribute_support<T, void_t<typename T::attribute_support>>
    : std::integral_constant<bool, T::attribute_support::value> {};

/// True if the given basic_json instantiation uses an attribute-aware object
/// type. Used to gate every attribute-related overload so that the behavior of
/// object types without attributes stays exactly as before.
template <typename BasicJsonType, typename SFINAE = void>
struct basic_json_has_attributes : std::false_type {};

template <typename BasicJsonType>
struct basic_json_has_attributes<BasicJsonType, void_t<typename BasicJsonType::object_t>>
    : has_attribute_support<typename BasicJsonType::object_t> {};

/// Iterator types for walking the attribute sub-container of an attribute-aware
/// object type. Object types without attribute support (the default, such as
/// std::map or ordered_map) yield void, so that the attribute API of basic_json
/// can be declared for every instantiation without changing their behavior.
template <typename ObjectType, typename SFINAE = void>
struct attr_iterators
{
    using iterator = void;
    using const_iterator = void;
};

template <typename ObjectType>
struct attr_iterators<ObjectType,
                      void_t<typename ObjectType::attr_iterator,
                             typename ObjectType::const_attr_iterator>>
{
    using iterator = typename ObjectType::attr_iterator;
    using const_iterator = typename ObjectType::const_attr_iterator;
};

}  // namespace detail
NLOHMANN_JSON_NAMESPACE_END
