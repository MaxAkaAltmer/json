//     __ _____ _____ _____
//  __|  |   __|     |   | |  JSON for Modern C++
// |  |  |__   |  |  | | | |  version 3.12.0
// |_____|_____|_____|_|___|  https://github.com/nlohmann/json
//
// SPDX-FileCopyrightText: 2013-2026 Niels Lohmann <https://nlohmann.me>
// SPDX-License-Identifier: MIT

#pragma once

#include <cstddef> // size_t, ptrdiff_t
#include <functional> // less
#include <initializer_list> // initializer_list
#include <iterator> // next, distance
#include <memory> // allocator
#include <type_traits> // is_nothrow_move_assignable, is_nothrow_move_constructible
#include <utility> // forward, move, pair, swap

#include <nlohmann/detail/abi_macros.hpp>
#include <nlohmann/detail/attr_traits.hpp> // detail::attr_key
#include <nlohmann/detail/macro_scope.hpp>
#include <nlohmann/detail/meta/type_traits.hpp>
#include <nlohmann/ordered_map.hpp> // ordered_map

#if JSON_HAS_THREE_WAY_COMPARISON
#include <compare> // std::partial_ordering
#endif

NLOHMANN_JSON_NAMESPACE_BEGIN

/// ordered_map_with_attrs: an insertion-order-preserving map-like container
/// that additionally stores a disjoint set of "attributes" (name/primitive
/// pairs). Member iteration and all standard associative-container operations
/// (find, at, operator[], begin, size, ...) observe the members storage only,
/// whereas attributes live in a separate ordered storage reachable through the
/// attr_* API. Keys carrying the detail::attr_key sentinel byte are routed to
/// the attribute storage by the mutating member functions.
///
/// Exposing capacity()/reserve() (forwarded from the members storage) also
/// makes detail::is_ordered_map<ordered_map_with_attrs> hold, so the library
/// treats it as an order-preserving object type.
///
/// Intended for use as the ObjectType template parameter of basic_json.
template <class Key, class T, class IgnoredLess = std::less<Key>,
          class Allocator = std::allocator<std::pair<const Key, T>>>
class ordered_map_with_attrs
{
  public:
    using key_type        = Key;
    using mapped_type     = T;
    using value_type      = std::pair<const Key, T>;
    using size_type       = std::size_t;
    using difference_type = std::ptrdiff_t;
    using allocator_type  = Allocator;
    using reference       = value_type&;
    using const_reference = const value_type&;
    using pointer         = value_type*;
    using const_pointer   = const value_type*;

    using storage_type           = ordered_map<Key, T, IgnoredLess, Allocator>;
    using key_compare            = typename storage_type::key_compare;
    using iterator               = typename storage_type::iterator;
    using const_iterator         = typename storage_type::const_iterator;
    using reverse_iterator       = typename storage_type::reverse_iterator;
    using const_reverse_iterator = typename storage_type::const_reverse_iterator;
    using attr_iterator          = typename storage_type::iterator;
    using const_attr_iterator    = typename storage_type::const_iterator;

    /// Marker read by detail::has_attribute_support: this object type stores
    /// attributes in addition to its members.
    using attribute_support = std::true_type;

    using value_compare          = std::less<value_type>;

    /// Sentinel byte marking an attribute key; see detail::attr_key.
    static constexpr char attr_marker() noexcept { return detail::attr_key<Key>::marker(); }

    static bool is_attr_key(const Key& k) noexcept
    {
        return detail::attr_key<Key>::is(k);
    }

    static Key strip_attr_marker(const Key& k)
    {
        return detail::attr_key<Key>::strip(k);
    }

    // -------- construction / assignment --------

    ordered_map_with_attrs() = default;

    explicit ordered_map_with_attrs(const Allocator& alloc)
        : members_(alloc), attrs_(alloc) {}

    template <class It>
    ordered_map_with_attrs(It first, It last, const Allocator& alloc = Allocator())
        : members_(alloc), attrs_(alloc)
    {
        for (; first != last; ++first)
        {
            insert(*first);
        }
    }

    ordered_map_with_attrs(std::initializer_list<value_type> init,
                           const Allocator& alloc = Allocator())
        : members_(alloc), attrs_(alloc)
    {
        for (const auto& kv : init)
        {
            insert(kv);
        }
    }

    ordered_map_with_attrs(const ordered_map_with_attrs&) = default;
    ordered_map_with_attrs(ordered_map_with_attrs&&) noexcept(std::is_nothrow_move_constructible<storage_type>::value) = default;
    ordered_map_with_attrs& operator=(const ordered_map_with_attrs&) = default;
    ordered_map_with_attrs& operator=(ordered_map_with_attrs&&) noexcept(std::is_nothrow_move_assignable<storage_type>::value) = default;
    ~ordered_map_with_attrs() = default;

    allocator_type get_allocator() const { return members_.get_allocator(); }
    key_compare    key_comp()    const { return key_compare(); }
    value_compare  value_comp()  const { return value_compare(); }

    // -------- standard associative API (members view only) --------

    iterator       begin()        noexcept { return members_.begin(); }
    const_iterator begin()   const noexcept { return members_.begin(); }
    const_iterator cbegin()  const noexcept { return members_.cbegin(); }
    iterator       end()          noexcept { return members_.end(); }
    const_iterator end()     const noexcept { return members_.end(); }
    const_iterator cend()    const noexcept { return members_.cend(); }

    reverse_iterator       rbegin()       noexcept { return members_.rbegin(); }
    const_reverse_iterator rbegin() const noexcept { return members_.rbegin(); }
    const_reverse_iterator crbegin() const noexcept { return members_.crbegin(); }
    reverse_iterator       rend()         noexcept { return members_.rend(); }
    const_reverse_iterator rend()   const noexcept { return members_.rend(); }
    const_reverse_iterator crend()  const noexcept { return members_.crend(); }

    // capacity() / reserve() are forwarded so that detail::is_ordered_map
    // recognizes this container as order-preserving.
    std::size_t capacity() const noexcept { return members_.capacity(); }
    void        reserve(std::size_t new_cap) { members_.reserve(new_cap); }

    bool      empty()    const noexcept { return members_.empty(); }
    size_type size()     const noexcept { return members_.size(); }
    size_type max_size() const noexcept { return members_.max_size(); }

    void clear() noexcept
    {
        members_.clear();
        attrs_.clear();
    }

    // keys carrying the sentinel byte address the attribute storage
    iterator find(const Key& k)
    {
        if (is_attr_key(k))
        {
            return attrs_.find(strip_attr_marker(k));
        }
        return members_.find(k);
    }

    const_iterator find(const Key& k) const
    {
        if (is_attr_key(k))
        {
            return attrs_.find(strip_attr_marker(k));
        }
        return members_.find(k);
    }

    size_type count(const Key& k) const
    {
        if (is_attr_key(k))
        {
            return attrs_.count(strip_attr_marker(k));
        }
        return members_.count(k);
    }

    template <class KeyType, detail::enable_if_t<
                 detail::is_usable_as_key_type<key_compare, key_type, KeyType>::value, int> = 0>
    iterator find(KeyType&& key)
    {
        return members_.find(std::forward<KeyType>(key));
    }

    template <class KeyType, detail::enable_if_t<
                 detail::is_usable_as_key_type<key_compare, key_type, KeyType>::value, int> = 0>
    const_iterator find(KeyType&& key) const
    {
        return members_.find(std::forward<KeyType>(key));
    }

    template <class KeyType, detail::enable_if_t<
                 detail::is_usable_as_key_type<key_compare, key_type, KeyType>::value, int> = 0>
    size_type count(KeyType&& key) const
    {
        return members_.count(std::forward<KeyType>(key));
    }

    template <class KeyType, detail::enable_if_t<
                 detail::is_usable_as_key_type<key_compare, key_type, KeyType>::value, int> = 0>
    bool contains(KeyType&& key) const
    {
        return members_.find(std::forward<KeyType>(key)) != members_.end();
    }

    // keys carrying the sentinel byte address the attribute storage
    mapped_type& at(const Key& k)
    {
        if (is_attr_key(k))
        {
            return attrs_.at(strip_attr_marker(k));
        }
        return members_.at(k);
    }

    const mapped_type& at(const Key& k) const
    {
        if (is_attr_key(k))
        {
            return attrs_.at(strip_attr_marker(k));
        }
        return members_.at(k);
    }

    template <class KeyType, detail::enable_if_t<
                 detail::is_usable_as_key_type<key_compare, key_type, KeyType>::value, int> = 0>
    mapped_type& at(KeyType&& key)
    {
        return members_.at(std::forward<KeyType>(key));
    }

    template <class KeyType, detail::enable_if_t<
                 detail::is_usable_as_key_type<key_compare, key_type, KeyType>::value, int> = 0>
    const mapped_type& at(KeyType&& key) const
    {
        return members_.at(std::forward<KeyType>(key));
    }

    /// Non-const operator[]: sentinel-prefixed keys create entries in attrs_
    /// (with the prefix stripped); all other keys create entries in members_.
    mapped_type& operator[](const Key& k)
    {
        if (is_attr_key(k))
        {
            return attrs_[strip_attr_marker(k)];
        }
        return members_[k];
    }

    mapped_type& operator[](Key&& k)
    {
        if (is_attr_key(k))
        {
            return attrs_[strip_attr_marker(k)];
        }
        return members_[std::move(k)];
    }

    /// Const operator[]: only consults members_. Missing keys throw.
    const mapped_type& operator[](const Key& k) const { return members_.at(k); }

    // -------- insert / emplace with sentinel routing --------

    std::pair<iterator, bool> insert(const value_type& v)
    {
        if (is_attr_key(v.first))
        {
            auto res = attrs_.emplace(strip_attr_marker(v.first), T(v.second));
            return {res.first, res.second};
        }
        return members_.insert(v);
    }

    std::pair<iterator, bool> insert(value_type&& v)
    {
        if (is_attr_key(v.first))
        {
            auto res = attrs_.emplace(strip_attr_marker(v.first), std::move(v.second));
            return {res.first, res.second};
        }
        return members_.insert(std::move(v));
    }

    iterator insert(const_iterator /*hint*/, const value_type& v) { return insert(v).first; }
    iterator insert(const_iterator /*hint*/, value_type&& v)      { return insert(std::move(v)).first; }

    template <class InputIt>
    void insert(InputIt first, InputIt last)
    {
        for (; first != last; ++first)
        {
            insert(*first);
        }
    }

    void insert(std::initializer_list<value_type> init)
    {
        for (const auto& kv : init)
        {
            insert(kv);
        }
    }

    template <class... Args>
    std::pair<iterator, bool> emplace(Args&&... args)
    {
        value_type v(std::forward<Args>(args)...);
        return insert(std::move(v));
    }

    template <class... Args>
    iterator emplace_hint(const_iterator /*hint*/, Args&&... args)
    {
        return emplace(std::forward<Args>(args)...).first;
    }

    template <class M>
    std::pair<iterator, bool> insert_or_assign(const Key& k, M&& obj)
    {
        if (is_attr_key(k))
        {
            const auto stripped = strip_attr_marker(k);
            auto it = attrs_.find(stripped);
            if (it != attrs_.end())
            {
                it->second = std::forward<M>(obj);
                return {it, false};
            }
            auto res = attrs_.emplace(stripped, T(std::forward<M>(obj)));
            return {res.first, res.second};
        }

        auto it = members_.find(k);
        if (it != members_.end())
        {
            it->second = std::forward<M>(obj);
            return {it, false};
        }
        auto res = members_.emplace(k, T(std::forward<M>(obj)));
        return {res.first, res.second};
    }

    // -------- erase (members view only) --------

    // keys carrying the sentinel byte address the attribute storage
    size_type erase(const Key& k)
    {
        if (is_attr_key(k))
        {
            return attrs_.erase(strip_attr_marker(k));
        }
        return members_.erase(k);
    }

    template <class KeyType, detail::enable_if_t<
                 detail::is_usable_as_key_type<key_compare, key_type, KeyType>::value, int> = 0>
    size_type erase(KeyType&& key)
    {
        return members_.erase(std::forward<KeyType>(key));
    }

    iterator  erase(iterator pos) { return members_.erase(pos); }

    // ordered_map declares erase(iterator)/erase(iterator, iterator), which
    // hide the const_iterator overloads inherited from std::vector, so the
    // position must be turned into a mutable iterator before the call.
    iterator  erase(const_iterator pos)
    {
        return members_.erase(members_.begin() + std::distance(members_.cbegin(), pos));
    }

    iterator  erase(const_iterator first, const_iterator last)
    {
        const auto offset = std::distance(members_.cbegin(), first);
        const auto count = std::distance(first, last);
        return members_.erase(members_.begin() + offset,
                              members_.begin() + offset + count);
    }

    void swap(ordered_map_with_attrs& other)
        noexcept(noexcept(members_.swap(other.members_)) && noexcept(attrs_.swap(other.attrs_)))
    {
        members_.swap(other.members_);
        attrs_.swap(other.attrs_);
    }

    // -------- comparison --------

    /// Compares members first and then attributes. The standard associative
    /// API above still observes the members view only, but the container as
    /// an object includes its attribute storage.

    /// @brief lexicographic comparison of two storages: -1, 0, or 1
    /// The inherited std::vector operators are deliberately not used here;
    /// ordered_map declares its own member functions, which interferes with
    /// the free comparison operators and their rewritten candidates.
    static int compare_storage(const storage_type& lhs, const storage_type& rhs)
    {
        auto lit = lhs.cbegin();
        auto rit = rhs.cbegin();
        for (; lit != lhs.cend() && rit != rhs.cend(); ++lit, ++rit)
        {
            if (lit->first < rit->first)
            {
                return -1;
            }
            if (rit->first < lit->first)
            {
                return 1;
            }
            if (lit->second < rit->second)
            {
                return -1;
            }
            if (rit->second < lit->second)
            {
                return 1;
            }
        }

        if (lit == lhs.cend() && rit != rhs.cend())
        {
            return -1;
        }
        if (lit != lhs.cend())
        {
            return 1;
        }
        return 0;
    }

    /// @brief elementwise equality of two storages, preserving their order
    static bool equal_storage(const storage_type& lhs, const storage_type& rhs)
    {
        if (lhs.size() != rhs.size())
        {
            return false;
        }

        auto lit = lhs.cbegin();
        auto rit = rhs.cbegin();
        for (; lit != lhs.cend(); ++lit, ++rit)
        {
            if (!(lit->first == rit->first) || !(lit->second == rit->second))
            {
                return false;
            }
        }

        return true;
    }

    friend bool operator==(const ordered_map_with_attrs& a, const ordered_map_with_attrs& b)
    {
        return equal_storage(a.members_, b.members_) &&
               equal_storage(a.attrs_, b.attrs_);
    }

#if !JSON_HAS_THREE_WAY_COMPARISON
    friend bool operator!=(const ordered_map_with_attrs& a, const ordered_map_with_attrs& b)
    {
        return !(a == b);
    }

    friend bool operator<(const ordered_map_with_attrs& a, const ordered_map_with_attrs& b)
    {
        const auto members = compare_storage(a.members_, b.members_);
        if (members != 0)
        {
            return members < 0;
        }

        return compare_storage(a.attrs_, b.attrs_) < 0;
    }

    friend bool operator<=(const ordered_map_with_attrs& a, const ordered_map_with_attrs& b)
    {
        const auto members = compare_storage(a.members_, b.members_);
        if (members != 0)
        {
            return members < 0;
        }

        return compare_storage(a.attrs_, b.attrs_) <= 0;
    }

    friend bool operator>(const ordered_map_with_attrs& a, const ordered_map_with_attrs& b)
    {
        const auto members = compare_storage(a.members_, b.members_);
        if (members != 0)
        {
            return members > 0;
        }

        return compare_storage(a.attrs_, b.attrs_) > 0;
    }

    friend bool operator>=(const ordered_map_with_attrs& a, const ordered_map_with_attrs& b)
    {
        const auto members = compare_storage(a.members_, b.members_);
        if (members != 0)
        {
            return members > 0;
        }

        return compare_storage(a.attrs_, b.attrs_) >= 0;
    }
#endif

#if JSON_HAS_THREE_WAY_COMPARISON
    friend std::partial_ordering operator<=>(const ordered_map_with_attrs& a, const ordered_map_with_attrs& b)
    {
        auto members = compare_storage(a.members_, b.members_);
        if (members == 0)
        {
            members = compare_storage(a.attrs_, b.attrs_);
        }

        if (members < 0)
        {
            return std::partial_ordering::less;
        }
        if (members > 0)
        {
            return std::partial_ordering::greater;
        }
        return std::partial_ordering::equivalent;
    }
#endif

    // -------- attribute API (explicit) --------

    attr_iterator       attr_begin()       noexcept { return attrs_.begin(); }
    const_attr_iterator attr_begin() const noexcept { return attrs_.begin(); }
    const_attr_iterator attr_cbegin() const noexcept { return attrs_.cbegin(); }
    attr_iterator       attr_end()         noexcept { return attrs_.end(); }
    const_attr_iterator attr_end()   const noexcept { return attrs_.end(); }
    const_attr_iterator attr_cend()  const noexcept { return attrs_.cend(); }

    attr_iterator       attr_find(const Key& k)       { return attrs_.find(k); }
    const_attr_iterator attr_find(const Key& k) const { return attrs_.find(k); }
    size_type           attr_count(const Key& k) const { return attrs_.count(k); }

    mapped_type&       attr_at(const Key& k)       { return attrs_.at(k); }
    const mapped_type& attr_at(const Key& k) const { return attrs_.at(k); }

    size_type attr_size()  const noexcept { return attrs_.size(); }
    bool      attr_empty() const noexcept { return attrs_.empty(); }

    size_type     attr_erase(const Key& k) { return attrs_.erase(k); }
    attr_iterator attr_erase(attr_iterator pos) { return attrs_.erase(pos); }

    template <class... Args>
    std::pair<attr_iterator, bool> attr_emplace(Args&&... args)
    {
        value_type v(std::forward<Args>(args)...);
        return attrs_.insert(std::move(v));
    }

    mapped_type& attr_subscript(const Key& k) { return attrs_[k]; }

    void clear_attrs() noexcept { attrs_.clear(); }

  private:
    storage_type members_;
    storage_type attrs_;
};

template <class Key, class T, class IgnoredLess, class Alloc>
void swap(ordered_map_with_attrs<Key, T, IgnoredLess, Alloc>& a,
          ordered_map_with_attrs<Key, T, IgnoredLess, Alloc>& b)
    noexcept(noexcept(a.swap(b)))
{
    a.swap(b);
}

NLOHMANN_JSON_NAMESPACE_END
