#pragma once
#include "date.hpp"

struct Cashflow {
    Date pay_date{};
    double amount{0.0};
};
