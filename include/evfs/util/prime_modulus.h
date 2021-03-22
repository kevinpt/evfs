// Prime modulus list used by dhash.c
// Generated with: prime_mod_gen.py --max_bits=28 -o prime_modulus.h 8L18 20E1.208 20E1.4
// Curve segments:
//     8 points (lin slope = 18.0)    11 - 137
//    20 points (exp = 1.208)         163 - 6007
//    16 points (exp = 1.4)           8389 - 933209

#define NUM_PRIME_MODULI       52
#define NUM_PRIME_MODULI_16    33

#define MAX_PRIME_MODULUS      268435399ULL
#define MAX_PRIME_MODULUS_16   32261


#define PRIME_LIST_16(M) \
  M(0, 11) \
  M(1, 29) \
  M(2, 47) \
  M(3, 67) \
  M(4, 83) \
  M(5, 101) \
  M(6, 113) \
  M(7, 137) \
  M(8, 163) \
  M(9, 199) \
  M(10, 241) \
  M(11, 293) \
  M(12, 353) \
  M(13, 421) \
  M(14, 509) \
  M(15, 619) \
  M(16, 751) \
  M(17, 907) \
  M(18, 1093) \
  M(19, 1321) \
  M(20, 1597) \
  M(21, 1931) \
  M(22, 2333) \
  M(23, 2819) \
  M(24, 3407) \
  M(25, 4111) \
  M(26, 4967) \
  M(27, 6007) \
  M(28, 8389) \
  M(29, 11743) \
  M(30, 16453) \
  M(31, 23041) \
  M(32, 32261)


#define PRIME_LIST(M) \
  M(33, 45161ULL) \
  M(34, 63241ULL) \
  M(35, 88523ULL) \
  M(36, 123941ULL) \
  M(37, 173501ULL) \
  M(38, 242923ULL) \
  M(39, 340079ULL) \
  M(40, 476137ULL) \
  M(41, 666559ULL) \
  M(42, 933209ULL) \
  M(43, 1048573ULL) \
  M(44, 2097143ULL) \
  M(45, 4194301ULL) \
  M(46, 8388593ULL) \
  M(47, 16777213ULL) \
  M(48, 33554393ULL) \
  M(49, 67108859ULL) \
  M(50, 134217689ULL) \
  M(51, 268435399ULL)
