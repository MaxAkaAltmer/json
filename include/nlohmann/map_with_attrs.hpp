//     __ _____ _____ _____
//  __|  |   __|     |   | |  JSON for Modern C++
// |  |  |__   |  |  | | | |  version 3.12.0
// |_____|_____|_____|_|___|  https://github.com/nlohmann/json
//
// SPDX-FileCopyrightText: 2013-2026 Niels Lohmann <https://nlohmann.me>
// SPDX-License-Identifier: MIT

#pragma once

#include <cstddef> // size_t, ptrdiff_t
#include <functional> // equal_to, less
#include <initializer_list> // initializer_list
#include <iterator> // iterator_traits
#include <map> // map
#include <memory> // allocator
#include <stdexcept> // out_of_range
#include <type_traits> // enable_if, is_same
#include <utility> // forward, move, pair, swap

#include <nlohmann/detail/abi_macros.hpp>
#include <nlohmann/detail/attr_traits.hpp>
#include <nlohmann/detail/macro_scope.hpp>
#include <nlohmann/detail/meta/type_traits.hpp>

#if JSON_HAS_THREE_WAY_COMPARISON
#include <compare> // std::partial_ordering
#endif

NLOHMANN_JSON_NAMESPACE_BEGIN

/// map_with_attrs: a map-like container that preserves an ordinary associative
/// view of JSON object members while additionally storing a disjoint set of
/// "attributes" (name/primitive pairs). All standard associative-container
/// operations (`find`, `at`, operator[], `begin`, `size`, ...) observe the
/// members view only; attributes are visible through `attr_*` methods and are
/// inserted via a sentinel-prefixed key routed to the attribute container.
///
/// Intended for use as the ObjectType template parameter of basic_json.
template <class Key, class T, class Cmp = std::less<Key>,
          class Allocator = std::allocator<std::pair<const Key, T>>>
class map_with_attrs
{
  public:
    using key_type        = Key;
    using mapped_type     = T;
    using value_type      = std::pair<const Key, T>;
    using size_type       = std::size_t;
    using difference_type = std::ptrdiff_t;
    using key_compare     = Cmp;
    using allocator_type  = Allocator;
    using reference       = value_type&;
    using const_reference = const value_type&;
    using pointer         = value_type*;
    using const_pointer   = const value_type*;

    using storage_type           = std::map<Key, T, Cmp, Allocator>;
    using iterator               = typename storage_type::iterator;
    using const_iterator         = typename storage_type::const_iterator;
    using reverse_iterator       = typename storage_type::reverse_iterator;
    using const_reverse_iterator = typename storage_type::const_reverse_iterator;
    using attr_iterator          = typename storage_type::iterator;
    using const_attr_iterator    = typename storage_type::const_iterator;

    /// Marker read by detail::has_attribute_support: this object type stores
    /// attributes in addition to its members.
    using attribute_support = std::true_type;

    struct value_compare
    {
        key_compare comp;

        value_compare() = default;
        explicit value_compare(const key_compare& c) : comp(c) {}

        bool operator()(const value_type& lhs, const value_type& rhs) const
        {
            return comp(lhs.first, rhs.first);
        }
    };

    /// Sentinel byte: the first byte of a key that marks the entry as an
    /// attribute when routed through the map-like insert API.
    static constexpr char attr_marker() noexcept { return detail::attr_key<Key>::marker(); }

    static bool is_attr_key(const Key& k) noexcept
    {
        return detail::attr_key<Key>::is(k);
    }

    /// Strip the leading attr_marker byte from a key. Assumes is_attr_key(k).
    static Key strip_attr_marker(const Key& k)
    {
        return detail::attr_key<Key>::strip(k);
    }

    // -------- construction / assignment --------

    map_with_attrs() = default;

    explicit map_with_attrs(const Allocator& alloc)
        : members_(alloc), attrs_(alloc) {}

    map_with_attrs(const map_with_attrs&) = default;
    map_with_attrs(map_with_attrs&&) = default;
    map_with_attrs& operator=(const map_with_attrs&) = default;
    map_with_attrs& operator=(map_with_attrs&&) = default;
    ~map_with_attrs() = default;

    template <class InputIt>
    map_with_attrs(InputIt first, InputIt last, const Allocator& alloc = Allocator())
        : members_(alloc), attrs_(alloc)
    {
        for (; first != last; ++first)
        {
            insert(*first);
        }
    }

    map_with_attrs(std::initializer_list<value_type> init,
                   const Allocator& alloc = Allocator())
        : members_(alloc), attrs_(alloc)
    {
        for (const auto& kv : init)
        {
            insert(kv);
        }
    }

    allocator_type get_allocator() const { return members_.get_allocator(); }
    key_compare    key_comp()      const { return members_.key_comp(); }
    value_compare  value_comp()    const { return value_compare(members_.key_comp()); }

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
        return members_.find(std::forward<KeyType>(key)) == members_.end() ? size_type(0) : size_type(1);
    }

    template <class KeyType, detail::enable_if_t<
                 detail::is_usable_as_key_type<key_compare, key_type, KeyType>::value, int> = 0>
    bool contains(KeyType&& key) const
    {
        return members_.find(std::forward<KeyType>(key)) != members_.end();
    }

    iterator       lower_bound(const Key& k)       { return members_.lower_bound(k); }
    const_iterator lower_bound(const Key& k) const { return members_.lower_bound(k); }
    iterator       upper_bound(const Key& k)       { return members_.upper_bound(k); }
    const_iterator upper_bound(const Key& k) const { return members_.upper_bound(k); }

    std::pair<iterator, iterator>             equal_range(const Key& k)       { return members_.equal_range(k); }
    std::pair<const_iterator, const_iterator> equal_range(const Key& k) const { return members_.equal_range(k); }

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
            auto res = attrs_.emplace(strip_attr_marker(v.first), v.second);
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
            return insert_or_assign_impl(attrs_, strip_attr_marker(k), std::forward<M>(obj));
        }
        return insert_or_assign_impl(members_, k, std::forward<M>(obj));
    }

    template <class M>
    std::pair<iterator, bool> insert_or_assign(Key&& k, M&& obj)
    {
        if (is_attr_key(k))
        {
            return insert_or_assign_impl(attrs_, strip_attr_marker(k), std::forward<M>(obj));
        }
        return insert_or_assign_impl(members_, std::forward<Key>(k), std::forward<M>(obj));
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
        auto it = members_.find(std::forward<KeyType>(key));
        if (it == members_.end())
        {
            return 0;
        }

        members_.erase(it);
        return 1;
    }

    iterator  erase(iterator pos) { return members_.erase(pos); }
    iterator  erase(const_iterator pos) { return members_.erase(pos); }
    iterator  erase(const_iterator first, const_iterator last) { return members_.erase(first, last); }

    void swap(map_with_attrs& other)
        noexcept(noexcept(members_.swap(other.members_)) && noexcept(attrs_.swap(other.attrs_)))
    {
        members_.swap(other.members_);
        attrs_.swap(other.attrs_);
    }

    // -------- comparison --------

    /// Compares both the associative members view and the attribute storage.
    /// Members are compared first; attributes break ties, so a container that
    /// differs only in attributes is a different value.
    friend bool operator==(const map_with_attrs& a, const map_with_attrs& b)
    {
        return a.members_ == b.members_ && a.attrs_ == b.attrs_;
    }

#if !JSON_HAS_THREE_WAY_COMPARISON
    friend bool operator!=(const map_with_attrs& a, const map_with_attrs& b)
    {
        return !(a == b);
    }

    friend bool operator<(const map_with_attrs& a, const map_with_attrs& b)
    {
        if (a.members_ != b.members_)
        {
            return a.members_ < b.members_;
        }
        return a.attrs_ < b.attrs_;
    }

    friend bool operator<=(const map_with_attrs& a, const map_with_attrs& b)
    {
        return !(b < a);
    }

    friend bool operator>(const map_with_attrs& a, const map_with_attrs& b)
    {
        return b < a;
    }

    friend bool operator>=(const map_with_attrs& a, const map_with_attrs& b)
    {
        return !(a < b);
    }
#endif

#if JSON_HAS_THREE_WAY_COMPARISON
    friend std::partial_ordering operator<=>(const map_with_attrs& a, const map_with_attrs& b)
    {
        if (a.members_ != b.members_)
        {
            return (a.members_ < b.members_) ? std::partial_ordering::less
                                             : std::partial_ordering::greater;
        }

        if (a.attrs_ != b.attrs_)
        {
            return (a.attrs_ < b.attrs_) ? std::partial_ordering::less
                                         : std::partial_ordering::greater;
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
        return attrs_.emplace(std::forward<Args>(args)...);
    }

    mapped_type& attr_subscript(const Key& k) { return attrs_[k]; }

    void clear_attrs() noexcept { attrs_.clear(); }

  private:
    template <class K, class M>
    std::pair<iterator, bool> insert_or_assign_impl(storage_type& table, K&& key, M&& obj)
    {
        auto it = table.find(key);
        if (it != table.end())
        {
            it->second = std::forward<M>(obj);
            return {it, false};
        }

        auto inserted = table.emplace_hint(table.end(), std::forward<K>(key), std::forward<M>(obj));
        return {inserted, true};
    }

    storage_type members_;
    storage_type attrs_;
};

template <class Key, class T, class Cmp, class Alloc>
void swap(map_with_attrs<Key, T, Cmp, Alloc>& a,
          map_with_attrs<Key, T, Cmp, Alloc>& b)
    noexcept(noexcept(a.swap(b)))
{
    a.swap(b);
}

NLOHMANN_JSON_NAMESPACE_END
