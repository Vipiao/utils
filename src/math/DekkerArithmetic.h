#pragma once

#include <cmath>
#include <cstdint>
#include <limits>
#include <type_traits>

template<typename T>
class DekkerArithmetic {
public:
   static constexpr T calculateScale() {
      // Based on the type of T, calculate the scale factor to be used in the Dekker algorithm.
      // This is 2^round_up(digits_of_mantissa/2) + 1
      constexpr auto mantissa_bits = std::numeric_limits<T>::digits;
      // Round up division by 2
      constexpr auto shift_amount = (mantissa_bits + (mantissa_bits % 2)) / 2;
      if constexpr (std::is_same_v<T, float>) {
         return static_cast<T>((static_cast<uint32_t>(1) << shift_amount) + 1);
      } else if constexpr (std::is_same_v<T, double>) {
         return static_cast<T>((static_cast<uint64_t>(1) << shift_amount) + 1);
      } else {
         static_assert(std::is_floating_point_v<T>, "DekkerArithmetic requires a floating-point type.");
         return T{};
      }
   }
   static constexpr T SCALE = calculateScale();
   struct DekkerNumber {
      T main;
      T error;

      explicit DekkerNumber() : main(0), error(0) {}

      explicit DekkerNumber(T mainPart, T errorPart) : main(mainPart), error(errorPart) {}

      explicit DekkerNumber(float value) : main(value), error(0) {}

      explicit DekkerNumber(double value) {
         main = static_cast<T>(value);
         error = static_cast<T>(value - static_cast<double>(main));
      }
   };

   static DekkerNumber add(const DekkerNumber& x, const DekkerNumber& y) {
      // The main parts are added
      T R{ x.main + y.main };
      T r{};

      // Adjust the error parts
      if (std::abs(x.main) > std::abs(y.main)) {
         r = x.main - R + y.main + y.error + x.error;
      } else {
         r = y.main - R + x.main + x.error + y.error;
      }

      // Sum the main and error parts
      T mainPart{ R + r };
      T errorPart{ R - mainPart + r };

      return DekkerNumber{ mainPart, errorPart };
   }

   static DekkerNumber multiply(const DekkerNumber& x, const DekkerNumber& y) {
      T p00{}, p01{}, p10{}, p11{};

      // Only the leading product keeps its rounding residual. The cross terms
      // are already one order down, so their residuals land below what the pair
      // can hold and are dropped.
      T e00{ exactProduct(x.main, y.main, p00) };
      exactProduct(x.main, y.error, p01);
      exactProduct(x.error, y.main, p10);
      exactProduct(x.error, y.error, p11);

      T mainPart = p00;
      T errorPart = e00 + p11 + (p01 + p10);
      return DekkerNumber{ mainPart, errorPart };
   }

   static DekkerNumber Split(T a) {
      DekkerNumber r{};
      T p = a * SCALE;
      r.main = a - p + p;
      r.error = a - r.main;
      return r;
   }

   static DekkerNumber DekkerMul12(T a, T b) {
      DekkerNumber A{ Split(a) };
      DekkerNumber B{ Split(b) };
      T p{ A.main * B.main };
      T q{ A.main * B.error + A.error * B.main };
      DekkerNumber R{};
      R.main = p + q;
      R.error = p - R.main + q + A.error * B.error;
      return R;
   }

   static DekkerNumber divide(const DekkerNumber& a, const DekkerNumber& b) {
      DekkerNumber u{};
      u.main = a.main / b.main;
      DekkerNumber t{ DekkerMul12(u.main, b.main)};
      T l{ (a.main - t.main - t.error + a.error - u.main * b.error) / b.main };
      DekkerNumber r{};
      r.main = u.main + l;
      r.error = u.main - r.main + l;
      return r;
   }

   static DekkerNumber sqrt(const DekkerNumber& x) {
      if (x.main > 0) {
         // Compute the square root of the main part
         T c = std::sqrt(x.main);

         // Compute the square root using Dekker multiplication for extended precision
         DekkerNumber cDekker = DekkerMul12(c, c);

         // Calculate the error correction term cc
         T cc = ((x.main - cDekker.main - cDekker.error + x.error) * static_cast<T>(0.5)) / c;

         // Calculate the high precision result
         DekkerNumber y;
         y.main = c + cc;
         y.error = c - y.main + cc;

         return y;
      } else {
         // For x.main <= 0, we cannot take the square root
         return DekkerNumber{ 0, 0 };
      }
   }

   //static DekkerNumber sqrt(const DekkerNumber& x) {
   //   T mainPart{ std::sqrt(x.main) };
   //   T fma_result{ std::fma(x.main, -mainPart * mainPart, x.error) };
   //   T errorPart{ fma_result / (2 * mainPart) };
   //
   //   return DekkerNumber{ mainPart, errorPart };
   //}

   static DekkerNumber abs(const DekkerNumber& x) {
      if (x.main < 0) {
         return DekkerNumber{ -x.main, -x.error };
      }
      return x;
   }

   static float toFloat(const DekkerNumber& x) {
      return x.main + x.error;
   }

   static double toDouble(const DekkerNumber& x) {
      return static_cast<double>(x.main) + static_cast<double>(x.error);
   }

private:
   // Helper function to calculate the rounding error of a float/double addition
   static T err(T a, T b) {
      volatile T temp = a + b;
      return (a - temp) + b;
   }

   // Helper function to perform exact product and return the error term
   static T exactProduct(T a, T b, T& product) {
      product = a * b;
      return std::fma(a, b, -product);
   }

};
