#pragma once
// Real Gregorian date utilities for the career clock (the real temporal model).
// Epoch: abs_day 0 == 2025-01-01. Header-only (inline, pure): doesn't touch the build scripts.
// doy convention: 1-based (doy 1 == January 1). Weekday: 0=Sunday .. 6=Saturday (2025-01-01 = Wednesday = 3).
namespace career { namespace dateutil {

inline bool IsLeapYear(int y) { return (y % 4 == 0 && y % 100 != 0) || (y % 400 == 0); }
inline int  DaysInYear(int y) { return IsLeapYear(y) ? 366 : 365; }
inline int  DaysInMonth(int y, int m) {
    static const int d[12] = { 31,28,31,30,31,30,31,31,30,31,30,31 };
    if (m == 2 && IsLeapYear(y)) return 29;
    return d[(m - 1) % 12];
}
// sum of days across the years [2025, year); negative if year < 2025.
inline int DaysBeforeYear(int year) {
    int total = 0;
    if (year >= 2025) { for (int y = 2025; y < year; ++y) total += DaysInYear(y); }
    else              { for (int y = year; y < 2025; ++y) total -= DaysInYear(y); }
    return total;
}
inline int DoyFromYmd(int y, int m, int d) {
    int doy = d;
    for (int mm = 1; mm < m; ++mm) doy += DaysInMonth(y, mm);
    return doy;
}
// (month, day) from a 1-based doy in year. Clamps to the year's range.
inline void YmdFromYearDoy(int year, int doy, int& outM, int& outD) {
    if (doy < 1) doy = 1;
    int maxd = DaysInYear(year); if (doy > maxd) doy = maxd;
    int m = 1;
    while (doy > DaysInMonth(year, m)) { doy -= DaysInMonth(year, m); ++m; }
    outM = m; outD = doy;
}
// abs_day (days since 2025-01-01) for a (year, doy 1-based).
inline int AbsDayFromYearDoy(int year, int doy) { return DaysBeforeYear(year) + (doy - 1); }
inline int AbsDayFromYmd(int y, int m, int d)   { return DaysBeforeYear(y) + (DoyFromYmd(y, m, d) - 1); }
// (year, doy 1-based) from an abs_day.
inline void YearDoyFromAbsDay(int absDay, int& outYear, int& outDoy) {
    int y = 2025, rem = absDay;
    if (rem >= 0) { while (rem >= DaysInYear(y)) { rem -= DaysInYear(y); ++y; } }
    else          { while (rem < 0)              { --y; rem += DaysInYear(y); } }
    outYear = y; outDoy = rem + 1;
}
// day of the week (0=Sunday .. 6=Saturday); 2025-01-01 (abs 0) = Wednesday (3).
inline int WeekdayFromAbsDay(int absDay) { return (((absDay + 3) % 7) + 7) % 7; }

// re-aligns a (refYear, refDoy) to a target year, preserving the month + Nth occurrence of the same weekday
// (e.g. "3rd Sunday of June"). Idempotent if targetYear==refYear. If the Nth occurrence
// doesn't exist in the target month (e.g. a 5th Sunday), it falls back to the last occurrence. Pure/deterministic.
inline int RealignDoy(int refYear, int refDoy, int targetYear) {
    int m, d; YmdFromYearDoy(refYear, refDoy, m, d);
    int wd  = WeekdayFromAbsDay(AbsDayFromYmd(refYear, m, d));   // 0=Sun..6=Sat
    int nth = (d - 1) / 7 + 1;                                    // the Nth occurrence of wd in the month (1..5)
    int firstWd  = WeekdayFromAbsDay(AbsDayFromYmd(targetYear, m, 1));
    int firstOcc = 1 + ((wd - firstWd + 7) % 7);                 // the day of the month of wd's 1st occurrence
    int day = firstOcc + (nth - 1) * 7;
    // 25/09 ([tester], rc.3): the 5th occurrence that does not exist in the target month CARRIES ON to the following week (first
    // days of next month) instead of falling back to the 4th: with the fallback, two rounds authored on the 4th and 5th Friday
    // of January (Aurora Series 3799: Ashcombe 24-Jan and Northhurst Park 31-Jan) projected onto the SAME day in 2029 and the calendar had
    // two races of the same series on the same day. This way the authored order is always preserved. Only December, if it
    // would run out of the year, falls back as before.
    int doy = DoyFromYmd(targetYear, m, 1) + (day - 1);
    if (doy > DaysInYear(targetYear)) doy -= 7;
    return doy;
}

}} // namespace career::dateutil
