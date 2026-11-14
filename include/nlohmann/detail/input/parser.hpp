//     __ _____ _____ _____
//  __|  |   __|     |   | |  JSON for Modern C++
// |  |  |__   |  |  | | | |  version 3.12.0
// |_____|_____|_____|_|___|  https://github.com/nlohmann/json
//
// SPDX-FileCopyrightText: 2013-2026 Niels Lohmann <https://nlohmann.me>
// SPDX-License-Identifier: MIT

#pragma once

#include <cmath> // isfinite
#include <cstdint> // uint8_t
#include <functional> // function
#include <string> // string
#include <utility> // move
#include <vector> // vector

#include <nlohmann/detail/attr_traits.hpp>
#include <nlohmann/detail/exceptions.hpp>
#include <nlohmann/detail/input/input_adapters.hpp>
#include <nlohmann/detail/input/json_sax.hpp>
#include <nlohmann/detail/input/lexer.hpp>
#include <nlohmann/detail/macro_scope.hpp>
#include <nlohmann/detail/meta/is_sax.hpp>
#include <nlohmann/detail/string_concat.hpp>
#include <nlohmann/detail/value_t.hpp>

NLOHMANN_JSON_NAMESPACE_BEGIN
namespace detail
{
////////////
// parser //
////////////

enum class parse_event_t : std::uint8_t
{
    /// the parser read `{` and started to process a JSON object
    object_start,
    /// the parser read `}` and finished processing a JSON object
    object_end,
    /// the parser read `[` and started to process a JSON array
    array_start,
    /// the parser read `]` and finished processing a JSON array
    array_end,
    /// the parser read a key of a value in an object
    key,
    /// the parser finished reading a JSON value
    value
};

template<typename BasicJsonType>
using parser_callback_t =
    std::function<bool(int /*depth*/, parse_event_t /*event*/, BasicJsonType& /*parsed*/)>;

/*!
@brief syntax analysis

This class implements a parser for JSON text. Nested arrays and objects are tracked with an explicit
stack instead of recursion, so deeply nested input does not exhaust the call stack, and what is read
is reported as SAX events.
*/
template<typename BasicJsonType, typename InputAdapterType>
class parser
{
    using number_integer_t = typename BasicJsonType::number_integer_t;
    using number_unsigned_t = typename BasicJsonType::number_unsigned_t;
    using number_float_t = typename BasicJsonType::number_float_t;
    using string_t = typename BasicJsonType::string_t;
    using lexer_t = lexer<BasicJsonType, InputAdapterType>;
    using token_type = typename lexer_t::token_type;

  public:
    /// a parser reading from an input adapter
    explicit parser(InputAdapterType&& adapter,
                    parser_callback_t<BasicJsonType> cb = nullptr,
                    const bool allow_exceptions_ = true,
                    const bool ignore_comments = false,
                    const bool ignore_trailing_commas_ = false,
                    const bool discard_number_values_ = false)
        : callback(std::move(cb))
        , m_lexer(std::move(adapter), ignore_comments, discard_number_values_)
        , allow_exceptions(allow_exceptions_)
        , ignore_trailing_commas(ignore_trailing_commas_)
    {
        // read first token
        get_token();
    }

    /*!
    @brief public parser interface

    @param[in] strict      whether to expect the last token to be EOF
    @param[in,out] result  parsed JSON value

    @throw parse_error.101 in case of an unexpected token (including invalid
           unicode escapes and surrogate errors, which are reported with a
           detailed message)
    */
    void parse(const bool strict, BasicJsonType& result)
    {
        if (callback)
        {
            json_sax_dom_callback_parser<BasicJsonType, InputAdapterType> sdp(result, callback, allow_exceptions, &m_lexer);

            // in case of an error, return a discarded value
            if (!parse_dom(sdp, strict))
            {
                result = value_t::discarded;
                return;
            }

            // set top-level value to null if it was discarded by the callback
            // function
            if (result.is_discarded())
            {
                result = nullptr;
            }
        }
        else
        {
            json_sax_dom_parser<BasicJsonType, InputAdapterType> sdp(result, allow_exceptions, &m_lexer);

            // in case of an error, return a discarded value
            if (!parse_dom(sdp, strict))
            {
                result = value_t::discarded;
                return;
            }
        }

        result.assert_invariant();
    }

    /*!
    @brief public accept interface

    @param[in] strict  whether to expect the last token to be EOF
    @return whether the input is a proper JSON text
    */
    bool accept(const bool strict = true)
    {
        json_sax_acceptor<BasicJsonType> sax_acceptor;
        return sax_parse(&sax_acceptor, strict);
    }

    template<typename SAX>
    JSON_HEDLEY_NON_NULL(2)
    bool sax_parse(SAX* sax, const bool strict = true)
    {
        (void)detail::is_sax_static_asserts<SAX, BasicJsonType> {};
        const bool result = sax_parse_internal(sax);

        if (result)
        {
            if (strict)
            {
                // strict mode: next byte must be EOF
                if (get_token() != token_type::end_of_input)
                {
                    return sax->parse_error(m_lexer.get_position(),
                                            m_lexer.get_token_string(),
                                            parse_error::create(101, m_lexer.get_position(), exception_message(token_type::end_of_input, "value"), nullptr));
                }
            }
            else
            {
                // the caller keeps using the input: position it right after
                // the value by leaving the character that terminated it
                m_lexer.release_lookahead();
            }
        }

        return result;
    }

  private:
    /*!
    @brief run a DOM SAX parser to completion and position the lexer

    Shared by both branches of @ref parse(): builds no SAX parser itself,
    but drives an already-constructed @a json_sax_dom_parser or
    @ref json_sax_dom_callback_parser through @ref sax_parse_internal(),
    then applies the strict-EOF check (reporting parse_error.101 through
    @a sdp on failure) or, in non-strict mode, releases the lookahead so
    the caller can keep reading the input right after the parsed value.

    @param[in,out] sdp     the DOM SAX parser to run
    @param[in] strict      whether to expect the last token to be EOF
    @return whether @a sdp did not report an error
    */
    template<typename DomSax>
    bool parse_dom(DomSax& sdp, const bool strict)
    {
        sax_parse_internal(&sdp);

        if (strict)
        {
            // in strict mode, input must be completely read
            if (get_token() != token_type::end_of_input)
            {
                sdp.parse_error(m_lexer.get_position(),
                                m_lexer.get_token_string(),
                                parse_error::create(101, m_lexer.get_position(),
                                                    exception_message(token_type::end_of_input, "value"), nullptr));
            }
        }
        else
        {
            // the caller keeps using the input: position it right after
            // the value by leaving the character that terminated it
            m_lexer.release_lookahead();
        }

        return !sdp.is_errored();
    }

    template<typename SAX>
    JSON_HEDLEY_NON_NULL(2)
    bool sax_parse_internal(SAX* sax)
    {
        // stack to remember the hierarchy of structured values we are parsing
        // true = array; false = object
        std::vector<bool> states;
        // value to avoid a goto (see comment where set to true)
        bool skip_to_state_evaluation = false;

        while (true)
        {
            if (!skip_to_state_evaluation)
            {
                // invariant: get_token() was called before each iteration
                switch (last_token)
                {
                    case token_type::begin_object:
                    {
                        if (JSON_HEDLEY_UNLIKELY(!sax->start_object(detail::unknown_size())))
                        {
                            return false;
                        }

                        // closing } -> we are done
                        if (get_token() == token_type::end_object)
                        {
                            if (JSON_HEDLEY_UNLIKELY(!sax->end_object()))
                            {
                                return false;
                            }
                            break;
                        }

                        // an identifier key denotes an attribute
                        const bool key_is_attribute = (last_token == token_type::value_identifier);

                        // parse key
                        if (JSON_HEDLEY_UNLIKELY(!token_is_object_key()))
                        {
                            return sax->parse_error(m_lexer.get_position(),
                                                    m_lexer.get_token_string(),
                                                    parse_error::create(101, m_lexer.get_position(), exception_message(token_type::value_string, "object key"), nullptr));
                        }
                        if (JSON_HEDLEY_UNLIKELY(!emit_object_key(sax)))
                        {
                            return false;
                        }

                        // parse separator (:)
                        if (JSON_HEDLEY_UNLIKELY(get_token() != token_type::name_separator))
                        {
                            return sax->parse_error(m_lexer.get_position(),
                                                    m_lexer.get_token_string(),
                                                    parse_error::create(101, m_lexer.get_position(), exception_message(token_type::name_separator, "object separator"), nullptr));
                        }

                        // remember we are now inside an object
                        states.push_back(false);

                        // parse values
                        get_token();

                        // an attribute only holds primitives
                        if (JSON_HEDLEY_UNLIKELY(key_is_attribute && !token_is_primitive_value()))
                        {
                            return attribute_value_error(sax);
                        }
                        continue;
                    }

                    case token_type::begin_array:
                    {
                        if (JSON_HEDLEY_UNLIKELY(!sax->start_array(detail::unknown_size())))
                        {
                            return false;
                        }

                        // closing ] -> we are done
                        if (get_token() == token_type::end_array)
                        {
                            if (JSON_HEDLEY_UNLIKELY(!sax->end_array()))
                            {
                                return false;
                            }
                            break;
                        }

                        // remember we are now inside an array
                        states.push_back(true);

                        // parse values (no need to call get_token)
                        continue;
                    }

                    case token_type::value_float:
                    {
                        const auto res = m_lexer.get_number_float();

                        if (JSON_HEDLEY_UNLIKELY(!std::isfinite(res)))
                        {
                            return sax->parse_error(m_lexer.get_position(),
                                                    m_lexer.get_token_string(),
                                                    out_of_range::create(406, concat("number overflow parsing '", m_lexer.get_token_string(), '\''), nullptr));
                        }

                        if (JSON_HEDLEY_UNLIKELY(!sax->number_float(res, m_lexer.get_string())))
                        {
                            return false;
                        }

                        break;
                    }

                    case token_type::literal_false:
                    {
                        if (JSON_HEDLEY_UNLIKELY(!sax->boolean(false)))
                        {
                            return false;
                        }
                        break;
                    }

                    case token_type::literal_null:
                    {
                        if (JSON_HEDLEY_UNLIKELY(!sax->null()))
                        {
                            return false;
                        }
                        break;
                    }

                    case token_type::literal_true:
                    {
                        if (JSON_HEDLEY_UNLIKELY(!sax->boolean(true)))
                        {
                            return false;
                        }
                        break;
                    }

                    case token_type::value_integer:
                    {
                        if (JSON_HEDLEY_UNLIKELY(!sax->number_integer(m_lexer.get_number_integer())))
                        {
                            return false;
                        }
                        break;
                    }

                    case token_type::value_string:
                    {
                        if (JSON_HEDLEY_UNLIKELY(!sax->string(m_lexer.get_string())))
                        {
                            return false;
                        }
                        break;
                    }

                    case token_type::value_unsigned:
                    {
                        if (JSON_HEDLEY_UNLIKELY(!sax->number_unsigned(m_lexer.get_number_unsigned())))
                        {
                            return false;
                        }
                        break;
                    }

                    case token_type::parse_error:
                    {
                        // using "uninitialized" to avoid an "expected" message
                        return sax->parse_error(m_lexer.get_position(),
                                                m_lexer.get_token_string(),
                                                parse_error::create(101, m_lexer.get_position(), exception_message(token_type::uninitialized, "value"), nullptr));
                    }
                    case token_type::end_of_input:
                    {
                        if (JSON_HEDLEY_UNLIKELY(m_lexer.get_position().chars_read_total == 1))
                        {
                            return sax->parse_error(m_lexer.get_position(),
                                                    m_lexer.get_token_string(),
                                                    parse_error::create(101, m_lexer.get_position(),
                                                            "attempting to parse an empty input; check that your input string or stream contains the expected JSON", nullptr));
                        }

                        return sax->parse_error(m_lexer.get_position(),
                                                m_lexer.get_token_string(),
                                                parse_error::create(101, m_lexer.get_position(), exception_message(token_type::literal_or_value, "value"), nullptr));
                    }
                    case token_type::uninitialized:
                    case token_type::end_array:
                    case token_type::end_object:
                    case token_type::name_separator:
                    case token_type::value_separator:
                    case token_type::literal_or_value:
                    default: // the last token was unexpected
                    {
                        return sax->parse_error(m_lexer.get_position(),
                                                m_lexer.get_token_string(),
                                                parse_error::create(101, m_lexer.get_position(), exception_message(token_type::literal_or_value, "value"), nullptr));
                    }
                }
            }
            else
            {
                skip_to_state_evaluation = false;
            }

            // we reached this line after we successfully parsed a value
            if (states.empty())
            {
                // empty stack: we reached the end of the hierarchy: done
                return true;
            }

            if (states.back())  // array
            {
                // comma -> next value
                // or end of array (ignore_trailing_commas = true)
                if (get_token() == token_type::value_separator)
                {
                    // parse a new value
                    get_token();

                    // if ignore_trailing_commas and last_token is ], we can continue to "closing ]"
                    if (!(ignore_trailing_commas && last_token == token_type::end_array))
                    {
                        continue;
                    }
                }

                // closing ]
                if (JSON_HEDLEY_LIKELY(last_token == token_type::end_array))
                {
                    if (JSON_HEDLEY_UNLIKELY(!sax->end_array()))
                    {
                        return false;
                    }

                    // We are done with this array. Before we can parse a
                    // new value, we need to evaluate the new state first.
                    // By setting skip_to_state_evaluation to true, the next
                    // iteration skips parsing a value and evaluates the
                    // enclosing state directly.
                    JSON_ASSERT(!states.empty());
                    states.pop_back();
                    skip_to_state_evaluation = true;
                    continue;
                }

                return sax->parse_error(m_lexer.get_position(),
                                        m_lexer.get_token_string(),
                                        parse_error::create(101, m_lexer.get_position(), exception_message(token_type::end_array, "array"), nullptr));
            }

            // states.back() is false -> object

            // comma -> next value
            // or end of object (ignore_trailing_commas = true)
            if (get_token() == token_type::value_separator)
            {
                get_token();

                // if ignore_trailing_commas and last_token is }, we can continue to "closing }"
                if (!(ignore_trailing_commas && last_token == token_type::end_object))
                {
                    // an identifier key denotes an attribute
                    const bool key_is_attribute = (last_token == token_type::value_identifier);

                    // parse key
                    if (JSON_HEDLEY_UNLIKELY(!token_is_object_key()))
                    {
                        return sax->parse_error(m_lexer.get_position(),
                                                m_lexer.get_token_string(),
                                                parse_error::create(101, m_lexer.get_position(), exception_message(token_type::value_string, "object key"), nullptr));
                    }

                    if (JSON_HEDLEY_UNLIKELY(!emit_object_key(sax)))
                    {
                        return false;
                    }

                    // parse separator (:)
                    if (JSON_HEDLEY_UNLIKELY(get_token() != token_type::name_separator))
                    {
                        return sax->parse_error(m_lexer.get_position(),
                                                m_lexer.get_token_string(),
                                                parse_error::create(101, m_lexer.get_position(), exception_message(token_type::name_separator, "object separator"), nullptr));
                    }

                    // parse values
                    get_token();

                    // an attribute only holds primitives
                    if (JSON_HEDLEY_UNLIKELY(key_is_attribute && !token_is_primitive_value()))
                    {
                        return attribute_value_error(sax);
                    }
                    continue;
                }
            }

            // closing }
            if (JSON_HEDLEY_LIKELY(last_token == token_type::end_object))
            {
                if (JSON_HEDLEY_UNLIKELY(!sax->end_object()))
                {
                    return false;
                }

                // We are done with this object. Before we can parse a
                // new value, we need to evaluate the new state first.
                // By setting skip_to_state_evaluation to true, the next
                // iteration skips parsing a value and evaluates the
                // enclosing state directly.
                JSON_ASSERT(!states.empty());
                states.pop_back();
                skip_to_state_evaluation = true;
                continue;
            }

            return sax->parse_error(m_lexer.get_position(),
                                    m_lexer.get_token_string(),
                                    parse_error::create(101, m_lexer.get_position(), exception_message(token_type::end_object, "object"), nullptr));
        }
    }

    /// whether the object type of the parsed value stores attributes
    static constexpr bool attributes_enabled() noexcept
    {
        return detail::basic_json_has_attributes<BasicJsonType>::value;
    }

    /// whether the last token can start an object entry: a quoted key, or, in
    /// the JSON with attributes format, an unquoted identifier
    bool token_is_object_key() const noexcept
    {
        return last_token == token_type::value_string
               || (attributes_enabled() && last_token == token_type::value_identifier);
    }

    /// whether the last token can be the value of an attribute: only
    /// primitives are allowed, nested objects and arrays are not
    bool token_is_primitive_value() const noexcept
    {
        switch (last_token)
        {
            case token_type::value_string:
            case token_type::value_integer:
            case token_type::value_unsigned:
            case token_type::value_float:
            case token_type::literal_true:
            case token_type::literal_false:
            case token_type::literal_null:
                return true;

            case token_type::uninitialized:
            case token_type::value_identifier:
            case token_type::begin_array:
            case token_type::begin_object:
            case token_type::end_array:
            case token_type::end_object:
            case token_type::name_separator:
            case token_type::value_separator:
            case token_type::parse_error:
            case token_type::end_of_input:
            case token_type::literal_or_value:
            default:
                return false;
        }
    }

    /*!
    @brief report the pending object key to @a sax

    A key read as an identifier is prefixed with the attribute sentinel byte,
    which is where the object container routes it into its attribute storage.
    */
    template<typename SAX>
    bool emit_object_key(SAX* sax)
    {
        if (attributes_enabled() && last_token == token_type::value_identifier)
        {
            string_t key(static_cast<typename string_t::size_type>(1),
                         static_cast<typename string_t::value_type>(detail::attr_key<string_t>::marker()));
            key.append(m_lexer.get_string());
            return sax->key(key);
        }

        return sax->key(m_lexer.get_string());
    }

    /// @brief reject a structured value stored under an attribute name
    template<typename SAX>
    bool attribute_value_error(SAX* sax)
    {
        return sax->parse_error(m_lexer.get_position(), m_lexer.get_token_string(),
                                parse_error::create(101, m_lexer.get_position(),
                                                    "an attribute value must be a primitive (null, a boolean, a number or a string)", nullptr));
    }

    /// get next token from lexer
    token_type get_token()
    {
        return last_token = m_lexer.scan();
    }

    std::string exception_message(const token_type expected, const std::string& context)
    {
        std::string error_msg = "syntax error ";

        if (!context.empty())
        {
            error_msg += concat("while parsing ", context, ' ');
        }

        error_msg += "- ";

        if (last_token == token_type::parse_error)
        {
            error_msg += concat(m_lexer.get_error_message(), "; last read: '",
                                m_lexer.get_token_string(), '\'');
        }
        else
        {
            error_msg += concat("unexpected ", lexer_t::token_type_name(last_token));
        }

        if (expected != token_type::uninitialized)
        {
            error_msg += concat("; expected ", lexer_t::token_type_name(expected));
        }

        return error_msg;
    }

  private:
    /// callback function
    const parser_callback_t<BasicJsonType> callback = nullptr;
    /// the type of the last read token
    token_type last_token = token_type::uninitialized;
    /// the lexer
    lexer_t m_lexer;
    /// whether to throw exceptions in case of errors
    const bool allow_exceptions = true;
    /// whether trailing commas in objects and arrays should be ignored (true) or signaled as errors (false)
    const bool ignore_trailing_commas = false;
};

}  // namespace detail
NLOHMANN_JSON_NAMESPACE_END
