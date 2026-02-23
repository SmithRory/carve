#pragma once

namespace Util
{

template <class... Ts>
struct overloads : Ts...
{
    using Ts::operator()...;
};

template <class... Ts>
overloads(Ts...) -> overloads<Ts...>;

} // namespace Util
