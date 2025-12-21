#ifndef EOM_HPP
#define EOM_HPP

#include "date.hpp"

class EOM {
public:
    static Date getLastBusinessDayOfMonth(int year, int month) {
        // 간단한 예시: 실제로는 금융 달력 사용
        Date lastDay(30, month, year);
        return lastDay;
    }
};

#endif // EOM_HPP