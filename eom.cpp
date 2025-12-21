#include <iostream>
#include <vector>
#include <ctime>

class Date {
public:
    int day, month, year;

    Date(int d, int m, int y) : day(d), month(m), year(y) {}

    void print() const {
        std::cout << day << "/" << month << "/" << year << std::endl;
    }

    static bool isEndOfMonth(const Date& d) {
        // This is a simple placeholder logic, more can be added.
        int daysInMonth = 30;  // Simplified, replace with actual month-day calculations.
        if ((d.day == daysInMonth) && (d.month == 7)) { // Example: July 31st
            return true;
        }
        return false;
    }

    static Date endOfMonth(const Date& d) {
        // This is a basic example.
        // More complex calendar logic should go here depending on the country (e.g., using QuantLib or custom logic).
        return Date(31, d.month, d.year);  // Adjusted for simplicity to last day of month.
    }
};

int main() {
    Date d1(29, 7, 2025);  // Date is 29th of July
    Date adjustedDate = Date::endOfMonth(d1);
    adjustedDate.print();  // Should print: 31/7/2025
    return 0;
}