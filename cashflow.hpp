#ifndef CASHFLOW_HPP
#define CASHFLOW_HPP

#include <vector>  // vector 헤더 파일 추가
#include "date.hpp"  // Date 헤더 파일 추가

struct Cashflow {
    double amount;
    Date date;

    Cashflow(double amt, const Date& dt) : amount(amt), date(dt) {}
};

#endif // CASHFLOW_HPP
