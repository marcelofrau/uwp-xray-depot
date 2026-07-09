#pragma once
#include <cstddef>
#include <type_traits>

namespace xb {

struct struct_field {
    const char* name;
    size_t offset;
    char type;       // 'i'=int, 'f'=float, 'd'=double, 'b'=bool, 's'=char[]
    size_t str_len;  // for 's' (char[]), max buffer length incl null
};

namespace detail {
    template<typename T> struct field_traits { static constexpr char ch = 0; };
    template<> struct field_traits<int> { static constexpr char ch = 'i'; };
    template<> struct field_traits<float> { static constexpr char ch = 'f'; };
    template<> struct field_traits<double> { static constexpr char ch = 'd'; };
    template<> struct field_traits<bool> { static constexpr char ch = 'b'; };
    template<size_t N> struct field_traits<char[N]> { static constexpr char ch = 's'; };

    template<typename M> constexpr size_t str_len_of() {
        if constexpr (std::is_array_v<M> && std::is_same_v<std::remove_extent_t<M>, char>)
            return std::extent_v<M>;
        else
            return 0;
    }
}

template<typename T, typename M>
struct_field field(const char* name, M T::*member)
{
    T dummy{};
    size_t offset = reinterpret_cast<const char*>(&(dummy.*member))
                  - reinterpret_cast<const char*>(&dummy);
    return {
        name,
        offset,
        detail::field_traits<M>::ch,
        detail::str_len_of<M>()
    };
}

}
