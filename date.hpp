#ifndef DATE_HPP
#define DATE_HPP

#include <iostream>
#include <ctime>

struct Date {
    int day, month, year;

    Date(int d, int m, int y) : day(d), month(m), year(y) {}

    // 날짜 출력 함수
    void print() const {
        std::cout << day << "/" << month << "/" << year << std::endl;
    }

    // 날짜 간의 차이 계산 함수 (예: 두 날짜 간 일수 계산)
    static int daysBetween(const Date& start, const Date& end) {
        // 간단한 예시: 실제로는 날짜 계산 라이브러리를 사용해야 함
        return (end.year - start.year) * 365 + (end.month - start.month) * 30 + (end.day - start.day);
    }
};

#endif // DATE_HPP
