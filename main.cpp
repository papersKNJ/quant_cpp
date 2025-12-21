#include "date.hpp"
#include "daycount.hpp"
#include "cashflow.hpp"
#include "eom.hpp"

int main() {
    // 예시 날짜 설정
    Date startDate(1, 1, 2022);
    Date endDate(1, 1, 2023);

    // Day Count 계산
    double dayCount365F = DayCount::calculate(DayCountConvention::ACT_365F, startDate, endDate);
    double dayCount360 = DayCount::calculate(DayCountConvention::ACT_360, startDate, endDate);

    std::cout << "Day Count (ACT/365F): " << dayCount365F << std::endl;
    std::cout << "Day Count (ACT/360): " << dayCount360 << std::endl;

    // EOM 처리 예시
    Date lastBusinessDay = EOM::getLastBusinessDayOfMonth(2022, 7);
    std::cout << "Last Business Day of July 2022: ";
    lastBusinessDay.print();

    return 0;
}