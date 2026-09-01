// dekker_arithmetic_test.cpp
//
// Self-verifying test for DekkerArithmetic. Every check decides its own verdict
// and main returns the failure count, so ctest reads the result from the exit
// code and nothing here needs a human to interpret it.
//
// The statistics are still printed, but only as context for a failure; they are
// not what passes or fails the run.

#include "math/DekkerArithmetic.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <random>
#include <string>
#include <vector>

namespace {

// The generator seed. Fixed so a failure reproduces from the message alone.
constexpr unsigned int k_seed{ 0 };
constexpr size_t k_randomSampleCount{ 10000 };
constexpr double k_randomRange{ 100.0 };

// A double splits into two floats with roughly twice a float's mantissa, so the
// round trip should land this close. Loose by two bits against the 2^-48 the
// split theoretically gives.
constexpr double k_roundTripTolerance{ 1.0 / 70368744177664.0 };  // 2^-46

// The finest the pair can resolve, relative to the value. The reference below is
// a double and carries its own rounding, so a float can land exactly on it by
// luck where the pair lands a few double-ulps away; that is the reference being
// sharper than the representation, not the representation being wrong. The
// accuracy comparison allows this much slack for it.
constexpr double k_dekkerResolution{ k_roundTripTolerance };

// Counts assertions and their failures, and prints the first few failures with
// the inputs that produced them.
class TestResults {
public:
   void check(bool passed, const std::string& name, const std::string& detail = {}) {
      m_checks++;
      if (passed) {
         return;
      }
      m_failures++;
      if (m_failures <= k_maxReportedFailures) {
         std::cout << "FAIL  " << name;
         if (!detail.empty()) {
            std::cout << " -- " << detail;
         }
         std::cout << std::endl;
      }
   }

   int checks() const { return m_checks; }
   int failures() const { return m_failures; }

private:
   static constexpr int k_maxReportedFailures{ 20 };
   int m_checks{ 0 };
   int m_failures{ 0 };
};

// What one operation's accuracy comparison found across every sample pair.
struct AccuracySummary {
   size_t comparedSamples{ 0 };
   size_t skippedSamples{ 0 };
   size_t violations{ 0 };
   double worstDekkerError{ 0.0 };
   double worstFloatError{ 0.0 };
   double firstViolationLhs{ 0.0 };
   double firstViolationRhs{ 0.0 };
};

// The inputs every operation is measured over: a deterministic uniform spread
// plus the full cross product of the awkward values, so zeroes, denormals,
// infinities and NaNs meet every other magnitude.
struct SamplePairs {
   std::vector<double> lhs;
   std::vector<double> rhs;
};

SamplePairs buildSamplePairs() {
   SamplePairs samples{};
   samples.lhs.resize(k_randomSampleCount);
   samples.rhs.resize(k_randomSampleCount);

   std::mt19937 generator{ k_seed };
   std::uniform_real_distribution<double> distribution{ -k_randomRange, k_randomRange };
   for (size_t ii = 0; ii < k_randomSampleCount; ii++) {
      samples.lhs[ii] = distribution(generator);
      samples.rhs[ii] = distribution(generator);
   }

   std::vector<double> specialNumbers{
      0.0, 1.0, -1.0,
      std::numeric_limits<double>::infinity(),
      -std::numeric_limits<double>::infinity(),
      std::numeric_limits<double>::quiet_NaN(),
      std::numeric_limits<double>::signaling_NaN(),
      std::numeric_limits<double>::denorm_min(),
      std::numeric_limits<double>::min(),
      std::numeric_limits<double>::max(),
      std::numeric_limits<double>::lowest(),
      std::numeric_limits<double>::denorm_min() * 2,
   };
   const size_t seedCount{ specialNumbers.size() };
   for (size_t ii = 0; ii < seedCount; ii++) {
      const double value{ specialNumbers[ii] };
      specialNumbers.push_back(-value);
      specialNumbers.push_back(value * 2);
      specialNumbers.push_back(value / 2);
      specialNumbers.push_back(value + 1);
      specialNumbers.push_back(value - 1);
      specialNumbers.push_back(value + 0.1);
      specialNumbers.push_back(value - 0.1);
      specialNumbers.push_back(value * 0.1);
      specialNumbers.push_back(value / 0.1);
      specialNumbers.push_back(value * 10);
      specialNumbers.push_back(value / 10);
   }

   for (size_t ii = 0; ii < specialNumbers.size(); ii++) {
      for (size_t jj = 0; jj < specialNumbers.size(); jj++) {
         samples.lhs.push_back(specialNumbers[ii]);
         samples.rhs.push_back(specialNumbers[jj]);
      }
   }
   return samples;
}

// The property under test: against a double-precision reference, the Dekker
// pair must never be further off than the single float is. referenceOp works in
// double, floatOp in float, dekkerOp on the split representation.
template<typename ReferenceOp, typename FloatOp, typename DekkerOp>
AccuracySummary compareAccuracy(const SamplePairs& samples, ReferenceOp referenceOp,
                                FloatOp floatOp, DekkerOp dekkerOp) {
   AccuracySummary summary{};
   for (size_t ii = 0; ii < samples.lhs.size(); ii++) {
      const double lhs{ samples.lhs[ii] };
      const double rhs{ samples.rhs[ii] };
      const float lhsFloat{ static_cast<float>(lhs) };
      const float rhsFloat{ static_cast<float>(rhs) };

      const double reference{ referenceOp(lhs, rhs) };
      const double floatResult{ static_cast<double>(floatOp(lhsFloat, rhsFloat)) };

      // The pair is two floats, so its domain is what a float can hold: an
      // operand that overflows or is already non-finite has no representation
      // here, and the compensation terms degenerate rather than approximate.
      // testNonFiniteBehaviour covers that boundary instead. A reference or a
      // float result outside the range leaves nothing to compare either, since
      // both error terms come out infinite or NaN and the ordering says nothing.
      // Counted so the skipped share stays visible.
      if (!std::isfinite(static_cast<double>(lhsFloat)) ||
          !std::isfinite(static_cast<double>(rhsFloat)) ||
          !std::isfinite(reference) || !std::isfinite(floatResult)) {
         summary.skippedSamples++;
         continue;
      }

      const DekkerArithmetic<float>::DekkerNumber lhsDekker{ lhs };
      const DekkerArithmetic<float>::DekkerNumber rhsDekker{ rhs };
      const double dekkerResult{
         DekkerArithmetic<float>::toDouble(dekkerOp(lhsDekker, rhsDekker)) };

      const double dekkerError{ std::abs(reference - dekkerResult) };
      const double floatError{ std::abs(reference - floatResult) };

      summary.comparedSamples++;
      summary.worstDekkerError = std::max(summary.worstDekkerError, dekkerError);
      summary.worstFloatError = std::max(summary.worstFloatError, floatError);

      // A non-finite Dekker result where the float path produced a number is a
      // failure in its own right: NaN loses every comparison, so without this it
      // would pass as "not worse".
      const double allowedError{ floatError + k_dekkerResolution * std::abs(reference) };
      const bool violated{ !std::isfinite(dekkerResult) || dekkerError > allowedError };
      if (violated) {
         if (summary.violations == 0) {
            summary.firstViolationLhs = lhs;
            summary.firstViolationRhs = rhs;
         }
         summary.violations++;
      }
   }
   return summary;
}

void reportAccuracy(TestResults& results, const std::string& operation,
                    const AccuracySummary& summary) {
   std::cout << operation << ": compared " << summary.comparedSamples << ", skipped "
             << summary.skippedSamples << ", worst Dekker error " << summary.worstDekkerError
             << ", worst float error " << summary.worstFloatError << std::endl;

   results.check(summary.comparedSamples > 0, operation + " has comparable samples",
      "every sample was skipped, so the comparison proved nothing");
   results.check(summary.violations == 0, operation + " is no worse than plain float",
      std::to_string(summary.violations) + " of " + std::to_string(summary.comparedSamples) +
      " samples, first at lhs=" + std::to_string(summary.firstViolationLhs) +
      " rhs=" + std::to_string(summary.firstViolationRhs));
}

void testAccuracyAgainstFloat(TestResults& results, const SamplePairs& samples) {
   reportAccuracy(results, "Addition", compareAccuracy(samples,
      [](double a, double b) { return a + b; },
      [](float a, float b) { return a + b; },
      [](const DekkerArithmetic<float>::DekkerNumber& a,
         const DekkerArithmetic<float>::DekkerNumber& b) {
         return DekkerArithmetic<float>::add(a, b);
      }));

   reportAccuracy(results, "Multiplication", compareAccuracy(samples,
      [](double a, double b) { return a * b; },
      [](float a, float b) { return a * b; },
      [](const DekkerArithmetic<float>::DekkerNumber& a,
         const DekkerArithmetic<float>::DekkerNumber& b) {
         return DekkerArithmetic<float>::multiply(a, b);
      }));

   reportAccuracy(results, "Division", compareAccuracy(samples,
      [](double a, double b) { return a / b; },
      [](float a, float b) { return a / b; },
      [](const DekkerArithmetic<float>::DekkerNumber& a,
         const DekkerArithmetic<float>::DekkerNumber& b) {
         return DekkerArithmetic<float>::divide(a, b);
      }));

   // Square root takes one operand, so the right-hand sample is ignored and the
   // magnitude is used to keep the argument in the defined domain.
   reportAccuracy(results, "Square root", compareAccuracy(samples,
      [](double a, double) { return std::sqrt(std::abs(a)); },
      [](float a, float) { return std::sqrt(std::abs(a)); },
      [](const DekkerArithmetic<float>::DekkerNumber& a,
         const DekkerArithmetic<float>::DekkerNumber&) {
         return DekkerArithmetic<float>::sqrt(DekkerArithmetic<float>::abs(a));
      }));
}

// The exactness the split guarantees, which an averaged error cannot see: the
// product of two floats needs 48 mantissa bits and a Dekker pair holds them, so
// this is an equality and not a tolerance.
void testProductIsExact(TestResults& results, const SamplePairs& samples) {
   size_t compared{ 0 };
   size_t mismatches{ 0 };
   for (size_t ii = 0; ii < samples.lhs.size(); ii++) {
      const float lhs{ static_cast<float>(samples.lhs[ii]) };
      const float rhs{ static_cast<float>(samples.rhs[ii]) };
      const double exactProduct{ static_cast<double>(lhs) * static_cast<double>(rhs) };

      // Underflow spills the residual out of float's normal range and overflow
      // loses the product outright; neither is what this check is about.
      if (!std::isfinite(exactProduct) ||
          std::abs(exactProduct) < static_cast<double>(std::numeric_limits<float>::min())) {
         continue;
      }
      compared++;

      const DekkerArithmetic<float>::DekkerNumber product{
         DekkerArithmetic<float>::multiply(DekkerArithmetic<float>::DekkerNumber{ lhs },
                                           DekkerArithmetic<float>::DekkerNumber{ rhs }) };
      if (DekkerArithmetic<float>::toDouble(product) != exactProduct) {
         mismatches++;
      }
   }
   std::cout << "Exact product: compared " << compared << std::endl;
   results.check(compared > 0, "exact product has comparable samples");
   results.check(mismatches == 0, "product of two floats is exact",
      std::to_string(mismatches) + " of " + std::to_string(compared) + " products were inexact");
}

// Splitting a double and putting it back together again has to preserve nearly
// twice a float's precision, or the representation is not buying anything.
void testRoundTrip(TestResults& results, const SamplePairs& samples) {
   size_t compared{ 0 };
   size_t mismatches{ 0 };
   double worstRelativeError{ 0.0 };
   for (const double value : samples.lhs) {
      // Outside float's normal range the split has no second float to spend, so
      // the round trip is not expected to hold there.
      const double magnitude{ std::abs(value) };
      if (!std::isfinite(value) ||
          magnitude < static_cast<double>(std::numeric_limits<float>::min()) ||
          magnitude > static_cast<double>(std::numeric_limits<float>::max())) {
         continue;
      }
      compared++;

      const DekkerArithmetic<float>::DekkerNumber split{ value };
      const double relativeError{
         std::abs(value - DekkerArithmetic<float>::toDouble(split)) / magnitude };
      worstRelativeError = std::max(worstRelativeError, relativeError);
      if (relativeError > k_roundTripTolerance) {
         mismatches++;
      }
   }
   std::cout << "Round trip: compared " << compared << ", worst relative error "
             << worstRelativeError << std::endl;
   results.check(compared > 0, "round trip has comparable samples");
   results.check(mismatches == 0, "double survives the split and rejoin",
      std::to_string(mismatches) + " of " + std::to_string(compared) +
      " values exceeded the tolerance, worst " + std::to_string(worstRelativeError));
}

// The boundary of the type's domain, which the averaged statistics discard
// entirely. Stated as rules so a change at the edge shows up as a failure rather
// than as a gap.
void testNonFiniteBehaviour(TestResults& results) {
   const double nan{ std::numeric_limits<double>::quiet_NaN() };
   const double infinity{ std::numeric_limits<double>::infinity() };

   const DekkerArithmetic<float>::DekkerNumber nanNumber{ nan };
   const DekkerArithmetic<float>::DekkerNumber infinityNumber{ infinity };
   const DekkerArithmetic<float>::DekkerNumber one{ 1.0 };
   const DekkerArithmetic<float>::DekkerNumber zero{ 0.0 };

   results.check(std::isnan(DekkerArithmetic<float>::toDouble(
      DekkerArithmetic<float>::add(nanNumber, one))), "NaN propagates through add");
   results.check(std::isnan(DekkerArithmetic<float>::toDouble(
      DekkerArithmetic<float>::multiply(nanNumber, one))), "NaN propagates through multiply");
   results.check(std::isnan(DekkerArithmetic<float>::toDouble(
      DekkerArithmetic<float>::divide(nanNumber, one))), "NaN propagates through divide");

   // The pair holds a value and a correction, and an infinity has no meaningful
   // correction: the compensation terms reduce to inf - inf. So an infinite
   // operand is outside the domain, and what matters is that it stays visibly
   // outside rather than collapsing into a plausible finite number.
   results.check(!std::isfinite(DekkerArithmetic<float>::toDouble(
      DekkerArithmetic<float>::add(infinityNumber, one))),
      "an infinite operand does not yield a finite sum");
   results.check(!std::isfinite(DekkerArithmetic<float>::toDouble(
      DekkerArithmetic<float>::multiply(infinityNumber, one))),
      "an infinite operand does not yield a finite product");

   // A double beyond float's range is the same boundary reached from the other
   // side: the split has no finite main part to put it in.
   const DekkerArithmetic<float>::DekkerNumber overflowed{
      static_cast<double>(std::numeric_limits<float>::max()) * 2.0 };
   results.check(!std::isfinite(DekkerArithmetic<float>::toDouble(overflowed)),
      "a double past float's range does not split into a finite pair");

   // Defined behaviour rather than an accident: a non-positive main part has no
   // real root, and the implementation answers zero instead of a NaN.
   results.check(DekkerArithmetic<float>::toDouble(
      DekkerArithmetic<float>::sqrt(DekkerArithmetic<float>::DekkerNumber{ -4.0 })) == 0.0,
      "square root of a negative main part is zero");
   results.check(DekkerArithmetic<float>::toDouble(
      DekkerArithmetic<float>::sqrt(zero)) == 0.0, "square root of zero is zero");

   // abs flips both halves together, or the pair would no longer sum to the
   // magnitude it represents.
   const DekkerArithmetic<float>::DekkerNumber negative{ -1.0 / 3.0 };
   results.check(DekkerArithmetic<float>::toDouble(DekkerArithmetic<float>::abs(negative)) ==
      -DekkerArithmetic<float>::toDouble(negative), "abs negates both halves");
}

}  // namespace

int main() {
   TestResults results{};
   const SamplePairs samples{ buildSamplePairs() };
   std::cout << "Dekker arithmetic test, " << samples.lhs.size() << " sample pairs, seed "
             << k_seed << std::endl << std::endl;

   testAccuracyAgainstFloat(results, samples);
   std::cout << std::endl;
   testProductIsExact(results, samples);
   testRoundTrip(results, samples);
   testNonFiniteBehaviour(results);

   std::cout << std::endl << results.checks() << " checks, " << results.failures()
             << " failed" << std::endl;

   // ctest reads the verdict from here; the count is clamped because a shell
   // exit status only carries a byte and 0 has to keep meaning success.
   return results.failures() == 0 ? 0 : std::min(results.failures(), 255);
}
